#include <apps/app_distance/rest_head_motion.h>
#include <stackchan/motion/motion.h>
#include <core/hal/hal.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "fakes/servo_motion/hal/hal.h"

namespace {

std::uint32_t clock_ms = 0;
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Write { int angle; std::uint16_t duration; };

// Hardware/time is the only motion substitute. Production Servo, Motion,
// RestHeadMotion and the actual UITK spring sources are compiled by this target.
class HardwareServo final : public stackchan::motion::Servo {
public:
    explicit HardwareServo(int actual = 0, uitk::Vector2i limits = {-1280, 1280}) : physical(actual)
    {
        set_angle_limit(limits);
    }

    int physical;
    bool torque = false;
    bool read_error = false;
    bool block = false;
    int protections = 0;
    int protection_stops = 0;
    std::vector<Write> writes;

    int getCurrentAngle() override
    {
        sync_hardware();
        return physical;
    }
    void setTorqueEnabled(bool enabled) override { torque = enabled; }
    bool getTorqueEnabled() override { return torque; }

protected:
    void set_angle_impl(int angle) override
    {
        physical = angle;
        hardware_active = false;
        writes.push_back({angle, 0});
    }
    void set_angle_with_duration_impl(int angle, std::uint16_t duration_ms) override
    {
        sync_hardware();
        hardware_origin = physical;
        hardware_target = angle;
        hardware_started = clock_ms;
        hardware_duration = duration_ms;
        hardware_active = true;
        writes.push_back({angle, duration_ms});
    }
    void update_timed_motion_impl(int) override
    {
        ++protections;
        sync_hardware();
        if (block) {
            ++protection_stops;
            // The real ScsServo's stall handler performs this hold after it
            // narrows its runtime limits and clears the timed state.
            stop_motion_at_angle(physical);
            set_angle_impl(physical);
        }
    }
    bool is_moving_impl() override
    {
        sync_hardware();
        return read_error || hardware_active;
    }

private:
    bool hardware_active = false;
    int hardware_origin = 0;
    int hardware_target = 0;
    std::uint32_t hardware_started = 0;
    std::uint16_t hardware_duration = 1;

    void sync_hardware()
    {
        if (!hardware_active) return;
        const auto elapsed = clock_ms - hardware_started;
        if (elapsed >= hardware_duration) {
            physical = hardware_target;
            hardware_active = false;
        } else {
            physical = hardware_origin + static_cast<int>(
                static_cast<std::int64_t>(hardware_target - hardware_origin) * elapsed / hardware_duration);
        }
    }
};

class ImmediateServo final : public stackchan::motion::Servo {
public:
    ImmediateServo() { set_angle_limit({-100, 100}); }
    int written = 0;
protected:
    void set_angle_impl(int angle) override { written = angle; }
};

void init(HardwareServo& servo)
{
    clock_ms = 0;
    servo.init();
    clock_ms = 20;
    servo.update();
    servo.writes.clear();
}

void sparse_updates_deliver_full_hardware_target()
{
    for (const auto cadence : {250u, 500u}) {
        HardwareServo servo;
        init(servo);
        const auto start = clock_ms;
        servo.moveWithDuration(80, 350);
        check(servo.writes.size() == 1 && servo.writes[0].angle == 80 && servo.writes[0].duration == 350,
              "timed move did not send its complete target and duration immediately");
        for (std::uint32_t elapsed = cadence; elapsed <= 1000; elapsed += cadence) {
            clock_ms = start + elapsed;
            servo.update();
        }
        check(servo.getCurrentAngle() == 80 && !servo.timedMotionActive() && servo.writes.size() == 1,
              "sparse updates slowed or overwrote the autonomous hardware target");
        check(servo.protections >= 1, "timed path stopped invoking the driver's obstruction checks");
    }
}

void timed_move_clears_old_real_spring()
{
    HardwareServo servo;
    init(servo);
    servo.moveWithSpeed(500, 300);
    clock_ms += 20;
    servo.update();
    servo.moveWithDuration(-80, 350);
    const auto writes = servo.writes.size();
    const auto start = clock_ms;
    for (const auto elapsed : {250u, 500u, 750u, 1000u, 1500u}) {
        clock_ms = start + elapsed;
        servo.update();
    }
    check(servo.physical == -80 && servo.writes.size() == writes, "old software spring overwrote a timed target");
}

void ordinary_target_cancels_timed_even_without_auto_sync()
{
    for (const bool auto_sync : {false, true}) {
        HardwareServo servo;
        init(servo);
        servo.setAutoAngleSyncEnabled(auto_sync);
        servo.moveWithDuration(80, 350);
        clock_ms += 100;
        const auto current = servo.getCurrentAngle();
        servo.move(current);
        check(!servo.timedMotionActive(), "ordinary target failed to cancel timed mode");
        check(servo.writes.back().duration == 20 && servo.writes.back().angle == current,
              "ordinary cancellation did not immediately override the outstanding hardware target");
        clock_ms += 20;
        servo.update();
        check(servo.physical == current && servo.writes.back().duration == 0,
              "ordinary hold did not override the previous hardware target on the next update");
        clock_ms += 1000;
        servo.update();
        check(servo.physical == current, "cancelled hardware target resumed later");
    }
}

void motion_stop_holds_both_physical_angles()
{
    clock_ms = 0;
    auto yaw = std::make_unique<HardwareServo>();
    auto pitch = std::make_unique<HardwareServo>(150, uitk::Vector2i{30, 870});
    auto* y = yaw.get();
    auto* p = pitch.get();
    stackchan::motion::Motion motion(std::move(yaw), std::move(pitch));
    motion.init();
    clock_ms = 20;
    motion.update();
    motion.moveWithDuration(80, 190, 350);
    clock_ms += 100;
    const auto current = motion.getCurrentAngles();
    motion.stop();
    check(!y->timedMotionActive() && !p->timedMotionActive(), "Motion::stop retained a timed axis");
    check(y->writes.back().duration == 20 && p->writes.back().duration == 20 &&
          y->writes.back().angle == current.x && p->writes.back().angle == current.y,
          "Motion::stop failed to override both hardware targets before another update");
    clock_ms += 20;
    motion.update();
    clock_ms += 1000;
    motion.update();
    check(y->physical == current.x && p->physical == current.y, "Motion::stop returned to an old target");
}

void limits_and_zero_duration_remain_bounded()
{
    HardwareServo servo(31, {30, 71});
    init(servo);
    servo.moveWithDuration(-100, 0);
    check(servo.writes.back().angle == 30 && servo.writes.back().duration == 1,
          "timed target bypassed angle limits or used an invalid duration");
    clock_ms += 500;
    servo.update();
    check(servo.physical == 30, "bounded timed target was not delivered");
}

void timed_obstruction_hook_cancels_before_old_spring_can_resume()
{
    HardwareServo servo;
    init(servo);
    servo.moveWithDuration(80, 350);
    servo.block = true;
    clock_ms += 100;
    servo.update();
    const auto held = servo.physical;
    check(servo.protection_stops == 1 && !servo.timedMotionActive() && servo.writes.back().duration == 0,
          "timed motion bypassed its obstruction hold");
    servo.block = false;
    clock_ms += 1000;
    servo.update();
    check(servo.physical == held, "stopped timed target resumed after an obstruction");
}

void feedback_error_remains_busy_and_does_not_release_torque()
{
    HardwareServo servo;
    init(servo);
    servo.setTorqueEnabled(true);
    servo.moveWithDuration(80, 350);
    servo.read_error = true;
    clock_ms += 1000;
    servo.update();
    check(servo.timedMotionActive() && servo.isMoving() && servo.torque,
          "timed mode treated failed motion feedback as idle");
    servo.read_error = false;
    clock_ms += 500;
    servo.update();
    clock_ms += 500;
    servo.update();
    check(!servo.timedMotionActive() && !servo.torque, "normal auto-release did not recover after valid feedback");
}

void minimum_duration_and_clock_wrap_are_honoured()
{
    HardwareServo servo;
    init(servo);
    clock_ms = 0xFFFFFF00u;
    const auto start = clock_ms;
    servo.moveWithDuration(80, 350);
    clock_ms = start + 250;
    servo.update();
    check(servo.timedMotionActive(), "timed mode ended early across clock wrap");
    clock_ms = start + 500;
    servo.update();
    check(!servo.timedMotionActive() && servo.physical == 80, "timed mode failed across clock wrap");
}

void unsupported_duration_driver_has_documented_immediate_fallback()
{
    clock_ms = 0;
    ImmediateServo servo;
    servo.init();
    servo.moveWithDuration(200, 350);
    check(servo.written == 100, "default timed hook did not issue an immediate bounded target");
    clock_ms = 500;
    servo.update();
    check(!servo.timedMotionActive(), "immediate fallback retained timed state forever");
}

void ordinary_spring_still_uses_its_existing_fixed_delta()
{
    HardwareServo servo;
    init(servo);
    servo.moveWithSpeed(80, 300);
    clock_ms += 500;
    servo.update();
    clock_ms += 500;
    servo.update();
    // Actual UITK computes 3.485 tenths of a degree after two dt=.02 calls.
    check(servo.physical == 3 && !servo.timedMotionActive(), "ordinary fixed-delta spring behavior changed");
}

void actual_controller_and_motion_keep_nonuniform_targets_at_two_hz()
{
  for (const auto cadence : {20u, 500u}) {
    clock_ms = 0;
    auto yaw = std::make_unique<HardwareServo>(87);
    auto pitch = std::make_unique<HardwareServo>(31, uitk::Vector2i{30, 870});
    auto* y = yaw.get();
    auto* p = pitch.get();
    stackchan::motion::Motion motion(std::move(yaw), std::move(pitch));
    motion.init();
    clock_ms = 20;
    motion.update();
    y->writes.clear();
    p->writes.clear();
    distance_pet::RestHeadMotion head;
    head.reserve(motion);
    const auto start = clock_ms;
    for (std::uint32_t elapsed = 0; elapsed <= idle_program::kMotionDurationMs; elapsed += cadence) {
        clock_ms = start + elapsed;
        head.tick(motion, elapsed);
        motion.update();
    }
    check(y->writes.size() == 16 && p->writes.size() == 16, "real controller/motion did not issue sixteen timed targets");
    for (const auto* servo : {y, p}) {
        for (std::size_t i = 0; i < servo->writes.size(); ++i)
            check(servo->writes[i].duration == idle_program::kMotionBeats[i].move_ms,
                  "a software spring overwrote the target's own hardware duration");
    }
    check(y->writes[2].angle == 7 && y->writes[6].angle == 167 && p->writes[10].angle == 30 && p->writes[14].angle == 71,
          "sparse production choreography failed to reach bounded endpoints in both directions");
    check(y->physical == 87 && p->physical == 50, "sparse choreography did not finish at its bounded neutral");
    head.cancel(motion);
    check(!motion.isModifyLocked(), "controller completion retained its cooperative lock");
  }
}

void actual_controller_skips_expired_targets_instead_of_replaying()
{
    clock_ms = 0;
    auto yaw = std::make_unique<HardwareServo>(87);
    auto pitch = std::make_unique<HardwareServo>(31, uitk::Vector2i{30, 870});
    auto* y = yaw.get();
    auto* p = pitch.get();
    stackchan::motion::Motion motion(std::move(yaw), std::move(pitch));
    motion.init();
    clock_ms = 20;
    motion.update();
    y->writes.clear();
    p->writes.clear();
    distance_pet::RestHeadMotion head;
    head.reserve(motion);
    const auto start = clock_ms;
    constexpr std::array<std::size_t, 12> selected{{1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15}};
    for (std::uint32_t elapsed = 1000; elapsed <= idle_program::kMotionDurationMs; elapsed += 1000) {
        clock_ms = start + elapsed;
        head.tick(motion, elapsed);
        head.tick(motion, elapsed);
        motion.update();
    }
    check(y->writes.size() == selected.size() && p->writes.size() == selected.size(),
          "slow production updates replayed expired goals or resent a duplicate frame");
    for (std::size_t i = 0; i < selected.size(); ++i) {
        const auto target = idle_program::kMotionBeats[selected[i]];
        check(y->writes[i].duration == target.move_ms && p->writes[i].duration == target.move_ms &&
                  y->writes[i].angle == 87 + target.yaw_delta,
              "slow production updates changed the current goal or its own travel duration");
    }
    clock_ms = start + idle_program::kMotionDurationMs + idle_program::kMotionBeats.back().move_ms;
    head.tick(motion, idle_program::kMotionDurationMs + idle_program::kMotionBeats.back().move_ms);
    motion.update();
    check(y->physical == 87 && p->physical == 50 && y->writes.size() == selected.size() &&
              p->writes.size() == selected.size(), "expired final centre did not finish once while retaining REST ownership");
    head.cancel(motion);
}

} // namespace

Hal& GetHAL() { static Hal hal; return hal; }
std::uint32_t Hal::millis() { return clock_ms; }

int main()
{
    smooth_ui_toolkit::ui_hal::on_get_tick([] { return clock_ms; });
    try {
        sparse_updates_deliver_full_hardware_target();
        timed_move_clears_old_real_spring();
        ordinary_target_cancels_timed_even_without_auto_sync();
        motion_stop_holds_both_physical_angles();
        limits_and_zero_duration_remain_bounded();
        timed_obstruction_hook_cancels_before_old_spring_can_resume();
        feedback_error_remains_busy_and_does_not_release_torque();
        minimum_duration_and_clock_wrap_are_honoured();
        unsupported_duration_driver_has_documented_immediate_fallback();
        ordinary_spring_still_uses_its_existing_fixed_delta();
        actual_controller_and_motion_keep_nonuniform_targets_at_two_hz();
        actual_controller_skips_expired_targets_instead_of_replaying();
        std::cout << "servo timed motion: 12 groups passed (production Servo/Motion/UITK, simulated hardware clock)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
