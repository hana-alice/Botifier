#include <apps/app_distance/touch_reaction.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using Reaction = distance_pet::TouchReaction;
using Mood = distance_pet::Mood;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Angles { int x; int y; };
struct FakeServo {
    Angles limits;
    Angles getAngleLimit() const { return limits; }
};
struct Command { int yaw; int pitch; std::uint16_t duration; std::uint32_t when; };
struct FakeMotion {
    bool locked = false;
    bool moving = false;
    bool torque_enabled = false;
    Angles current{25, 150};
    FakeServo yaw{{-1280, 1280}};
    FakeServo pitch{{30, 870}};
    std::uint32_t now = 0;
    int current_reads = 0;
    int lock_writes = 0;
    int stops = 0;
    int torque_enables = 0;
    std::vector<Command> commands;

    bool isModifyLocked() const { return locked; }
    bool isMoving() const { return moving; }
    Angles getCurrentAngles() { ++current_reads; return current; }
    FakeServo& yawServo() { return yaw; }
    FakeServo& pitchServo() { return pitch; }
    void setModifyLock(bool value) { locked = value; ++lock_writes; }
    void setTorqueEnabled(bool value)
    {
        torque_enabled = value;
        if (value) ++torque_enables;
    }
    void moveWithDuration(int y, int p, std::uint16_t duration)
    {
        check(torque_enabled, "a new hardware target did not enable torque");
        commands.push_back({y, p, duration, now});
        current = {y, p};
    }
    void stop() { ++stops; moving = false; }
};

void tick(Reaction& reaction, FakeMotion& motion, std::uint32_t now)
{
    motion.now = now;
    reaction.tick(motion, now);
}

void sample(Reaction& reaction, FakeMotion& motion, std::uint32_t start, std::uint32_t duration)
{
    for (std::uint32_t age = 0; age <= duration; age += 20) tick(reaction, motion, start + age);
}

void gaze_leads_actual_pose_and_complete_flow()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 1000), "ordinary measured pose was rejected");
    check(reaction.active() && reaction.owns_lock() && motion.locked && reaction.generation() == 1,
          "accepted touch failed to own a fresh performance");
    check(reaction.phase() == Reaction::Phase::Notice && reaction.mood() == Mood::Curious,
          "touch did not begin with attention");
    const auto eyes = reaction.eye_cue(1100);
    check(eyes.directed && eyes.x < -5 && eyes.y < 0, "eyes did not lead toward the intended response");
    tick(reaction, motion, 1000);
    tick(reaction, motion, 1119);
    check(motion.commands.empty(), "physical head moved before the gaze lead elapsed");
    tick(reaction, motion, 1120);
    check(motion.commands.size() == 1 && motion.commands.front().yaw > 0 &&
              motion.commands.front().yaw < 25 && motion.commands.front().pitch >= 138,
          "head failed to ease from its measured off-centre pose");
    tick(reaction, motion, 1320);
    check(reaction.phase() == Reaction::Phase::Respond && reaction.mood() == Mood::Happy,
          "ordinary touch did not progress to its happy response");
    tick(reaction, motion, 2450);
    check(reaction.phase() == Reaction::Phase::Wait && reaction.mood() == Mood::Happy,
          "performance did not retain a feedback window");
    tick(reaction, motion, 3050);
    check(reaction.phase() == Reaction::Phase::Settle && reaction.mood() == Mood::Relief,
          "performance did not settle its expression");
    tick(reaction, motion, 3990);
    tick(reaction, motion, 4000);
    check(!reaction.active() && !reaction.owns_lock() && !motion.locked && motion.stops == 1 &&
              reaction.phase() == Reaction::Phase::Idle && reaction.mood() == Mood::Idle,
          "performance failed to finish and release at three seconds");
    check(motion.current_reads == 1 && motion.lock_writes == 2 && !reaction.eye_cue(4000).directed,
          "animation frames polled measured angles or retained a directed gaze after finishing");
}

void quintic_samples_are_bounded_and_throttled()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 0), "normal trigger failed");
    sample(reaction, motion, 0, Reaction::kDurationMs);
    check(motion.commands.size() >= 10 && motion.commands.size() <= 29,
          "sampled physical curve emitted too few or too many commands");
    check(motion.torque_enables == static_cast<int>(motion.commands.size()) && motion.current_reads == 1,
          "commands failed torque reactivation or added per-frame bus reads");
    int previous_yaw = 25;
    int previous_pitch = 150;
    bool went_up = false, went_down = false;
    for (std::size_t i = 0; i < motion.commands.size(); ++i) {
        const auto& command = motion.commands[i];
        check(command.yaw >= -80 && command.yaw <= 80 && command.pitch >= 110 && command.pitch <= 190,
              "curve left the small physical envelope");
        check(std::abs(command.yaw - previous_yaw) <= 18 && std::abs(command.pitch - previous_pitch) <= 12,
              "adjacent curve samples jumped farther than the angular rate bound");
        check(command.duration > 0 && command.duration <= 100, "sample duration exceeded its lookahead");
        if (i != 0) check(command.when - motion.commands[i - 1].when >= 100, "hardware commands exceeded 10 Hz");
        went_up = went_up || command.pitch < 145;
        went_down = went_down || command.pitch > 155;
        previous_yaw = command.yaw;
        previous_pitch = command.pitch;
    }
    const auto first_delta = std::abs(motion.commands[0].yaw - 25);
    const auto next_delta = std::abs(motion.commands[1].yaw - motion.commands[0].yaw);
    check(first_delta > 0 && next_delta > first_delta, "curve lost its eased acceleration");
    check(went_up && went_down, "response failed to combine yaw with a small pitch nod");
    check(std::abs(motion.commands.back().yaw - 25) <= 10 && std::abs(motion.commands.back().pitch - 150) <= 2,
          "settle forced calibrated zero or failed to return near its original pose");
}

void retouch_uses_actual_pose_and_changes_intent()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 1000), "first trigger failed");
    sample(reaction, motion, 1000, 400);
    const auto first_gaze = reaction.eye_cue(1100).x;
    motion.current = {60, 175}; // Measured pose deliberately differs from our old goal.
    check(reaction.trigger(motion, 1500), "valid second touch was rejected");
    const auto count = motion.commands.size();
    tick(reaction, motion, 1619);
    check(motion.commands.size() == count, "retouch bypassed its gaze lead");
    tick(reaction, motion, 1620);
    const auto goal = motion.commands.back();
    check(std::abs(goal.yaw - 60) <= 18 && std::abs(goal.pitch - 175) <= 12 && goal.yaw > 40,
          "retouch resumed an old commanded pose rather than its actual pose");
    check(reaction.eye_cue(1600).x * first_gaze < 0 && reaction.generation() == 2,
          "valid retouch failed to alternate its direction");
    tick(reaction, motion, 1820);
    check(reaction.mood() == Mood::Affection && motion.current_reads == 2 && motion.lock_writes == 1,
          "short-term touch history failed to deepen the response without reacquiring its lock");
    tick(reaction, motion, 4000);
    check(reaction.active(), "retouch did not replace the original deadline");
    tick(reaction, motion, 4500);
    check(!reaction.active() && motion.stops == 1, "retouch left a queued or unbounded performance");
}

void repeated_touches_cannot_drift_the_anchor()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 0), "first trigger failed");
    for (std::uint32_t age = 0; age <= 12000; age += 20) {
        if (age != 0 && age % 400 == 0) check(reaction.trigger(motion, age), "valid repeated touch failed");
        tick(reaction, motion, age);
    }
    check(reaction.generation() == 31 && motion.current_reads == 31 && motion.lock_writes == 1,
          "repeated input accumulated queue entries or repeatedly locked hardware");
    for (const auto& command : motion.commands) {
        check(command.yaw >= -80 && command.yaw <= 80 && command.pitch >= 110 && command.pitch <= 190,
              "continuous touches drifted the initial pitch safety window");
    }
    sample(reaction, motion, 12000, Reaction::kDurationMs);
    check(!reaction.active() && !motion.locked && motion.stops == 1, "touch stream failed to finish after the last event");
}

void debounce_and_recent_history_have_explicit_bounds()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 1000), "first trigger failed");
    check(!reaction.trigger(motion, 1399) && reaction.generation() == 1 && motion.current_reads == 1,
          "duplicate contact bypassed the 400 ms debounce");
    check(reaction.trigger(motion, 1400), "400 ms boundary was not accepted");
    sample(reaction, motion, 1400, Reaction::kDurationMs);
    check(reaction.trigger(motion, 8400), "seven-second history boundary was rejected");
    tick(reaction, motion, 8720);
    check(reaction.mood() == Mood::Affection, "recent touch boundary lost its affectionate variant");
    for (std::uint32_t age = 340; age <= Reaction::kDurationMs; age += 20) tick(reaction, motion, 8400 + age);
    motion.current = {50, 300};
    check(reaction.trigger(motion, 15401), "expired history failed to capture a new safe origin");
    tick(reaction, motion, 15721);
    check(reaction.mood() == Mood::Happy, "expired history incorrectly retained the affectionate variant");
    reaction.cancel(motion);
}

void foreign_lock_or_motion_is_never_touched()
{
    for (const bool locked : {false, true}) {
        Reaction reaction;
        FakeMotion motion;
        motion.locked = locked;
        motion.moving = !locked;
        check(!reaction.trigger(motion, 1000), "occupied physical motion was accepted");
        reaction.cancel(motion);
        tick(reaction, motion, 2000);
        check(motion.current_reads == 0 && motion.lock_writes == 0 && motion.stops == 0 && motion.commands.empty(),
              "rejected touch stopped or unlocked a foreign owner");
    }
}

void ownership_loss_abandons_without_reclaim_or_stop()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 0), "first trigger failed");
    tick(reaction, motion, 120);
    motion.locked = false;
    tick(reaction, motion, 220);
    check(!reaction.active() && !reaction.owns_lock() && motion.stops == 0 && motion.lock_writes == 1,
          "observed external unlock was reclaimed or stopped");
    motion.locked = true;
    reaction.cancel(motion);
    tick(reaction, motion, 1000);
    check(motion.locked && motion.stops == 0 && motion.lock_writes == 1 && motion.commands.size() == 1,
          "cancel touched a new owner after ownership loss");

    Reaction lost_at_trigger;
    FakeMotion other;
    check(lost_at_trigger.trigger(other, 0), "first trigger failed");
    other.locked = false;
    check(!lost_at_trigger.trigger(other, 400) && !lost_at_trigger.active() && other.lock_writes == 1,
          "retouch silently reacquired a cleared session lock");
}

void cancel_is_owned_idempotent_and_reusable()
{
    for (const bool commanded : {false, true}) {
        Reaction reaction;
        FakeMotion motion;
        reaction.cancel(motion);
        check(reaction.trigger(motion, 1000), "trigger after an unstarted cancel failed");
        if (commanded) tick(reaction, motion, 1120);
        reaction.cancel(motion);
        reaction.cancel(motion);
        tick(reaction, motion, 1500);
        check(!reaction.active() && !motion.locked && motion.lock_writes == 2 && motion.stops == (commanded ? 1 : 0),
              "cancel stopped uncommanded hardware or released twice");
        check(reaction.trigger(motion, 1600), "new explicit touch failed after cancel");
        tick(reaction, motion, 1920);
        check(reaction.mood() == Mood::Happy, "cancel retained stale interaction history");
        reaction.cancel(motion);
    }
}

void unavailable_or_far_limits_do_not_jump_into_range()
{
    for (const auto actual_yaw : {-500, -81, 81, 500, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        Reaction reaction;
        FakeMotion motion;
        motion.current.x = actual_yaw;
        check(!reaction.trigger(motion, 0) && motion.lock_writes == 0 && motion.commands.empty(),
              "distant initial pose was clamped into an abrupt calibrated-centre movement");
    }
    for (const auto invalid : {Angles{0, 0}, Angles{100, -100}, Angles{100, 200}}) {
        Reaction reaction;
        FakeMotion motion;
        motion.yaw.limits = invalid;
        check(!reaction.trigger(motion, 0) && motion.lock_writes == 0, "unavailable yaw limits acquired hardware");
    }
    Reaction no_pitch;
    FakeMotion motion;
    motion.pitch.limits = {0, 0};
    check(!no_pitch.trigger(motion, 0) && motion.lock_writes == 0, "missing pitch limits acquired hardware");
}

void tightened_limits_clamp_or_stop_without_inventing_an_origin()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 0), "first trigger failed");
    tick(reaction, motion, 120);
    motion.yaw.limits = {-20, 20};
    motion.pitch.limits = {145, 155};
    tick(reaction, motion, 220);
    check(reaction.active() && motion.commands.back().yaw >= -20 && motion.commands.back().yaw <= 20 &&
              motion.commands.back().pitch >= 145 && motion.commands.back().pitch <= 155,
          "safe tightened limits were not applied to the next sample");
    const auto count = motion.commands.size();
    motion.pitch.limits = {300, 400};
    tick(reaction, motion, 320);
    tick(reaction, motion, 420);
    check(!reaction.active() && !motion.locked && motion.stops == 1 && motion.commands.size() == count,
          "disjoint limits invented a new pitch centre or restarted a failed session");

    Reaction excluded_goal;
    FakeMotion other;
    check(excluded_goal.trigger(other, 0), "first trigger failed");
    tick(excluded_goal, other, 120);
    other.yaw.limits = {50, 60};
    tick(excluded_goal, other, 220);
    check(!excluded_goal.active() && other.commands.size() == 1 && other.stops == 1 && !other.locked,
          "a newly unsafe outstanding target caused a large clamp jump");
}

void late_frames_sample_once_and_never_replay()
{
    Reaction reaction;
    FakeMotion motion;
    check(reaction.trigger(motion, 0), "first trigger failed");
    tick(reaction, motion, 2500);
    check(motion.commands.size() == 1 && std::abs(motion.commands.back().yaw - 25) <= 18 &&
              std::abs(motion.commands.back().pitch - 150) <= 12,
          "late frame replayed the old curve or jumped across its envelope");
    tick(reaction, motion, 2500);
    tick(reaction, motion, 2599);
    check(motion.commands.size() == 1, "late repeated frames caused a command burst");
    tick(reaction, motion, 100000);
    tick(reaction, motion, 100100);
    check(!reaction.active() && !motion.locked && motion.commands.size() == 1 && motion.stops == 1,
          "expired performance replayed remaining nodes or waited forever on hardware");

    Reaction entirely_missed;
    FakeMotion other;
    check(entirely_missed.trigger(other, 0), "first trigger failed");
    tick(entirely_missed, other, 4000);
    check(!entirely_missed.active() && !other.locked && other.commands.empty() && other.stops == 0,
          "entirely missed performance moved or stopped uncommanded hardware");
}

void uint32_wrap_preserves_timing_and_debounce()
{
    Reaction reaction;
    FakeMotion motion;
    const auto start = std::numeric_limits<std::uint32_t>::max() - 150u;
    check(reaction.trigger(motion, start), "pre-wrap trigger failed");
    tick(reaction, motion, start + 120u);
    tick(reaction, motion, start + 220u);
    check(motion.commands.size() == 2 && reaction.active(), "clock wrap ended or throttled a valid performance incorrectly");
    check(!reaction.trigger(motion, start + 399u) && reaction.trigger(motion, start + 400u),
          "clock wrap broke debounce boundaries");
    sample(reaction, motion, start + 400u, Reaction::kDurationMs);
    check(!reaction.active() && !motion.locked && reaction.generation() == 2 && motion.stops == 1,
          "wrapped retouch failed to finish its replacement deadline");
}

void extreme_runtime_pitch_has_defined_bounded_arithmetic()
{
    for (const auto pitch : {std::numeric_limits<int>::min() + 10, std::numeric_limits<int>::max() - 10}) {
        Reaction reaction;
        FakeMotion motion;
        motion.current = {0, pitch};
        motion.pitch.limits = {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
        check(reaction.trigger(motion, 0), "valid extreme arithmetic fixture was rejected");
        sample(reaction, motion, 0, Reaction::kDurationMs);
        for (const auto& command : motion.commands) {
            const auto delta = static_cast<std::int64_t>(command.pitch) - pitch;
            check(delta >= -40 && delta <= 40, "extreme pitch arithmetic escaped the original small window");
        }
    }
}

} // namespace

int main()
{
    try {
        gaze_leads_actual_pose_and_complete_flow();
        quintic_samples_are_bounded_and_throttled();
        retouch_uses_actual_pose_and_changes_intent();
        repeated_touches_cannot_drift_the_anchor();
        debounce_and_recent_history_have_explicit_bounds();
        foreign_lock_or_motion_is_never_touched();
        ownership_loss_abandons_without_reclaim_or_stop();
        cancel_is_owned_idempotent_and_reusable();
        unavailable_or_far_limits_do_not_jump_into_range();
        tightened_limits_clamp_or_stop_without_inventing_an_origin();
        late_frames_sample_once_and_never_replay();
        uint32_wrap_preserves_timing_and_debounce();
        extreme_runtime_pitch_has_defined_bounded_arithmetic();
        std::cout << "touch_reaction_test: 13 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "touch_reaction_test: " << error.what() << '\n';
        return 1;
    }
}
