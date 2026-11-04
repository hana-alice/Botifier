#include <apps/app_distance/alert_head_motion.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Alert = distance_pet::AlertHeadMotion;
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
        check(torque_enabled, "new hardware goal did not enable torque");
        commands.push_back({y, p, duration, now});
        current = {y, p};
    }
    void stop() { ++stops; moving = false; }
};
void tick(Alert& alert, FakeMotion& motion, std::uint32_t now, bool enabled = true)
{
    motion.now = now;
    alert.tick(motion, now, enabled);
}
void sample(Alert& alert, FakeMotion& motion, std::uint32_t start, std::uint32_t duration)
{
    for (std::uint32_t age = 0; age <= duration; age += 20) tick(alert, motion, start + age);
}

void sustained_sway_keeps_both_directions_and_one_origin()
{
    Alert alert;
    FakeMotion motion;
    sample(alert, motion, 1000, 600 + 30 * Alert::kCycleMs);
    check(alert.active() && alert.owns_lock() && motion.locked && motion.lock_writes == 1 &&
              motion.current_reads == 1 && motion.stops == 0,
          "continuous reminder stopped, reacquired or polled its origin");
    check(motion.commands.size() > 500 && motion.torque_enables == static_cast<int>(motion.commands.size()),
          "continuous reminder did not sustain torque-enabled hardware goals");
    int previous = 25;
    for (std::size_t i = 0; i < motion.commands.size(); ++i) {
        const auto& c = motion.commands[i];
        check(c.yaw >= -60 && c.yaw <= 60 && c.pitch == 150, "sway drifted yaw bounds or moved pitch");
        check(std::abs(c.yaw - previous) <= 18 && c.duration == 100, "sway exceeded rate or timed horizon");
        if (i) check(c.when - motion.commands[i - 1].when >= 100, "commands exceeded ten Hz");
        previous = c.yaw;
    }
    for (std::uint32_t cycle = 0; cycle < 30; ++cycle) {
        bool left = false, right = false;
        const auto start = 1600u + cycle * Alert::kCycleMs;
        for (const auto& c : motion.commands) {
            if (c.when >= start && c.when < start + Alert::kCycleMs) {
                left |= c.yaw == -60;
                right |= c.yaw == 60;
            }
        }
        check(left && right, "a repeated cycle lost a physical direction");
    }
}

void starts_from_measured_pose_then_pauses_without_writes()
{
    Alert alert;
    FakeMotion motion;
    motion.current = {80, 151};
    tick(alert, motion, 0);
    check(motion.commands.size() == 1 && motion.commands[0].yaw < 80 && motion.commands[0].yaw >= 77 &&
              motion.commands[0].pitch == 151,
          "first sample jumped to calibrated centre or changed pitch");
    sample(alert, motion, 20, 3000);
    check(motion.current.x == 0 && alert.active(), "sway failed to return to its centred hold");
    const auto count = motion.commands.size();
    for (std::uint32_t now = 3040; now <= 4080; now += 20) tick(alert, motion, now);
    check(motion.commands.size() == count && motion.locked && motion.current_reads == 1,
          "centred hold repeated hardware writes or released its cooperative lock");
}

void cadence_and_easing_use_current_time_only()
{
    Alert alert;
    FakeMotion motion;
    motion.current.x = 60;
    tick(alert, motion, 1000);
    const auto first = motion.commands.back().yaw;
    tick(alert, motion, 1099);
    check(motion.commands.size() == 1, "sub-100 ms tick emitted another command");
    tick(alert, motion, 1100);
    check(60 - first < first - motion.commands.back().yaw, "quintic onset did not ease its acceleration");
    const auto before = motion.commands.size();
    tick(alert, motion, 3200); // Current rightward segment, not old leftward peak.
    check(motion.commands.size() == before + 1 &&
              motion.commands.back().yaw > motion.commands[before - 1].yaw &&
              std::abs(motion.commands.back().yaw - motion.commands[before - 1].yaw) <= 18,
          "missed frame queued old targets or jumped across its rate limit");
    tick(alert, motion, 3200);
    tick(alert, motion, 3299);
    check(motion.commands.size() == before + 1, "late frames caused a catch-up command burst");
}

void disabling_and_explicit_cancel_release_only_owned_motion()
{
    Alert alert;
    FakeMotion motion;
    tick(alert, motion, 0);
    tick(alert, motion, 20, false);
    check(!alert.active() && !alert.owns_lock() && !motion.locked && motion.stops == 1 && motion.lock_writes == 2,
          "safe distance failed to stop and release its active reminder");
    alert.cancel(motion);
    tick(alert, motion, 40, false);
    check(motion.stops == 1 && motion.lock_writes == 2, "repeated cancellation stopped or unlocked twice");
    motion.current = {-30, 350};
    tick(alert, motion, 100);
    check(alert.active() && motion.current_reads == 2 && motion.commands.back().pitch == 350 &&
              std::abs(motion.commands.back().yaw + 30) <= 18,
          "new eligible interval reused the old measured pose");
    alert.cancel(motion);
    check(!alert.active() && !motion.locked && motion.stops == 2, "explicit cancellation retained hardware ownership");

    Alert uncommanded;
    FakeMotion centred;
    centred.current.x = 0;
    tick(uncommanded, centred, 0);
    check(uncommanded.active() && centred.commands.empty(), "centred onset moved unnecessarily");
    uncommanded.cancel(centred);
    check(!centred.locked && centred.lock_writes == 2 && centred.stops == 0,
          "uncommanded cancellation failed to release or stopped untouched hardware");
}

void foreign_lock_and_moving_hardware_are_untouched()
{
    for (bool locked : {false, true}) {
        Alert alert;
        FakeMotion motion;
        motion.locked = locked;
        motion.moving = !locked;
        sample(alert, motion, 0, 500);
        alert.cancel(motion);
        check(!alert.active() && motion.current_reads == 0 && motion.commands.empty() &&
                  motion.stops == 0 && motion.lock_writes == 0,
              "reminder measured, stopped, moved or unlocked foreign hardware");
        motion.locked = motion.moving = false;
        tick(alert, motion, 600);
        check(alert.active(), "temporarily busy hardware permanently suppressed this interval");
    }
}

void observed_lock_loss_abandons_until_interval_resets()
{
    Alert alert;
    FakeMotion motion;
    tick(alert, motion, 0);
    motion.locked = false;
    tick(alert, motion, 100);
    const auto count = motion.commands.size();
    check(!alert.active() && !alert.owns_lock() && motion.stops == 0 && motion.lock_writes == 1,
          "observed lock loss stopped or cleared a possible new owner");
    motion.locked = true; // Another owner after the observed gap.
    tick(alert, motion, 200);
    motion.locked = false;
    tick(alert, motion, 300);
    check(motion.commands.size() == count && motion.lock_writes == 1,
          "lost interval reacquired hardware without an eligibility reset");
    tick(alert, motion, 400, false);
    tick(alert, motion, 500);
    check(alert.active() && motion.current_reads == 2, "reset failed to allow a fresh measured session");
    // The real shared API exposes only a boolean: an entirely unobserved
    // clear-and-relock cannot be distinguished from uninterrupted ownership.
}

void malformed_one_sided_and_far_limits_block_without_motion()
{
    for (const auto limits : {Angles{0, 80}, Angles{-80, 0}, Angles{10, 20},
                              Angles{20, 10}, Angles{0, 0}}) {
        Alert alert;
        FakeMotion motion;
        motion.yaw.limits = limits;
        tick(alert, motion, 0);
        motion.yaw.limits = {-1280, 1280};
        tick(alert, motion, 100);
        check(!alert.active() && motion.current_reads == 1 && motion.commands.empty() && motion.lock_writes == 0,
              "invalid yaw window moved or retried during the same interval");
        alert.cancel(motion);
        tick(alert, motion, 200);
        check(alert.active(), "explicit reset did not unblock corrected limits");
    }
    for (const auto actual : {Angles{81, 150}, Angles{-81, 150}, Angles{0, 29}, Angles{0, 871}}) {
        Alert alert;
        FakeMotion motion;
        motion.current = actual;
        tick(alert, motion, 0);
        check(!alert.active() && motion.commands.empty() && motion.lock_writes == 0,
              "unsafe actual pose was clamped into a large reposition");
    }
    Alert alert;
    FakeMotion motion;
    motion.pitch.limits = {500, 500};
    tick(alert, motion, 0);
    check(!alert.active() && motion.commands.empty(), "malformed pitch window was admitted");
}

void tightened_limits_stop_and_do_not_reacquire()
{
    for (bool tighten_pitch : {false, true}) {
        Alert alert;
        FakeMotion motion;
        tick(alert, motion, 0);
        if (tighten_pitch) motion.pitch.limits = {31, 870};
        else motion.yaw.limits = {-30, 30};
        tick(alert, motion, 20); // Safety checked even before command cadence.
        check(!alert.active() && !motion.locked && motion.stops == 1 && motion.commands.size() == 1,
              "tightened limits continued motion or failed immediate owned cleanup");
        motion.pitch.limits = {30, 870};
        motion.yaw.limits = {-1280, 1280};
        tick(alert, motion, 100);
        check(!alert.active() && motion.current_reads == 1 && motion.lock_writes == 2,
              "unsafe interval automatically reacquired after limits expanded");
        tick(alert, motion, 200, false);
        tick(alert, motion, 300);
        check(alert.active() && motion.current_reads == 2, "new interval failed to remeasure corrected limits");
    }
}

void tight_valid_windows_clamp_both_directions_without_pitch_arithmetic()
{
    for (const auto pitch : {std::numeric_limits<int>::min() + 1, std::numeric_limits<int>::max() - 1}) {
        Alert alert;
        FakeMotion motion;
        motion.current = {0, pitch};
        motion.yaw.limits = {-7, 11};
        motion.pitch.limits = {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
        sample(alert, motion, 0, 8000);
        bool left = false, right = false;
        for (const auto& c : motion.commands) {
            check(c.yaw >= -7 && c.yaw <= 11 && c.pitch == pitch, "tight limits or fixed extreme pitch escaped bounds");
            left |= c.yaw == -7;
            right |= c.yaw == 11;
        }
        check(left && right && motion.current_reads == 1, "tight window lost one direction or polled angles");
    }
}

void unsigned_clock_wrap_preserves_cycle_and_cadence()
{
    Alert alert;
    FakeMotion motion;
    const auto start = std::numeric_limits<std::uint32_t>::max() - 350u;
    sample(alert, motion, start, 8000);
    bool left = false, right = false;
    for (std::size_t i = 0; i < motion.commands.size(); ++i) {
        const auto& c = motion.commands[i];
        if (i) check(c.when - motion.commands[i - 1].when >= 100, "clock wrap broke sample cadence");
        left |= c.yaw == -60;
        right |= c.yaw == 60;
    }
    check(left && right && alert.active() && motion.current_reads == 1, "clock wrap ended or distorted continuous sway");
    tick(alert, motion, start + 8020u, false);
    check(!alert.active() && !motion.locked && motion.stops == 1, "wrapped cancellation failed");
}
} // namespace

int main()
{
    try {
        sustained_sway_keeps_both_directions_and_one_origin();
        starts_from_measured_pose_then_pauses_without_writes();
        cadence_and_easing_use_current_time_only();
        disabling_and_explicit_cancel_release_only_owned_motion();
        foreign_lock_and_moving_hardware_are_untouched();
        observed_lock_loss_abandons_until_interval_resets();
        malformed_one_sided_and_far_limits_block_without_motion();
        tightened_limits_stop_and_do_not_reacquire();
        tight_valid_windows_clamp_both_directions_without_pitch_arithmetic();
        unsigned_clock_wrap_preserves_cycle_and_cadence();
        std::cout << "alert_head_motion_test: 10 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "alert_head_motion_test: " << error.what() << '\n';
        return 1;
    }
}
