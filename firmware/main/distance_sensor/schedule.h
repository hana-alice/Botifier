#pragma once

#include <cstdint>

namespace distance_sensor {

enum class ScheduleMode { WaitingClock, Off, Automatic, Manual, ManualOff };

struct ScheduleStatus {
    ScheduleMode mode = ScheduleMode::WaitingClock;
    bool clock_available = false;
    int local_weekday = -1; // Sunday = 0; fixed Beijing time, independent of UI TZ.
    int local_minute = -1;
    std::uint32_t manual_remaining_ms = 0;
    std::uint32_t generation = 0; // Invalidates measurements across on/off edges.

    bool enabled() const noexcept
    {
        return mode == ScheduleMode::Automatic || mode == ScheduleMode::Manual;
    }
};

inline const char* schedule_name(ScheduleMode mode) noexcept
{
    switch (mode) {
        case ScheduleMode::Automatic: return "automatic";
        case ScheduleMode::Manual: return "manual";
        case ScheduleMode::ManualOff: return "manual_off";
        case ScheduleMode::Off: return "off";
        default: return "waiting_clock";
    }
}

// One owner or external synchronization. No I/O, allocation, TZ mutation or NVS.
// Weekdays mean Monday-Friday, without a holiday/compensatory-workday calendar.
class SchedulePolicy {
public:
    ScheduleStatus update(std::int64_t unix_seconds, std::uint64_t uptime_ms) noexcept
    {
        ScheduleStatus result;
        result.clock_available = unix_seconds >= 1577836800 && unix_seconds <= UINT32_MAX;
        std::int64_t day = -1;
        bool in_window = false;
        if (result.clock_available) {
            const auto local = unix_seconds + 8 * 60 * 60;
            day = local / 86400;
            result.local_weekday = static_cast<int>((day + 4) % 7);
            result.local_minute = static_cast<int>(local % 86400 / 60);
            in_window = result.local_minute >= 9 * 60 + 30 && result.local_minute < 21 * 60 + 30;
            if (card_until_ms_ > uptime_ms && card_until_local_ == next_boundary(local)) {
                result.mode = card_mode_;
                if (result.enabled()) {
                    const auto wall_remaining_ms = static_cast<std::uint64_t>(card_until_local_ - local) * 1000;
                    const auto remaining = card_until_ms_ - uptime_ms;
                    result.manual_remaining_ms = static_cast<std::uint32_t>(
                        remaining < wall_remaining_ms ? remaining : wall_remaining_ms);
                }
                return publish(result);
            }
        }
        card_until_ms_ = 0;
        if (result.local_weekday >= 1 && result.local_weekday <= 5 && in_window) {
            manual_until_ms_ = 0;
            result.mode = ScheduleMode::Automatic;
            return publish(result);
        }
        if (in_window && manual_until_ms_ > uptime_ms && manual_day_ == day) {
            result.mode = ScheduleMode::Manual;
            const auto wall_remaining_ms = static_cast<std::uint64_t>(
                (21 * 60 + 30) * 60 - (unix_seconds + 8 * 60 * 60) % 86400) * 1000;
            const auto remaining = manual_until_ms_ - uptime_ms;
            result.manual_remaining_ms = static_cast<std::uint32_t>(
                remaining < wall_remaining_ms ? remaining : wall_remaining_ms);
            return publish(result);
        }
        manual_until_ms_ = 0;
        result.mode = result.clock_available ? ScheduleMode::Off : ScheduleMode::WaitingClock;
        return publish(result);
    }

    void touch(std::int64_t unix_seconds, std::uint64_t uptime_ms) noexcept
    {
        const auto current = update(unix_seconds, uptime_ms);
        // A touch only opens the current day's 09:30-21:30 window; it never
        // schedules a future window, overrides its end, or bypasses no clock.
        if (!current.clock_available || card_until_ms_ != 0 || current.mode == ScheduleMode::Automatic ||
            current.local_minute < 9 * 60 + 30 || current.local_minute >= 21 * 60 + 30) return;
        const auto local = unix_seconds + 8 * 60 * 60;
        manual_day_ = local / 86400;
        const auto seconds = (21 * 60 + 30) * 60 - local % 86400;
        manual_until_ms_ = uptime_ms + static_cast<std::uint64_t>(seconds) * 1000;
    }

    ScheduleStatus toggle(std::int64_t unix_seconds, std::uint64_t uptime_ms) noexcept
    {
        // Publish any expired edge before a fresh card action can reopen it.
        const auto current = update(unix_seconds, uptime_ms);
        if (!current.clock_available) return current;
        manual_until_ms_ = 0;
        card_mode_ = current.enabled() ? ScheduleMode::ManualOff : ScheduleMode::Manual;
        if (card_until_ms_ == 0) {
            const auto local = unix_seconds + 8 * 60 * 60;
            card_until_local_ = next_boundary(local);
            card_until_ms_ = uptime_ms + static_cast<std::uint64_t>(card_until_local_ - local) * 1000;
        }
        // Repeated cards in this period retain the original monotonic deadline,
        // including after a backward wall-clock correction.
        return update(unix_seconds, uptime_ms);
    }

private:
    static std::int64_t next_boundary(std::int64_t local) noexcept
    {
        const auto second = local % 86400;
        const auto day_start = local - second;
        if (second < (9 * 60 + 30) * 60) return day_start + (9 * 60 + 30) * 60;
        if (second < (21 * 60 + 30) * 60) return day_start + (21 * 60 + 30) * 60;
        return day_start + 86400 + (9 * 60 + 30) * 60;
    }

    ScheduleStatus publish(ScheduleStatus result) noexcept
    {
        if (enabled_ != result.enabled()) ++generation_;
        enabled_ = result.enabled();
        result.generation = generation_;
        return result;
    }

    std::uint64_t manual_until_ms_ = 0;
    std::int64_t manual_day_ = -1;
    std::uint64_t card_until_ms_ = 0;
    std::int64_t card_until_local_ = -1;
    ScheduleMode card_mode_ = ScheduleMode::Manual;
    std::uint32_t generation_ = 0;
    bool enabled_ = false;
};

} // namespace distance_sensor
