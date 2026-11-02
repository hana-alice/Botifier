#pragma once

#include "types.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace idle_program {

struct WeeklyUpdate {
    bool available = false;
    std::uint32_t sequence = 0;
    int remaining_percent = -1;
    std::uint32_t reset_at = 0;
    std::uint32_t observed_at = 0;
    std::uint32_t ttl_ms = 0;
};

inline bool valid_config(const Config& value) noexcept
{
    constexpr std::uint32_t maximum = 24u * 60u * 60u * 1000u;
    return value.eye_interval_ms >= 1000 && value.eye_interval_ms <= maximum &&
           value.rest_duration_ms >= 1000 && value.rest_duration_ms <= maximum &&
           value.idle_grace_ms <= maximum && value.pet_gap_ms <= maximum &&
           value.weekly_duration_ms >= 1000 && value.weekly_duration_ms <= maximum &&
           value.weekly_interval_ms >= 1000 && value.weekly_interval_ms <= maximum &&
           value.weekly_duration_ms <= value.weekly_interval_ms;
}

inline bool valid_weekly(const WeeklyUpdate& value) noexcept
{
    if (!value.available) return true;
    return value.remaining_percent >= 0 && value.remaining_percent <= 100 &&
           value.observed_at != 0 && value.reset_at > value.observed_at &&
           static_cast<std::uint64_t>(value.reset_at) - value.observed_at <= 8u * 24u * 60u * 60u &&
           value.ttl_ms >= 60000 && value.ttl_ms <= 3600000;
}

// Only supplied monotonic times advance this policy. Snapshot reads never
// complete a rest or create a programme. The service serializes all mutations.
class Policy {
public:
    void start(std::uint32_t now_ms) noexcept
    {
        if (active_) return;
        active_ = true;
        clock_ms_ = eye_since_ms_ = activity_ms_ = pet_since_ms_ = weekly_since_ms_ = now_ms;
    }

    Config config() const noexcept { return config_; }

    bool set_config(const Config& value, std::uint32_t now_ms) noexcept
    {
        if (!valid_config(value) || !advance_clock(now_ms)) return false;
        mark_due(now_ms);
        if (value.eye_interval_ms != config_.eye_interval_ms && !eye_due_) eye_since_ms_ = now_ms;
        config_ = value;
        activity_ms_ = now_ms;
        change_program(Program::Pet, now_ms);
        return true;
    }

    void set_context(const Context& value, std::uint32_t now_ms) noexcept
    {
        if (!advance_clock(now_ms)) return;
        const bool was_blocked = blocked(now_ms);
        context_ = value;
        context_ms_ = now_ms;
        have_context_ = true;
        if (was_blocked || blocked(now_ms)) {
            activity_ms_ = now_ms;
            change_program(Program::Pet, now_ms);
        }
    }

    void note_activity(std::uint32_t now_ms) noexcept
    {
        if (!advance_clock(now_ms)) return;
        mark_due(now_ms);
        activity_ms_ = now_ms;
        change_program(Program::Pet, now_ms);
    }

    bool update_weekly(const WeeklyUpdate& value, std::uint32_t now_ms, std::uint32_t unix_seconds = 0) noexcept
    {
        if (!valid_weekly(value) || !advance_clock(now_ms, unix_seconds)) return false;
        if (have_sequence_) {
            const auto delta = value.sequence - sequence_;
            if (delta == 0 || delta > 0x7fffffffu) return false;
        }
        if (value.available && have_observed_ && value.observed_at < weekly_.observed_at) return false;
        have_sequence_ = true;
        sequence_ = value.sequence;
        if (!value.available) {
            weekly_status_ = WeeklyStatus::Unavailable;
            if (program_ == Program::Weekly) change_program(Program::Pet, now_ms);
            return true;
        }
        const auto observed_ms = static_cast<std::uint64_t>(value.observed_at) * 1000u;
        const auto expiry = std::min(observed_ms + value.ttl_ms,
                                     static_cast<std::uint64_t>(value.reset_at) * 1000u);
        const bool same_observed = have_observed_ && value.observed_at == weekly_.observed_at;
        weekly_expiry_ms_ = same_observed ? std::min(weekly_expiry_ms_, expiry) : expiry;
        wall_ms_ = std::max(wall_ms_, observed_ms);
        have_wall_ = true;
        weekly_ = value;
        have_observed_ = true;
        weekly_status_ = wall_ms_ < weekly_expiry_ms_ ? WeeklyStatus::Fresh : WeeklyStatus::Stale;
        if (weekly_status_ != WeeklyStatus::Fresh && program_ == Program::Weekly) change_program(Program::Pet, now_ms);
        return true;
    }

    void tick(std::uint32_t now_ms, std::uint32_t unix_seconds = 0) noexcept
    {
        if (!active_ || !advance_clock(now_ms, unix_seconds)) return;
        mark_due(now_ms);
        refresh_weekly();
        if (blocked(now_ms)) {
            activity_ms_ = now_ms;
            change_program(Program::Pet, now_ms);
            return;
        }
        if (program_ == Program::EyeRest && now_ms - program_since_ms_ >= config_.rest_duration_ms) {
            ++completed_rests_;
            eye_due_ = false;
            eye_since_ms_ = now_ms;
            change_program(Program::RestDone, now_ms);
            return;
        }
        if (program_ == Program::RestDone) {
            if (now_ms - program_since_ms_ >= 2000u) change_program(Program::Pet, now_ms);
            return;
        }
        if (program_ == Program::EyeRest) return;
        const bool idle = now_ms - activity_ms_ >= config_.idle_grace_ms;
        // Eye rest can replace an ordinary weekly card and bypass the pet gap.
        if (eye_due_ && idle) {
            change_program(Program::EyeRest, now_ms);
            return;
        }
        if (program_ == Program::Weekly) {
            if (now_ms - program_since_ms_ >= config_.weekly_duration_ms || !weekly_displayable()) {
                change_program(Program::Pet, now_ms);
            }
            return;
        }
        if (idle && weekly_displayable() && now_ms - weekly_since_ms_ >= config_.weekly_interval_ms &&
            now_ms - pet_since_ms_ >= config_.pet_gap_ms) {
            weekly_since_ms_ = now_ms;
            change_program(Program::Weekly, now_ms);
        }
    }

    Snapshot snapshot(std::uint32_t now_ms, std::uint32_t unix_seconds = 0) const noexcept
    {
        Policy copy = *this;
        const bool chronological = copy.advance_clock(now_ms, unix_seconds);
        copy.refresh_weekly();
        Snapshot result;
        result.active = active_;
        result.config = config_;
        result.blocked = !chronological || copy.blocked(now_ms);
        result.program = result.blocked ? Program::Pet : program_;
        result.generation = generation_;
        result.completed_rests = completed_rests_;
        result.eye_remaining_ms = eye_due_ ? 0u : remaining(config_.eye_interval_ms, now_ms - eye_since_ms_);
        if (result.program == Program::EyeRest) result.remaining_ms = remaining(config_.rest_duration_ms, now_ms - program_since_ms_);
        else if (result.program == Program::Weekly) result.remaining_ms = remaining(config_.weekly_duration_ms, now_ms - program_since_ms_);
        else if (result.program == Program::RestDone) result.remaining_ms = remaining(2000u, now_ms - program_since_ms_);
        result.weekly_status = copy.weekly_status_;
        result.weekly_left = copy.weekly_status_ == WeeklyStatus::Fresh ? weekly_.remaining_percent : -1;
        result.weekly_reset_at = have_observed_ ? weekly_.reset_at : 0u;
        result.weekly_observed_at = have_observed_ ? weekly_.observed_at : 0u;
        if (have_observed_) {
            const auto age = copy.wall_ms_ - static_cast<std::uint64_t>(weekly_.observed_at) * 1000u;
            result.weekly_age_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(age, std::numeric_limits<std::uint32_t>::max()));
        }
        return result;
    }

private:
    static std::uint32_t remaining(std::uint32_t duration, std::uint32_t elapsed) noexcept
    {
        return elapsed >= duration ? 0u : duration - elapsed;
    }

    bool advance_clock(std::uint32_t now_ms, std::uint32_t unix_seconds = 0) noexcept
    {
        if (!active_) { clock_ms_ = now_ms; }
        const auto elapsed = now_ms - clock_ms_;
        if (elapsed > 0x7fffffffu) return false;
        if (have_wall_) wall_ms_ += elapsed;
        clock_ms_ = now_ms;
        if (unix_seconds) {
            wall_ms_ = std::max(wall_ms_, static_cast<std::uint64_t>(unix_seconds) * 1000u);
            have_wall_ = true;
        }
        return true;
    }

    bool blocked(std::uint32_t now_ms) const noexcept
    {
        return !active_ || !config_.enabled || !have_context_ || now_ms - context_ms_ >= 500u ||
               !context_.surface_available || context_.foreground_busy || context_.distance_alert;
    }

    void mark_due(std::uint32_t now_ms) noexcept
    {
        if (active_ && now_ms - eye_since_ms_ >= config_.eye_interval_ms) eye_due_ = true;
    }

    bool weekly_displayable() const noexcept
    {
        return weekly_status_ == WeeklyStatus::Fresh || weekly_status_ == WeeklyStatus::Stale ||
               weekly_status_ == WeeklyStatus::Unavailable;
    }

    void refresh_weekly() noexcept
    {
        if (weekly_status_ == WeeklyStatus::Fresh && wall_ms_ >= weekly_expiry_ms_) weekly_status_ = WeeklyStatus::Stale;
    }

    void change_program(Program value, std::uint32_t now_ms) noexcept
    {
        if (program_ == value) return;
        program_ = value;
        program_since_ms_ = now_ms;
        if (value == Program::Pet) pet_since_ms_ = now_ms;
        ++generation_;
    }

    Config config_;
    Context context_;
    WeeklyUpdate weekly_;
    Program program_ = Program::Pet;
    WeeklyStatus weekly_status_ = WeeklyStatus::Waiting;
    bool active_ = false;
    bool have_context_ = false;
    bool eye_due_ = false;
    bool have_sequence_ = false;
    bool have_observed_ = false;
    bool have_wall_ = false;
    std::uint32_t clock_ms_ = 0, context_ms_ = 0, eye_since_ms_ = 0, activity_ms_ = 0;
    std::uint32_t program_since_ms_ = 0, pet_since_ms_ = 0, weekly_since_ms_ = 0;
    std::uint32_t generation_ = 0, completed_rests_ = 0, sequence_ = 0;
    std::uint64_t wall_ms_ = 0, weekly_expiry_ms_ = 0;
};

} // namespace idle_program
