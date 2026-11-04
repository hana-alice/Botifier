#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace distance_pet {

// A low-priority reminder: one gentle left/right sway, then a centred pause.
// The caller cancels before admitting touch, NFC, eye-rest or navigation.
class AlertHeadMotion {
public:
    static constexpr std::uint32_t kCommandIntervalMs = 100;
    static constexpr std::uint32_t kCycleMs = 3600;

    template <typename Motion>
    void tick(Motion& motion, std::uint32_t now_ms, bool enabled)
    {
        if (!enabled) { cancel(motion); return; }
        if (_blocked) return;
        if (_active && !motion.isModifyLocked()) {
            // A boolean lock has no owner token. Once observed lost, never
            // stop or reclaim it until the caller resets this interval.
            _active = _owns_lock = _sent = false;
            _blocked = true;
            return;
        }
        if (!_active) {
            if (motion.isModifyLocked() || motion.isMoving()) return;
            const auto actual = motion.getCurrentAngles();
            if (!limits(motion, _window) || actual.x < _window.low || actual.x > _window.high ||
                actual.y < _window.pitch_low || actual.y > _window.pitch_high) {
                _blocked = true;
                return;
            }
            _from = _last_goal = actual.x;
            _pitch = actual.y;
            _start_ms = now_ms;
            _sampled = _sent = false;
            motion.setModifyLock(true);
            _active = _owns_lock = true;
        }

        Window current;
        if (!limits(motion, current) || current.low > _window.low || current.high < _window.high ||
            current.pitch_low > _window.pitch_low || current.pitch_high < _window.pitch_high) {
            finish(motion);
            _blocked = true;
            return;
        }
        if (_sampled && now_ms - _last_sample_ms < kCommandIntervalMs) return;
        _sampled = true;
        _last_sample_ms = now_ms;
        // Sample the current curve once after a missed frame, with no queue
        // of old waypoints and no repeated hardware writes during the pause.
        const auto age = now_ms - _start_ms;
        const int target = sampled_yaw(age);
        const int goal = std::clamp(target, _last_goal - 18, _last_goal + 18);
        if (goal == _last_goal) return;
        motion.setTorqueEnabled(true);
        motion.moveWithDuration(goal, _pitch, static_cast<std::uint16_t>(kCommandIntervalMs));
        _last_goal = goal;
        _sent = true;
    }

    template <typename Motion>
    void cancel(Motion& motion)
    {
        finish(motion);
        _blocked = false;
    }

    bool active() const noexcept { return _active; }
    bool owns_lock() const noexcept { return _owns_lock; }

private:
    struct Window { int low; int high; int pitch_low; int pitch_high; };

    static float ease(float t) noexcept
    {
        return t * t * t * (10.0f + t * (-15.0f + 6.0f * t));
    }
    static int blend(int from, int to, std::uint32_t elapsed) noexcept
    {
        return static_cast<int>(std::lround(from + (to - from) * ease(elapsed / 600.0f)));
    }
    int sampled_yaw(std::uint32_t age) const noexcept
    {
        // Add lookahead after reducing elapsed time so uint32 clock wrap
        // cannot overflow the sample into a different phase.
        if (age < 600) return blend(_from, 0, std::min<std::uint32_t>(age + kCommandIntervalMs, 600u));
        const auto phase = ((age - 600u) % kCycleMs + kCommandIntervalMs) % kCycleMs;
        const int left = std::max(_window.low, -60);
        const int right = std::min(_window.high, 60);
        if (phase < 600) return blend(0, left, phase);
        if (phase < 1200) return blend(left, 0, phase - 600);
        if (phase < 1800) return blend(0, right, phase - 1200);
        if (phase < 2400) return blend(right, 0, phase - 1800);
        return 0;
    }
    template <typename Motion>
    static bool limits(Motion& motion, Window& window)
    {
        const auto yaw = motion.yawServo().getAngleLimit();
        const auto pitch = motion.pitchServo().getAngleLimit();
        if (yaw.x >= yaw.y || pitch.x >= pitch.y) return false;
        window = {std::max(yaw.x, -80), std::min(yaw.y, 80), pitch.x, pitch.y};
        return window.low < 0 && window.high > 0;
    }
    template <typename Motion>
    void finish(Motion& motion)
    {
        if (_owns_lock && motion.isModifyLocked()) {
            if (_sent) motion.stop();
            motion.setModifyLock(false);
        }
        _active = _owns_lock = _sent = _sampled = false;
    }

    bool _active = false;
    bool _owns_lock = false;
    bool _sent = false;
    bool _sampled = false;
    bool _blocked = false;
    int _from = 0;
    int _last_goal = 0;
    int _pitch = 0;
    Window _window{};
    std::uint32_t _start_ms = 0;
    std::uint32_t _last_sample_ms = 0;
};

} // namespace distance_pet
