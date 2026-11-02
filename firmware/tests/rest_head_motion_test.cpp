#include <apps/app_distance/rest_head_motion.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr auto kDurationMs = idle_program::kMotionDurationMs;
constexpr auto kBeatCount = idle_program::kMotionBeats.size();

constexpr std::uint32_t at(std::size_t index)
{
    return idle_program::kMotionBeats[index].start_ms;
}

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Angles { int x; int y; };
struct FakeServo {
    Angles limits;
    Angles getAngleLimit() const { return limits; }
};
struct Command { int yaw; int pitch; int duration_ms; };
struct FakeMotion {
    bool locked = false;
    bool moving = false;
    bool torque_enabled = false;
    Angles current{15, 150};
    FakeServo yaw{{-1280, 1280}};
    FakeServo pitch{{30, 870}};
    int lock_writes = 0;
    int current_reads = 0;
    int stops = 0;
    int torque_enables = 0;
    int torque_disables = 0;
    std::vector<Command> commands;
    std::vector<Command> effective_commands;

    bool isModifyLocked() const { return locked; }
    bool isMoving() const { return moving; }
    Angles getCurrentAngles() { ++current_reads; return current; }
    FakeServo& yawServo() { return yaw; }
    FakeServo& pitchServo() { return pitch; }
    void setModifyLock(bool value) { locked = value; ++lock_writes; }
    void setTorqueEnabled(bool enabled)
    {
        torque_enabled = enabled;
        if (enabled) ++torque_enables;
        else ++torque_disables;
    }
    void moveWithDuration(int y, int p, std::uint16_t duration_ms)
    {
        commands.push_back({y, p, duration_ms});
        if (torque_enabled) effective_commands.push_back({y, p, duration_ms});
    }
    void stop() { ++stops; }
};

void normal_beats_and_centre_hold()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    for (std::uint32_t elapsed = 0; elapsed <= kDurationMs + 1000u; elapsed += 10) head.tick(motion, elapsed);
    check(head.started() && !head.skipped() && head.owns_lock(), "normal scene did not retain its lock");
    check(motion.commands.size() == kBeatCount, "normal scene must send exactly the scheduled beats");
    check(motion.current_reads == 1 && motion.lock_writes == 1, "scene re-read its origin or reacquired its lock");
    for (std::size_t i = 0; i < motion.commands.size(); ++i) {
        const auto& command = motion.commands[i];
        check(command.yaw == idle_program::kMotionBeats[i].yaw_delta &&
              command.pitch == motion.current.y + idle_program::kMotionBeats[i].pitch_delta &&
              command.duration_ms == idle_program::kMotionBeats[i].move_ms,
              "normal beat disagreed with the shared relative choreography");
    }
    check(motion.commands.back().yaw == 0 && motion.commands.back().pitch == 150, "final beat failed to face calibrated home");
    head.tick(motion, kDurationMs + 20000);
    check(motion.commands.size() == kBeatCount && motion.locked && motion.stops == 0, "REST hold emitted or released early");
    head.cancel(motion);
    check(!head.owns_lock() && !motion.locked && motion.stops == 1, "completion failed to stop and release");
}

void skipped_frames_do_not_replay()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, at(0));
    head.tick(motion, at(5) + 150u);
    head.tick(motion, at(5) + 150u);
    head.tick(motion, at(6) - 1u);
    head.tick(motion, at(0));
    check(motion.commands.size() == 2 && motion.commands.back().yaw == 60 &&
              motion.commands.back().duration_ms == 360, "jump or backdated tick replayed an old beat or changed the current duration");
    head.tick(motion, kDurationMs);
    head.tick(motion, 2 * kDurationMs);
    check(motion.commands.size() == 3 && motion.commands.back().yaw == 0 && motion.commands.back().pitch == 150,
          "missed final beat did not force one centre command");
    head.cancel(motion);
}

void delayed_tick_after_preparation_uses_current_beat()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, 0);
    head.tick(motion, at(11) + 100u);
    check(motion.commands.size() == 2 && motion.commands.front().duration_ms == 700 &&
              motion.commands.back().pitch == 110, "late running tick replayed a target after preparation");
    head.cancel(motion);
    distance_pet::RestHeadMotion expired;
    expired.tick(motion, 0);
    expired.tick(motion, kDurationMs + 1);
    check(motion.commands.size() == 4 && motion.commands.back().pitch == 150, "expired running session failed to centre once");
    expired.cancel(motion);
}

void explicit_exit_is_idempotent_and_terminal()
{
    for (const auto elapsed : {0u, 350u, at(1), kDurationMs - 200u, kDurationMs + 20000u}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        head.tick(motion, 0);
        head.tick(motion, elapsed);
        const auto count = motion.commands.size();
        head.cancel(motion); // HOME, TUNE and PROGRAMS OFF share this exit.
        head.cancel(motion);
        head.tick(motion, elapsed + 1000u);
        check(motion.stops == (count ? 1 : 0) && motion.lock_writes == (count ? 2 : 0) &&
                  !motion.locked && motion.commands.size() == count,
              "explicit exit changed hardware twice or restarted a cancelled scene");
    }
    distance_pet::RestHeadMotion never_started;
    FakeMotion motion;
    never_started.cancel(motion);
    never_started.cancel(motion);
    never_started.tick(motion, 0);
    check(motion.stops == 0 && motion.lock_writes == 0 && motion.commands.empty(), "unstarted exit touched hardware");
}

void busy_or_locked_first_attempt_is_skipped()
{
    for (const bool locked : {false, true}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        motion.locked = locked;
        motion.moving = !locked;
        head.tick(motion, at(0));
        check(head.skipped() && !head.started(), "occupied hardware must skip the whole motion phase");
        motion.locked = false;
        motion.moving = false;
        head.tick(motion, at(2));
        motion.locked = true; // A different owner may acquire it before cancel.
        head.cancel(motion);
        check(motion.current_reads == 0 && motion.lock_writes == 0 && motion.commands.empty() &&
              motion.stops == 0 && motion.locked, "skipped scene retried or modified another owner");
    }
}

void dynamic_limits_are_read_on_each_new_beat()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, at(0));
    motion.yaw.limits = {-30, 25};
    motion.pitch.limits = {140, 160};
    head.tick(motion, at(3));
    check(motion.commands.back().yaw == -30, "new yaw minimum was not applied");
    head.tick(motion, at(7));
    check(motion.commands.back().yaw == 25, "new yaw maximum was not applied");
    head.tick(motion, at(11));
    check(motion.commands.back().pitch == 140, "new pitch minimum was not applied");
    head.tick(motion, at(15));
    check(motion.commands.back().pitch == 160, "new pitch maximum was not applied");
    motion.yaw.limits = {-30, 10};
    motion.pitch.limits = {140, 145};
    head.tick(motion, kDurationMs);
    check(motion.commands.back().yaw == 0 && motion.commands.back().pitch == 142,
          "final centre ignored the current narrowed limits");
    head.cancel(motion);
}

void ownership_loss_is_not_reacquired_or_stopped()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, at(0));
    motion.locked = false;
    head.tick(motion, at(1));
    check(head.ownership_lost() && !head.owns_lock(), "external unlock was not detected");
    motion.locked = true;
    head.tick(motion, at(2));
    head.cancel(motion);
    check(motion.commands.size() == 1 && motion.lock_writes == 1 && motion.stops == 0 && motion.locked,
          "lost owner reclaimed the lock or stopped a new owner");

    distance_pet::RestHeadMotion cancelled_after_unlock;
    FakeMotion other;
    cancelled_after_unlock.tick(other, at(0));
    other.locked = false;
    cancelled_after_unlock.cancel(other);
    check(cancelled_after_unlock.ownership_lost() && other.stops == 0 && other.lock_writes == 1,
          "cancel after ownership loss touched hardware");
}

void unsatisfiable_limits_fail_without_larger_movement()
{
    for (const auto bad_limit : {Angles{200, -200}, Angles{500, 800}}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        motion.yaw.limits = bad_limit;
        head.tick(motion, at(0));
        head.cancel(motion);
        check(head.skipped() && head.limits_unavailable() && !head.started() && motion.commands.empty() &&
              motion.lock_writes == 0 && motion.stops == 0, "invalid initial limits caused hardware activity");
    }
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, at(0));
    motion.pitch.limits = {300, 400};
    head.tick(motion, at(1));
    check(head.limits_unavailable() && !head.owns_lock() && motion.commands.size() == 1 &&
          motion.stops == 1 && !motion.locked, "shifted limits escaped the small relative motion window");
    motion.pitch.limits = {30, 870};
    head.tick(motion, at(2));
    head.cancel(motion);
    check(motion.commands.size() == 1 && motion.stops == 1, "limit failure retried or stopped twice");
}

void boundary_arithmetic_is_defined()
{
  for (const auto yaw : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
    distance_pet::RestHeadMotion outside;
    FakeMotion extreme;
    extreme.current.x = yaw;
    outside.tick(extreme, 0);
    check(outside.skipped() && !outside.started() && extreme.commands.empty() && extreme.lock_writes == 0 &&
              extreme.torque_enables == 0, "extreme initial yaw overflowed the bounded-home guard");
  }
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.current = {0, std::numeric_limits<int>::min()};
    motion.pitch.limits = {std::numeric_limits<int>::min(), std::numeric_limits<int>::min() + 10};
    head.tick(motion, at(0));
    head.tick(motion, at(7));
    head.tick(motion, at(11));
    check(motion.commands.size() == 3 && motion.commands[1].yaw == 80 &&
          motion.commands[2].pitch == std::numeric_limits<int>::min(), "boundary arithmetic overflowed");
    head.cancel(motion);
}

void reservation_leaves_previous_motion_running_and_captures_late_base()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.moving = true;
    head.reserve(motion);
    head.reserve(motion);
    check(head.owns_lock() && !head.started() && !head.skipped() && motion.lock_writes == 1 &&
          motion.current_reads == 0 && motion.commands.empty() && motion.stops == 0,
          "intro reservation moved, stopped or captured the previous motion");
    motion.current = {40, 250}; // The previous movement finishes during the intro.
    motion.moving = false;
    head.tick(motion, at(0));
    head.tick(motion, at(3));
    head.reserve(motion);
    check(head.started() && !head.skipped() && motion.lock_writes == 1 && motion.current_reads == 1 &&
          motion.commands.size() == 2 && motion.commands[0].yaw == 0 && motion.commands[0].pitch == 250 &&
          motion.commands[1].yaw == -80 && motion.commands[1].pitch == 250,
          "reserved scene rejected its own lock or used the pre-intro origin");
    head.cancel(motion);
    check(motion.stops == 1 && !motion.locked, "started reservation did not stop and release on exit");
}

void reservation_does_not_steal_other_lock_or_retry()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.locked = true;
    motion.moving = true;
    head.reserve(motion);
    motion.locked = false;
    motion.moving = false;
    head.reserve(motion);
    head.tick(motion, at(0));
    head.cancel(motion);
    check(head.skipped() && !head.started() && !head.owns_lock() && motion.lock_writes == 0 &&
          motion.current_reads == 0 && motion.commands.empty() && motion.stops == 0,
          "intro reservation stole a lock or retried after rejection");
}

void reserved_but_still_moving_skips_without_stopping_previous_motion()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.moving = true;
    head.reserve(motion);
    head.tick(motion, at(0));
    check(head.skipped() && !head.started() && !head.owns_lock() && !motion.locked && motion.lock_writes == 2 &&
          motion.current_reads == 0 && motion.commands.empty() && motion.stops == 0,
          "reserved scene interrupted an unfinished prior movement");
    motion.moving = false;
    head.reserve(motion);
    head.tick(motion, at(1));
    head.cancel(motion);
    check(motion.lock_writes == 2 && motion.commands.empty() && motion.stops == 0,
          "unfinished prior movement caused a later retry or stop");
}

void reserve_only_cancel_or_bad_limits_never_stops_other_motion()
{
    for (const bool invalid_limits : {false, true}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        motion.moving = true;
        head.reserve(motion);
        if (invalid_limits) {
            motion.moving = false;
            motion.yaw.limits = {500, 800};
            head.tick(motion, at(0));
            check(head.skipped() && head.limits_unavailable(), "reserved invalid limits were not rejected");
        }
        head.cancel(motion);
        head.cancel(motion);
        head.reserve(motion);
        head.tick(motion, 0);
        check(!head.started() && !head.owns_lock() && !motion.locked && motion.lock_writes == 2 &&
              motion.commands.empty() && motion.stops == 0,
              "reserve-only cancellation stopped an unowned movement or leaked its lock");
    }
}

void reservation_ownership_loss_is_terminal()
{
    for (const bool detect_with_reserve : {false, true}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        head.reserve(motion);
        motion.locked = false;
        if (detect_with_reserve) head.reserve(motion);
        else head.tick(motion, 0);
        motion.locked = true;
        head.reserve(motion);
        head.tick(motion, at(1));
        head.cancel(motion);
        check(head.ownership_lost() && !head.started() && !head.owns_lock() && motion.locked &&
              motion.lock_writes == 1 && motion.current_reads == 0 && motion.commands.empty() && motion.stops == 0,
              "lost reservation reclaimed or modified another owner");
    }
}

void each_new_beat_reenables_torque_before_its_target()
{
    // Model the hardware distinction that a queued goal is not effective while
    // torque is disabled. The old controller passed a command-count-only fake.
    FakeMotion control;
    control.moveWithDuration(50, 150, 350);
    check(control.commands.size() == 1 && control.effective_commands.empty(),
          "torque-disabled control model accepted an effective movement");

    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.reserve(motion);
    check(motion.torque_enables == 0, "intro reservation enabled torque");
    for (std::uint32_t beat = 0; beat < idle_program::kMotionBeats.size(); ++beat) {
        motion.torque_enabled = false; // Auto-release may occur between beats.
        head.tick(motion, at(beat));
        check(motion.torque_enables == static_cast<int>(beat + 1) &&
              motion.effective_commands.size() == beat + 1,
              "new beat did not enable torque before sending its goal");
        motion.torque_enabled = false;
        head.tick(motion, at(beat) + 10u);
        check(motion.torque_enables == static_cast<int>(beat + 1) &&
              motion.effective_commands.size() == beat + 1,
              "duplicate beat unexpectedly enabled torque or resent its goal");
    }
    head.tick(motion, kDurationMs);
    head.tick(motion, kDurationMs + 20000);
    head.cancel(motion);
    check(motion.torque_enables == static_cast<int>(kBeatCount) && motion.torque_disables == 0 &&
          motion.effective_commands.size() == kBeatCount,
          "REST hold or cancellation changed torque settings");
}

void rejected_or_cancelled_reservations_never_enable_torque()
{
    for (const bool locked : {false, true}) {
        distance_pet::RestHeadMotion busy;
        FakeMotion motion;
        motion.locked = locked;
        motion.moving = !locked;
        busy.tick(motion, at(0));
        busy.cancel(motion);
        check(motion.torque_enables == 0 && motion.torque_disables == 0,
              "rejected direct scene changed another movement's torque");
    }
    distance_pet::RestHeadMotion reserved;
    FakeMotion motion;
    reserved.reserve(motion);
    reserved.cancel(motion);
    check(motion.torque_enables == 0 && motion.torque_disables == 0, "reserve-only cancellation changed torque");
    distance_pet::RestHeadMotion still_moving;
    still_moving.reserve(motion);
    motion.moving = true;
    still_moving.tick(motion, at(0));
    still_moving.cancel(motion);
    check(motion.torque_enables == 0 && motion.torque_disables == 0, "unfinished prior movement changed torque");
}

void lost_ownership_or_limits_never_reenable_torque()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, at(0));
    check(motion.torque_enables == 1 && motion.effective_commands.size() == 1, "initial beat did not enable torque");
    motion.locked = false;
    motion.torque_enabled = false;
    head.tick(motion, at(1));
    head.cancel(motion);
    check(motion.torque_enables == 1 && !motion.torque_enabled, "lost owner reenabled torque");

    distance_pet::RestHeadMotion bad_limits;
    FakeMotion invalid;
    bad_limits.reserve(invalid);
    invalid.yaw.limits = {500, 800};
    bad_limits.tick(invalid, at(0));
    bad_limits.cancel(invalid);
    check(invalid.torque_enables == 0 && invalid.torque_disables == 0,
          "invalid initial limits enabled torque without a safe target");
}

void four_directions_repeat_two_complete_excursions()
{
    check(kDurationMs == 12700 && kBeatCount == 17,
          "head sequence must prepare home then perform sixteen targets");
    constexpr std::array<Angles, 17> expected{{
        {0, 0}, {-60, 0}, {0, 0}, {-80, 0}, {0, 0}, {60, 0}, {0, 0}, {80, 0}, {0, 0},
        {0, -30}, {0, 0}, {0, -40}, {0, 0}, {0, 30}, {0, 0}, {0, 40}, {0, 0},
    }};
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    for (std::uint32_t i = 0; i < expected.size(); ++i) {
        if (i) {
            head.tick(motion, at(i) - 1u);
            check(motion.commands.size() == i, "a head target began before its actual start boundary");
        }
        head.tick(motion, at(i));
        check(motion.commands.back().yaw == expected[i].x && motion.commands.back().pitch == 150 + expected[i].y,
              "a direction did not return to centre between its two excursions");
        check(motion.commands.back().duration_ms == idle_program::kMotionBeats[i].move_ms,
              "a target did not use its own specified travel duration");
        head.tick(motion, at(i) + 1u);
        check(motion.commands.size() == i + 1u, "duplicate frame resent a head target");
    }
    head.tick(motion, kDurationMs - 1);
    check(motion.commands.size() == 17 && motion.commands.back().yaw == 0 && motion.commands.back().pitch == 150,
          "final centre did not retain its complete pause");
    head.cancel(motion);
}

void starting_at_pitch_edge_uses_both_directions_inside_original_window()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.current = {128, 31};
    for (std::uint32_t i = 0; i < kBeatCount; ++i) {
        head.tick(motion, at(i));
        const auto& command = motion.commands.back();
        check(command.yaw >= -80 && command.yaw <= 80 && command.pitch >= 30 && command.pitch <= 71,
              "yaw escaped the home window or pitch escaped its original actual-angle envelope");
    }
    check(motion.commands[9].pitch == 35 && motion.commands[11].pitch == 30 &&
          motion.commands[13].pitch == 65 && motion.commands[15].pitch == 71 && motion.commands[16].pitch == 50,
          "pitch near its stop did not have two visible beats in both directions and a bounded centre");
    check(motion.commands[3].yaw == -80 && motion.commands[7].yaw == 80 && motion.commands[16].yaw == 0,
          "an off-centre starting yaw prevented a real crossing of calibrated home");
    head.cancel(motion);
}

void shrinking_limits_recentre_without_expanding_original_window()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.current.y = 31;
    head.tick(motion, at(0));
    motion.pitch.limits = {45, 65};
    head.tick(motion, at(11));
    check(motion.commands.back().pitch == 45, "tightened pitch minimum was not honoured");
    head.tick(motion, at(12));
    check(motion.commands.back().pitch == 55, "tightened pitch window did not retain a bounded neutral");
    head.tick(motion, at(15));
    check(motion.commands.back().pitch == 65, "tightened pitch maximum was not honoured");
    motion.pitch.limits = {100, 110};
    head.tick(motion, at(16));
    check(head.limits_unavailable() && !head.owns_lock() && motion.stops == 1,
          "shifted physical limits caused an expanded movement envelope");
    head.cancel(motion);
}

void home_preparation_is_timed_after_reservation_and_can_be_cancelled()
{
    for (const bool reserve : {false, true}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        if (reserve) head.reserve(motion);
        check(!head.started() && motion.current_reads == 0 && motion.commands.empty() &&
                  motion.lock_writes == (reserve ? 1 : 0) && motion.torque_enables == 0 && motion.stops == 0,
              "intro reservation began centring before the video finished");
        head.tick(motion, 0);
        head.tick(motion, 699);
        head.tick(motion, 700);
        check(head.started() && motion.commands.size() == 1 && motion.commands.front().duration_ms == 700 &&
                  motion.commands.front().yaw == 0 && motion.commands.front().pitch == 150 &&
                  motion.torque_enables == 1 && motion.current_reads == 1,
              "home preparation did not send exactly one 700 ms calibrated-centre target");
        head.tick(motion, at(1));
        check(motion.commands.size() == 2 && motion.commands.back().yaw == -60,
              "first excursion did not wait until after the complete preparation");
        head.cancel(motion);
    }
    distance_pet::RestHeadMotion cancelled;
    FakeMotion motion;
    cancelled.reserve(motion);
    cancelled.tick(motion, 0);
    cancelled.tick(motion, 350);
    cancelled.cancel(motion);
    cancelled.tick(motion, at(1));
    check(motion.commands.size() == 1 && motion.stops == 1 && motion.torque_enables == 1 && !motion.locked &&
              motion.lock_writes == 2,
          "cancelling home preparation failed to stop it once or leaked its reservation");
}

void positive_and_negative_initial_yaw_share_calibrated_home()
{
    for (const auto initial : {-300, -128, 0, 128, 300}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        motion.current.x = initial;
        for (std::size_t i = 0; i < kBeatCount; ++i) head.tick(motion, at(i));
        check(motion.commands.size() == 17 && motion.commands.front().yaw == 0 &&
                  motion.commands.front().duration_ms == 700,
              "bounded off-centre initial yaw did not first move to calibrated home");
        for (std::size_t i = 0; i < kBeatCount; ++i)
            check(motion.commands[i].yaw == idle_program::kMotionBeats[i].yaw_delta,
                  "performance still used its off-centre starting pose as the yaw neutral");
        check(motion.commands[1].yaw < 0 && motion.commands[3].yaw < 0 && motion.commands[5].yaw > 0 &&
                  motion.commands[7].yaw > 0 && motion.commands.back().yaw == 0,
              "yaw span did not represent actual negative and positive sides of home");
        head.cancel(motion);
    }
    for (const bool reserve : {false, true}) {
        for (const auto initial : {-301, 301}) {
            distance_pet::RestHeadMotion head;
            FakeMotion motion;
            motion.current.x = initial;
            if (reserve) head.reserve(motion);
            head.tick(motion, 0);
            head.tick(motion, at(1));
            head.cancel(motion);
            check(head.skipped() && !head.started() && !head.owns_lock() && motion.commands.empty() &&
                      motion.torque_enables == 0 && motion.stops == 0 && !motion.locked &&
                      motion.lock_writes == (reserve ? 2 : 0),
                  "initial yaw outside thirty degrees attempted a large recenter or leaked its reservation");
        }
    }
}

void yaw_limits_must_cross_home_and_never_redefine_neutral()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.current.x = 128;
    motion.yaw.limits = {-30, 10};
    for (std::size_t i = 0; i < kBeatCount; ++i) head.tick(motion, at(i));
    for (std::size_t i = 0; i < kBeatCount; ++i)
        check(motion.commands[i].yaw == std::clamp<int>(idle_program::kMotionBeats[i].yaw_delta, -30, 10),
              "tight asymmetric yaw limits changed home or escaped their permitted sides");
    check(motion.commands[3].yaw == -30 && motion.commands[7].yaw == 10 && motion.commands.back().yaw == 0,
          "tight crossing limits did not preserve both signs and calibrated neutral");
    head.cancel(motion);

    for (const auto limits : {Angles{0, 80}, Angles{-80, 0}, Angles{10, 80}, Angles{-80, -1}, Angles{0, 0}}) {
        distance_pet::RestHeadMotion blocked;
        FakeMotion invalid;
        invalid.yaw.limits = limits;
        blocked.reserve(invalid);
        blocked.tick(invalid, 0);
        blocked.tick(invalid, at(1));
        blocked.cancel(invalid);
        check(blocked.skipped() && blocked.limits_unavailable() && !blocked.started() &&
                  invalid.commands.empty() && invalid.torque_enables == 0 && invalid.stops == 0 && !invalid.locked,
              "one-sided yaw limits manufactured another off-centre neutral");
    }
    distance_pet::RestHeadMotion changed;
    FakeMotion dynamic;
    changed.tick(dynamic, 0);
    dynamic.yaw.limits = {0, 80};
    changed.tick(dynamic, at(1));
    check(changed.limits_unavailable() && !changed.owns_lock() && dynamic.commands.size() == 1 &&
              dynamic.stops == 1 && !dynamic.locked,
          "a newly one-sided yaw range did not stop an already-started preparation");
    changed.cancel(dynamic);
}

void preparation_deadline_accepts_120_and_rejects_late_first_ticks()
{
    constexpr auto deadline = idle_program::kMotionBeats[1].start_ms -
                              idle_program::kMotionBeats.front().move_ms;
    static_assert(deadline == 120);
    for (const bool reserve : {false, true}) {
        distance_pet::RestHeadMotion accepted;
        FakeMotion motion;
        if (reserve) accepted.reserve(motion);
        accepted.tick(motion, deadline);
        check(accepted.started() && !accepted.skipped() && accepted.owns_lock() &&
                  motion.commands.size() == 1 && motion.commands.front().duration_ms == 700 &&
                  motion.commands.front().yaw == 0,
              "exactly 120 ms first tick did not preserve the complete preparation");
        accepted.tick(motion, at(1));
        check(motion.commands.size() == 2 && motion.commands.back().yaw == -60,
              "accepted preparation did not proceed to the first scheduled excursion");
        accepted.cancel(motion);

        for (const auto late : {deadline + 1u, 1000u, kDurationMs + 1u}) {
            distance_pet::RestHeadMotion rejected;
            FakeMotion other;
            if (reserve) rejected.reserve(other);
            other.moving = true; // Existing movement must keep running.
            rejected.tick(other, late);
            rejected.tick(other, 0);
            rejected.tick(other, at(1));
            rejected.cancel(other);
            check(rejected.skipped() && !rejected.started() && !rejected.owns_lock() &&
                      other.commands.empty() && other.current_reads == 0 && other.torque_enables == 0 &&
                      other.stops == 0 && other.moving && !other.locked &&
                      other.lock_writes == (reserve ? 2 : 0),
                  "late first tick moved, stopped, retried, or leaked an existing reservation");
        }
    }
    distance_pet::RestHeadMotion foreign;
    FakeMotion occupied;
    occupied.locked = true;
    occupied.moving = true;
    foreign.tick(occupied, 1000);
    foreign.cancel(occupied);
    check(foreign.skipped() && !foreign.started() && occupied.locked && occupied.moving &&
              occupied.lock_writes == 0 && occupied.stops == 0 && occupied.commands.empty() &&
              occupied.current_reads == 0 && occupied.torque_enables == 0,
          "late unreserved first tick changed another owner's lock or motion");
}

} // namespace

int main()
{
    try {
        normal_beats_and_centre_hold();
        skipped_frames_do_not_replay();
        delayed_tick_after_preparation_uses_current_beat();
        explicit_exit_is_idempotent_and_terminal();
        busy_or_locked_first_attempt_is_skipped();
        dynamic_limits_are_read_on_each_new_beat();
        ownership_loss_is_not_reacquired_or_stopped();
        unsatisfiable_limits_fail_without_larger_movement();
        boundary_arithmetic_is_defined();
        reservation_leaves_previous_motion_running_and_captures_late_base();
        reservation_does_not_steal_other_lock_or_retry();
        reserved_but_still_moving_skips_without_stopping_previous_motion();
        reserve_only_cancel_or_bad_limits_never_stops_other_motion();
        reservation_ownership_loss_is_terminal();
        each_new_beat_reenables_torque_before_its_target();
        rejected_or_cancelled_reservations_never_enable_torque();
        lost_ownership_or_limits_never_reenable_torque();
        four_directions_repeat_two_complete_excursions();
        starting_at_pitch_edge_uses_both_directions_inside_original_window();
        shrinking_limits_recentre_without_expanding_original_window();
        home_preparation_is_timed_after_reservation_and_can_be_cancelled();
        positive_and_negative_initial_yaw_share_calibrated_home();
        yaw_limits_must_cross_home_and_never_redefine_neutral();
        preparation_deadline_accepts_120_and_rejects_late_first_ticks();
        std::cout << "rest head motion: 24 groups passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
