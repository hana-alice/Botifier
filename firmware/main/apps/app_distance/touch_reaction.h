#pragma once

#include "pet_motion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace distance_pet {

// The app admits touches only while ordinary LIVE content owns the screen.
// This controller owns one bounded physical performance; it never queues input
// and never takes a lock from the NFC/eye-rest or another motion owner.
class TouchReaction {
public:
    enum class Phase { Idle, Notice, Respond, Wait, Settle };
    static constexpr std::uint32_t kDurationMs = 3000;
    static constexpr std::uint32_t kCommandIntervalMs = 100;
    static constexpr std::uint32_t kTouchIntervalMs = 400;
    static constexpr std::uint32_t kRecentMs = 7000;

    template <typename Motion>
    bool trigger(Motion& motion, std::uint32_t now_ms)
    {
        if (_active && !check_ownership(motion)) return false;
        if (_has_history && now_ms - _last_touch_ms < kTouchIntervalMs) return false;
        if (!_active && (motion.isModifyLocked() || motion.isMoving())) return false;

        // On the real HAL these are the virtual, measured servo angles. Do not
        // poll them on every animation frame or infer them from our old target.
        const auto actual = motion.getCurrentAngles();
        const bool recent = _has_history && now_ms - _last_touch_ms <= kRecentMs;
        const Pose anchor = recent ? _anchor : Pose{actual.x, actual.y};
        Window window;
        if (!limits(motion, anchor, window) || !contains(window, {actual.x, actual.y})) {
            if (_active) cancel(motion);
            return false;
        }

        if (!_owns_lock) {
            motion.setModifyLock(true);
            _owns_lock = true;
            _motion_sent = false;
        }
        _anchor = anchor; // Repeated touches keep one origin and cannot drift.
        _from = {actual.x, actual.y};
        _last_goal = _from;
        _affectionate = recent;
        _direction = (++_generation & 1u) ? -1 : 1;
        _start_ms = now_ms;
        _last_touch_ms = now_ms;
        _has_history = true;
        _active = true;
        _phase = Phase::Notice;
        return true;
    }

    template <typename Motion>
    void tick(Motion& motion, std::uint32_t now_ms)
    {
        if (!_active || !check_ownership(motion)) return;
        const auto age = now_ms - _start_ms;
        if (age >= kDurationMs) {
            finish(motion);
            return;
        }
        _phase = phase_at(age);
        // Eyes have time to notice the touch before the head follows. A late
        // frame samples the current curve once; it never replays old waypoints.
        if (age < kGazeLeadMs || (_motion_sent && now_ms - _last_command_ms < kCommandIntervalMs)) return;

        Window window;
        if (!limits(motion, _anchor, window) || !contains(window, _last_goal)) {
            // A tightened obstruction limit must not turn into a larger move
            // toward a newly invented centre. Stop only our own performance.
            cancel(motion);
            return;
        }
        const auto horizon = std::min(kCommandIntervalMs, kDurationMs - age);
        auto goal = sampled_head(age + horizon);
        goal.yaw = std::clamp(goal.yaw, window.yaw_low, window.yaw_high);
        goal.pitch = std::clamp(goal.pitch, window.pitch_low, window.pitch_high);
        // Bounds also apply after a missed frame or a rapid opposite retouch.
        // A very short final sample retains the same maximum angular rate.
        const auto yaw_step = std::max(1, static_cast<int>(18u * horizon / kCommandIntervalMs));
        const auto pitch_step = std::max(1, static_cast<int>(12u * horizon / kCommandIntervalMs));
        goal.yaw = approach(_last_goal.yaw, goal.yaw, yaw_step);
        goal.pitch = approach(_last_goal.pitch, goal.pitch, pitch_step);
        if (goal.yaw == _last_goal.yaw && goal.pitch == _last_goal.pitch) return;

        motion.setTorqueEnabled(true);
        motion.moveWithDuration(goal.yaw, goal.pitch, static_cast<std::uint16_t>(horizon));
        _last_goal = goal;
        _last_command_ms = now_ms;
        _motion_sent = true;
    }

    template <typename Motion>
    void cancel(Motion& motion)
    {
        finish(motion);
        _has_history = false;
    }

    bool active() const noexcept { return _active; }
    bool owns_lock() const noexcept { return _owns_lock; }
    std::uint32_t generation() const noexcept { return _generation; }
    Phase phase() const noexcept { return _phase; }

    Mood mood() const noexcept
    {
        switch (_phase) {
            case Phase::Notice: return Mood::Curious;
            case Phase::Respond:
            case Phase::Wait: return _affectionate ? Mood::Affection : Mood::Happy;
            case Phase::Settle: return Mood::Relief;
            default: return Mood::Idle;
        }
    }

    EyeCue eye_cue(std::uint32_t now_ms) const noexcept
    {
        EyeCue cue;
        if (!_active) return cue;
        const auto age = now_ms - _start_ms;
        if (age >= kDurationMs) return cue;
        // This eye track shares the head's intent, with the first glance ahead
        // of its physical movement. Ordinary random scanning is suppressed by
        // the caller while directed is true; the emotion track still animates.
        constexpr std::array<std::uint32_t, 8> times{{0, 120, 480, 760, 1100, 1450, 2050, kDurationMs}};
        constexpr std::array<float, 8> xs{{0, 7, 6, 3, 5, 3, 3, 0}};
        constexpr std::array<float, 8> ys{{0, -3, -2, 3, -2, 0, 0, 0}};
        std::size_t i = 1;
        while (i + 1u < times.size() && age > times[i]) ++i;
        const auto t = ease(static_cast<float>(age - times[i - 1]) / static_cast<float>(times[i] - times[i - 1]));
        cue.x = _direction * mix(xs[i - 1], xs[i], t);
        cue.y = mix(ys[i - 1], ys[i], t);
        cue.directed = true;
        return cue;
    }

private:
    struct Pose { int yaw = 0; int pitch = 0; };
    struct Window { int yaw_low; int yaw_high; int pitch_low; int pitch_high; };
    static constexpr std::uint32_t kGazeLeadMs = 120;

    static float ease(float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * t * (10.0f + t * (-15.0f + 6.0f * t));
    }
    static float mix(float a, float b, float t) noexcept { return a + (b - a) * t; }
    static int approach(int from, int goal, int step) noexcept
    {
        return static_cast<int>(std::clamp<std::int64_t>(goal, static_cast<std::int64_t>(from) - step,
                                                        static_cast<std::int64_t>(from) + step));
    }
    static int offset(int base, int delta) noexcept
    {
        return static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(base) + delta,
                                                        std::numeric_limits<int>::min(),
                                                        std::numeric_limits<int>::max()));
    }
    static int blend_angle(int from, int to, float t) noexcept
    {
        // Preserve defined behaviour even for a malformed/extreme runtime
        // angle; float alone loses enough precision to round INT_MAX upward.
        const auto value = std::llround(static_cast<double>(from) +
                                        (static_cast<double>(to) - from) * t);
        return static_cast<int>(std::clamp<std::int64_t>(value, std::numeric_limits<int>::min(),
                                                        std::numeric_limits<int>::max()));
    }
    static bool contains(const Window& window, Pose pose) noexcept
    {
        return pose.yaw >= window.yaw_low && pose.yaw <= window.yaw_high &&
               pose.pitch >= window.pitch_low && pose.pitch <= window.pitch_high;
    }
    static Phase phase_at(std::uint32_t age) noexcept
    {
        if (age < 320) return Phase::Notice;
        if (age < 1450) return Phase::Respond;
        if (age < 2050) return Phase::Wait;
        return Phase::Settle;
    }

    Pose sampled_head(std::uint32_t age) const noexcept
    {
        const int yaw = _direction;
        const std::array<Pose, 8> poses{{
            _from, _from,
            {_anchor.yaw + yaw * (_affectionate ? 45 : 55), offset(_anchor.pitch, -(_affectionate ? 18 : 12))},
            {_anchor.yaw + yaw * (_affectionate ? 20 : 40), offset(_anchor.pitch, _affectionate ? 28 : 24)},
            {_anchor.yaw + yaw * (_affectionate ? 35 : 25), offset(_anchor.pitch, -(_affectionate ? 24 : 18))},
            {_anchor.yaw + yaw * (_affectionate ? 14 : 18), offset(_anchor.pitch, 2)},
            {_anchor.yaw + yaw * (_affectionate ? 14 : 18), offset(_anchor.pitch, 2)},
            {_anchor.yaw + yaw * 8, _anchor.pitch}}};
        constexpr std::array<std::uint32_t, 8> times{{0, 120, 480, 760, 1100, 1450, 2050, kDurationMs}};
        age = std::min(age, kDurationMs);
        std::size_t i = 1;
        while (i + 1u < times.size() && age > times[i]) ++i;
        const auto t = ease(static_cast<float>(age - times[i - 1]) / static_cast<float>(times[i] - times[i - 1]));
        return {blend_angle(poses[i - 1].yaw, poses[i].yaw, t),
                blend_angle(poses[i - 1].pitch, poses[i].pitch, t)};
    }

    template <typename Motion>
    static bool limits(Motion& motion, Pose anchor, Window& window)
    {
        const auto yaw = motion.yawServo().getAngleLimit();
        const auto pitch = motion.pitchServo().getAngleLimit();
        if (yaw.x >= yaw.y || pitch.x >= pitch.y) return false;
        window.yaw_low = std::max(yaw.x, -80);
        window.yaw_high = std::min(yaw.y, 80);
        window.pitch_low = static_cast<int>(std::max<std::int64_t>(pitch.x, static_cast<std::int64_t>(anchor.pitch) - 40));
        window.pitch_high = static_cast<int>(std::min<std::int64_t>(pitch.y, static_cast<std::int64_t>(anchor.pitch) + 40));
        return window.yaw_low < window.yaw_high && window.pitch_low < window.pitch_high;
    }

    template <typename Motion>
    bool check_ownership(Motion& motion)
    {
        if (_owns_lock && motion.isModifyLocked()) return true;
        // The shared lock has no owner token. Once observed cleared, abandon
        // this session without stopping a possible new owner's hardware.
        _owns_lock = false;
        _active = false;
        _phase = Phase::Idle;
        _has_history = false;
        return false;
    }

    template <typename Motion>
    void finish(Motion& motion)
    {
        if (_owns_lock) {
            if (motion.isModifyLocked()) {
                if (_motion_sent) motion.stop();
                motion.setModifyLock(false);
            } else {
                _has_history = false;
            }
        }
        _owns_lock = false;
        _active = false;
        _phase = Phase::Idle;
        _motion_sent = false;
    }

    bool _active = false;
    bool _owns_lock = false;
    bool _motion_sent = false;
    bool _has_history = false;
    bool _affectionate = false;
    int _direction = -1;
    std::uint32_t _start_ms = 0;
    std::uint32_t _last_touch_ms = 0;
    std::uint32_t _last_command_ms = 0;
    std::uint32_t _generation = 0;
    Phase _phase = Phase::Idle;
    Pose _anchor;
    Pose _from;
    Pose _last_goal;
};

} // namespace distance_pet
