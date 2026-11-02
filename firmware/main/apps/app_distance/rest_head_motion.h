#pragma once

#include <apps/common/idle_program/eye_sequence.h>
#include <algorithm>
#include <cstdint>

namespace distance_pet {

// One eye-break session owns this controller. The caller cancels it before
// replacing it for the next session; a busy/locked first attempt is not retried.
class RestHeadMotion {
public:
    // Reserve during the character intro so cooperating idle modifiers cannot
    // start a fresh motion just before the head choreography begins. Any motion
    // already in progress keeps running; its final angles are captured by tick.
    template <typename Motion>
    void reserve(Motion& motion)
    {
        if (_cancelled || _skipped || _ownership_lost || _limits_unavailable) return;
        if (_owns_lock) {
            check_ownership(motion);
            return;
        }
        if (_attempted) return;
        _attempted = true;
        if (motion.isModifyLocked()) {
            _skipped = true;
            return;
        }
        motion.setModifyLock(true);
        _owns_lock = true;
    }

    template <typename Motion>
    void tick(Motion& motion, std::uint32_t elapsed_ms)
    {
        if (_cancelled || _skipped || _ownership_lost || _limits_unavailable) return;
        if (_owns_lock && !check_ownership(motion)) return;
        if (!_started) {
            constexpr auto latest_start = idle_program::kMotionBeats[1].start_ms -
                                          idle_program::kMotionBeats.front().move_ms;
            // The complete preparation must finish before the first excursion.
            // A late first UI tick cannot bypass or truncate that movement.
            if (elapsed_ms > latest_start) {
                _skipped = true;
                release(motion);
                return;
            }
            if (!_owns_lock) {
                if (_attempted) return;
                _attempted = true;
                if (motion.isModifyLocked() || motion.isMoving()) {
                    _skipped = true;
                    return;
                }
            } else if (motion.isMoving()) {
                // The intro did not give the previous movement enough time to
                // finish. Drop our reservation without stopping that movement.
                _skipped = true;
                release(motion);
                return;
            }
            const auto current = motion.getCurrentAngles();
            // The first timed goal faces the calibrated home. Do not attempt
            // a large repositioning from an arbitrary previous pose.
            if (current.x < -300 || current.x > 300) {
                _skipped = true;
                release(motion);
                return;
            }
            _base_pitch = current.y;
        }

        const bool finished = elapsed_ms >= idle_program::kMotionDurationMs;
        const auto beat = finished ? idle_program::kMotionBeats.size() - 1u
                                   : idle_program::motion_index(elapsed_ms);
        if (_last_beat >= static_cast<int>(beat)) return;
        // An expired phase still sends its final centre target if the frame at
        // its scheduled frame was missed. Earlier beats are never replayed.
        int yaw = 0, pitch = 0;
        if (!targets(motion, idle_program::motion_beat(elapsed_ms), yaw, pitch)) {
            if (!_started) _skipped = true;
            _limits_unavailable = true;
            release(motion);
            return;
        }
        if (!_owns_lock) {
            motion.setModifyLock(true);
            _owns_lock = true;
        }
        // The generic servo releases torque at rest. A zero/centre beat can
        // finish before the next beat, so every fresh target must re-enable it.
        // Reservation and duplicate ticks deliberately leave torque untouched.
        motion.setTorqueEnabled(true);
        motion.moveWithDuration(yaw, pitch, idle_program::kMotionBeats[beat].move_ms);
        _target_yaw = yaw;
        _target_pitch = pitch;
        _started = true;
        _last_beat = static_cast<int>(beat);
        // Retain ownership through REST 20; the caller releases it on completion
        // or explicit exit, so ordinary idle movement cannot restart meanwhile.
    }

    template <typename Motion>
    void cancel(Motion& motion)
    {
        _cancelled = true;
        release(motion);
    }

    bool started() const noexcept { return _started; }
    bool skipped() const noexcept { return _skipped; }
    bool owns_lock() const noexcept { return _owns_lock; }
    bool ownership_lost() const noexcept { return _ownership_lost; }
    bool limits_unavailable() const noexcept { return _limits_unavailable; }
    int last_beat() const noexcept { return _last_beat; }
    int target_yaw() const noexcept { return _target_yaw; }
    int target_pitch() const noexcept { return _target_pitch; }

private:
    template <typename Motion>
    bool check_ownership(Motion& motion)
    {
        if (motion.isModifyLocked()) return true;
        // The shared bool lock has no owner token. Once cleared externally,
        // do not stop another owner's motion or try to acquire it again.
        _owns_lock = false;
        _ownership_lost = true;
        return false;
    }

    template <typename Motion>
    bool targets(Motion& motion, idle_program::MotionBeat offset, int& yaw, int& pitch) const
    {
        const auto yaw_limit = motion.yawServo().getAngleLimit();
        const auto pitch_limit = motion.pitchServo().getAngleLimit();
        return home_yaw_target(offset.yaw_delta, yaw_limit.x, yaw_limit.y, yaw) &&
               bounded_target(_base_pitch, offset.pitch_delta, 40, pitch_limit.x, pitch_limit.y, pitch);
    }

    static bool home_yaw_target(int offset, int minimum, int maximum, int& result)
    {
        const auto low = std::max(minimum, -80);
        const auto high = std::min(maximum, 80);
        // A one-sided range cannot perform left and right around home. Do not
        // manufacture another off-centre neutral from its midpoint.
        if (low >= 0 || high <= 0) return false;
        result = std::clamp(offset, low, high);
        return true;
    }

    static bool bounded_target(int base, int offset, int radius, int minimum, int maximum, int& result)
    {
        // A runtime limit may tighten after stall protection. Intersect it with
        // the small relative motion window, rather than moving farther to reach
        // a newly shifted limit. int64 arithmetic also avoids signed overflow.
        const auto low = std::max<std::int64_t>(minimum, static_cast<std::int64_t>(base) - radius);
        const auto high = std::min<std::int64_t>(maximum, static_cast<std::int64_t>(base) + radius);
        if (low > high) return false;
        // When already beside a physical stop, use the permitted window's centre
        // so both directions remain visible. Neither direction can leave the
        // original actual-angle +/- radius envelope, even after limits tighten.
        const auto centre = low + (high - low) / 2;
        const auto extent = offset < 0 ? centre - low : high - centre;
        result = static_cast<int>(std::clamp<std::int64_t>(
            centre + static_cast<std::int64_t>(offset) * extent / radius, low, high));
        return true;
    }

    template <typename Motion>
    void release(Motion& motion)
    {
        if (!_owns_lock) return;
        if (motion.isModifyLocked()) {
            if (_started) motion.stop();
            motion.setModifyLock(false);
        } else {
            _ownership_lost = true;
        }
        _owns_lock = false;
    }

    bool _attempted = false;
    bool _started = false;
    bool _skipped = false;
    bool _owns_lock = false;
    bool _ownership_lost = false;
    bool _limits_unavailable = false;
    bool _cancelled = false;
    int _base_pitch = 0;
    int _last_beat = -1;
    int _target_yaw = 0;
    int _target_pitch = 0;
};

} // namespace distance_pet
