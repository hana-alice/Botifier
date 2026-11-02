#include <apps/app_distance/rest_head_motion.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Angles { int x; int y; };
struct FakeServo {
    Angles limits;
    Angles getAngleLimit() const { return limits; }
};
struct Command { int yaw; int pitch; int speed; };
struct FakeMotion {
    bool locked = false;
    bool moving = false;
    Angles current{15, 150};
    FakeServo yaw{{-1280, 1280}};
    FakeServo pitch{{30, 870}};
    int lock_writes = 0;
    int current_reads = 0;
    int stops = 0;
    std::vector<Command> commands;

    bool isModifyLocked() const { return locked; }
    bool isMoving() const { return moving; }
    Angles getCurrentAngles() { ++current_reads; return current; }
    FakeServo& yawServo() { return yaw; }
    FakeServo& pitchServo() { return pitch; }
    void setModifyLock(bool value) { locked = value; ++lock_writes; }
    void moveWithSpeed(int y, int p, int speed) { commands.push_back({y, p, speed}); }
    void stop() { ++stops; }
};

void normal_beats_and_centre_hold()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    for (std::uint32_t elapsed = 0; elapsed <= 5000; elapsed += 10) head.tick(motion, elapsed);
    check(head.started() && !head.skipped() && head.owns_lock(), "normal scene did not retain its lock");
    check(motion.commands.size() == 9, "normal scene must send exactly nine beats");
    check(motion.current_reads == 1 && motion.lock_writes == 1, "scene re-read its origin or reacquired its lock");
    for (std::size_t i = 0; i < motion.commands.size(); ++i) {
        const auto& command = motion.commands[i];
        check(command.yaw == motion.current.x + idle_program::kMotionBeats[i].yaw_delta &&
              command.pitch == motion.current.y + idle_program::kMotionBeats[i].pitch_delta && command.speed == 300,
              "normal beat disagreed with the shared relative choreography");
    }
    check(motion.commands.back().yaw == 15 && motion.commands.back().pitch == 150, "final beat failed to centre");
    head.tick(motion, 24000);
    check(motion.commands.size() == 9 && motion.locked && motion.stops == 0, "REST hold emitted or released early");
    head.cancel(motion);
    check(!head.owns_lock() && !motion.locked && motion.stops == 1, "completion failed to stop and release");
}

void skipped_frames_do_not_replay()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, 0);
    head.tick(motion, 1650);
    head.tick(motion, 1650);
    head.tick(motion, 1999);
    check(motion.commands.size() == 2 && motion.commands.back().yaw == -45, "jump replayed an old beat");
    head.tick(motion, 4500);
    head.tick(motion, 9000);
    check(motion.commands.size() == 3 && motion.commands.back().yaw == 15 && motion.commands.back().pitch == 150,
          "missed final beat did not force one centre command");
    head.cancel(motion);
}

void delayed_first_tick_uses_current_beat()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, 2600);
    check(motion.commands.size() == 1 && motion.commands[0].pitch == 110, "late start replayed the beginning");
    head.cancel(motion);
    distance_pet::RestHeadMotion expired;
    expired.tick(motion, 4501);
    check(motion.commands.size() == 2 && motion.commands.back().pitch == 150, "expired start failed to centre once");
    expired.cancel(motion);
}

void explicit_exit_is_idempotent_and_terminal()
{
    for (const auto elapsed : {0u, 1000u, 4300u, 21000u}) {
        distance_pet::RestHeadMotion head;
        FakeMotion motion;
        head.tick(motion, elapsed);
        const auto count = motion.commands.size();
        head.cancel(motion); // HOME, TUNE and PROGRAMS OFF share this exit.
        head.cancel(motion);
        head.tick(motion, elapsed + 500);
        check(motion.stops == 1 && motion.lock_writes == 2 && !motion.locked && motion.commands.size() == count,
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
        head.tick(motion, 0);
        check(head.skipped() && !head.started(), "occupied hardware must skip the whole motion phase");
        motion.locked = false;
        motion.moving = false;
        head.tick(motion, 1000);
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
    head.tick(motion, 0);
    motion.yaw.limits = {-30, 25};
    motion.pitch.limits = {140, 160};
    head.tick(motion, 500);
    check(motion.commands.back().yaw == -30, "new yaw minimum was not applied");
    head.tick(motion, 1000);
    check(motion.commands.back().yaw == 25, "new yaw maximum was not applied");
    head.tick(motion, 2500);
    check(motion.commands.back().pitch == 140, "new pitch minimum was not applied");
    head.tick(motion, 3000);
    check(motion.commands.back().pitch == 160, "new pitch maximum was not applied");
    motion.yaw.limits = {-30, 10};
    motion.pitch.limits = {140, 145};
    head.tick(motion, 4500);
    check(motion.commands.back().yaw == 10 && motion.commands.back().pitch == 145,
          "final centre ignored the current narrowed limits");
    head.cancel(motion);
}

void ownership_loss_is_not_reacquired_or_stopped()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, 0);
    motion.locked = false;
    head.tick(motion, 500);
    check(head.ownership_lost() && !head.owns_lock(), "external unlock was not detected");
    motion.locked = true;
    head.tick(motion, 1000);
    head.cancel(motion);
    check(motion.commands.size() == 1 && motion.lock_writes == 1 && motion.stops == 0 && motion.locked,
          "lost owner reclaimed the lock or stopped a new owner");

    distance_pet::RestHeadMotion cancelled_after_unlock;
    FakeMotion other;
    cancelled_after_unlock.tick(other, 0);
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
        head.tick(motion, 0);
        head.cancel(motion);
        check(head.skipped() && head.limits_unavailable() && !head.started() && motion.commands.empty() &&
              motion.lock_writes == 0 && motion.stops == 0, "invalid initial limits caused hardware activity");
    }
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    head.tick(motion, 0);
    motion.pitch.limits = {300, 400};
    head.tick(motion, 500);
    check(head.limits_unavailable() && !head.owns_lock() && motion.commands.size() == 1 &&
          motion.stops == 1 && !motion.locked, "shifted limits escaped the small relative motion window");
    motion.pitch.limits = {30, 870};
    head.tick(motion, 1000);
    head.cancel(motion);
    check(motion.commands.size() == 1 && motion.stops == 1, "limit failure retried or stopped twice");
}

void boundary_arithmetic_is_defined()
{
    distance_pet::RestHeadMotion head;
    FakeMotion motion;
    motion.current = {std::numeric_limits<int>::max(), std::numeric_limits<int>::min()};
    motion.yaw.limits = {std::numeric_limits<int>::max() - 10, std::numeric_limits<int>::max()};
    motion.pitch.limits = {std::numeric_limits<int>::min(), std::numeric_limits<int>::min() + 10};
    head.tick(motion, 0);
    head.tick(motion, 1000);
    head.tick(motion, 2500);
    check(motion.commands.size() == 3 && motion.commands[1].yaw == std::numeric_limits<int>::max() &&
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
    head.tick(motion, 0);
    head.tick(motion, 500);
    head.reserve(motion);
    check(head.started() && !head.skipped() && motion.lock_writes == 1 && motion.current_reads == 1 &&
          motion.commands.size() == 2 && motion.commands[0].yaw == 40 && motion.commands[0].pitch == 250 &&
          motion.commands[1].yaw == -40 && motion.commands[1].pitch == 250,
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
    head.tick(motion, 0);
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
    head.tick(motion, 0);
    check(head.skipped() && !head.started() && !head.owns_lock() && !motion.locked && motion.lock_writes == 2 &&
          motion.current_reads == 0 && motion.commands.empty() && motion.stops == 0,
          "reserved scene interrupted an unfinished prior movement");
    motion.moving = false;
    head.reserve(motion);
    head.tick(motion, 500);
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
            head.tick(motion, 0);
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
        head.tick(motion, 500);
        head.cancel(motion);
        check(head.ownership_lost() && !head.started() && !head.owns_lock() && motion.locked &&
              motion.lock_writes == 1 && motion.current_reads == 0 && motion.commands.empty() && motion.stops == 0,
              "lost reservation reclaimed or modified another owner");
    }
}

} // namespace

int main()
{
    try {
        normal_beats_and_centre_hold();
        skipped_frames_do_not_replay();
        delayed_first_tick_uses_current_beat();
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
        std::cout << "rest head motion: 14 groups passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
