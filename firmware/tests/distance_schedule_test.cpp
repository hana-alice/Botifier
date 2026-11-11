#include <distance_sensor/schedule.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using distance_sensor::ScheduleMode;
using distance_sensor::SchedulePolicy;
using distance_sensor::ScheduleStatus;

namespace {

// Independently fixed fixture: 2026-11-02 00:00:00 +08:00 is a Monday.
constexpr std::int64_t monday_midnight_utc = 1793548800;
constexpr std::uint64_t hour_ms = 3600000;
constexpr std::int64_t beijing(int day, int hour, int minute = 0, int second = 0)
{
    return monday_midnight_utc + day * 86400 + hour * 3600 + minute * 60 + second;
}
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void check_mode(const ScheduleStatus& status, ScheduleMode mode)
{
    check(status.mode == mode, "unexpected schedule mode");
    check(status.enabled() == (mode == ScheduleMode::Automatic || mode == ScheduleMode::Manual),
          "enabled state disagrees with schedule mode");
    if (mode != ScheduleMode::Manual) check(status.manual_remaining_ms == 0, "inactive lease has remaining time");
}

void weekday_window_has_exact_start_and_end()
{
    for (int day = 0; day != 5; ++day) {
        SchedulePolicy policy;
        check_mode(policy.update(beijing(day, 9, 29, 59), 0), ScheduleMode::Off);
        const auto opening = policy.update(beijing(day, 9, 30), 1000);
        check_mode(opening, ScheduleMode::Automatic);
        check(opening.clock_available && opening.local_weekday == day + 1 && opening.local_minute == 570,
              "weekday opening does not use Beijing calendar");
        check_mode(policy.update(beijing(day, 21, 29, 59), 43200000), ScheduleMode::Automatic);
        check_mode(policy.update(beijing(day, 21, 30), 43201000), ScheduleMode::Off);
    }
}
void weekends_have_no_automatic_window()
{
    for (int day = 5; day != 7; ++day) {
        SchedulePolicy policy;
        for (const int hour : {0, 9, 12, 21, 23}) {
            const auto status = policy.update(beijing(day, hour, 30), 0);
            check_mode(status, ScheduleMode::Off);
            check(status.local_weekday == (day + 1) % 7, "weekend day is incorrect");
        }
    }
}
void utc_date_boundary_uses_the_next_beijing_day()
{
    SchedulePolicy policy;
    // Sunday 2026-11-08 15:59:59 UTC -> Sunday 23:59:59 Beijing.
    auto status = policy.update(1794153599, 0);
    check(status.local_weekday == 0 && status.local_minute == 1439, "wrong Beijing Sunday");
    status = policy.update(1794153600, 1000);
    check(status.local_weekday == 1 && status.local_minute == 0, "UTC 16:00 did not cross Beijing midnight");
    check_mode(status, ScheduleMode::Off);
    check_mode(policy.update(beijing(7, 9, 30), 34201000), ScheduleMode::Automatic);
}
void invalid_clock_never_opens_or_queues_a_manual_window()
{
    for (const std::int64_t invalid : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{1577836799},
             std::int64_t{UINT32_MAX} + 1, std::numeric_limits<std::int64_t>::max()}) {
        SchedulePolicy policy;
        policy.touch(invalid, 1000);
        const auto status = policy.update(invalid, 1000);
        check_mode(status, ScheduleMode::WaitingClock);
        check(!status.clock_available && status.local_weekday == -1 && status.local_minute == -1,
              "invalid clock invented a local calendar");
        check_mode(policy.update(beijing(5, 12), 2000), ScheduleMode::Off);
    }
    SchedulePolicy policy;
    check(policy.update(1577836800, 0).clock_available, "minimum valid clock rejected");
    check(policy.update(UINT32_MAX, 0).clock_available, "maximum valid clock rejected");
    check_mode(policy.update(beijing(0, 12), 1000), ScheduleMode::Automatic);
}
void weekend_touch_enables_only_until_2130()
{
    for (int day = 5; day != 7; ++day) {
        SchedulePolicy policy;
        policy.touch(beijing(day, 9, 30), 1000);
        auto status = policy.update(beijing(day, 9, 30), 1000);
        check_mode(status, ScheduleMode::Manual);
        check(status.manual_remaining_ms == 12 * hour_ms, "weekend touch did not end at 21:30");
        status = policy.update(beijing(day, 21, 29, 59), 12 * hour_ms);
        check_mode(status, ScheduleMode::Manual);
        check(status.manual_remaining_ms == 1000, "last second of manual window was lost");
        check_mode(policy.update(beijing(day, 21, 30), 1000 + 12 * hour_ms), ScheduleMode::Off);
        check_mode(policy.update(beijing(day, 23, 59, 59), 14 * hour_ms), ScheduleMode::Off);
    }
    SchedulePolicy late;
    late.touch(beijing(5, 21, 29, 59), 1000);
    check(late.update(beijing(5, 21, 29, 59), 1000).manual_remaining_ms == 1000,
          "late touch extended beyond the shared time window");
    check_mode(late.update(beijing(5, 21, 30), 2000), ScheduleMode::Off);
}
void outside_window_touches_do_not_enable_or_reserve_a_future_window()
{
    for (int day = 0; day != 7; ++day) {
        for (const int second : {0, 9 * 3600 + 29 * 60 + 59, 21 * 3600 + 30 * 60, 86399}) {
            SchedulePolicy policy;
            const auto epoch = beijing(day, 0) + second;
            policy.touch(epoch, 1000);
            check_mode(policy.update(epoch, 1000), ScheduleMode::Off);
            // A correction into the window must not awaken an earlier touch.
            check_mode(policy.update(beijing(day, 12), 2000),
                       day < 5 ? ScheduleMode::Automatic : ScheduleMode::Off);
        }
    }
    SchedulePolicy policy;
    policy.touch(beijing(5, 9, 29, 59), 1000);
    check_mode(policy.update(beijing(5, 9, 30), 2000), ScheduleMode::Off);
    policy.touch(beijing(5, 21, 30), 3000);
    check_mode(policy.update(beijing(6, 9, 30), 12 * hour_ms), ScheduleMode::Off);
}
void manual_touch_never_carries_into_another_day()
{
    SchedulePolicy policy;
    policy.touch(beijing(5, 12), 1000);
    check_mode(policy.update(beijing(5, 12), 1000), ScheduleMode::Manual);
    // Wall-clock jump while the original monotonic lease would still be live.
    check_mode(policy.update(beijing(6, 12), 2000), ScheduleMode::Off);
    check_mode(policy.update(beijing(5, 12), 3000), ScheduleMode::Off);
}
void automatic_window_touch_does_not_extend_the_evening()
{
    SchedulePolicy policy;
    policy.touch(beijing(4, 21, 29, 59), 1000);
    check_mode(policy.update(beijing(4, 21, 29, 59), 1000), ScheduleMode::Automatic);
    check_mode(policy.update(beijing(4, 21, 30), 2000), ScheduleMode::Off);
    policy.touch(beijing(4, 21, 30), 2000);
    check_mode(policy.update(beijing(4, 21, 30), 2000), ScheduleMode::Off);
}
void reboot_discards_manual_override()
{
    SchedulePolicy original;
    original.touch(beijing(6, 12), 5000);
    check_mode(original.update(beijing(6, 12), 5000), ScheduleMode::Manual);
    SchedulePolicy rebooted;
    check_mode(rebooted.update(beijing(6, 12), 0), ScheduleMode::Off);
    check_mode(rebooted.update(0, 1), ScheduleMode::WaitingClock);
    check_mode(rebooted.update(beijing(7, 10), 2), ScheduleMode::Automatic);
}
void usable_clock_keeps_schedule_working_without_network_input()
{
    SchedulePolicy policy;
    // No connection flag or repeated SNTP success is needed after clock sync.
    check_mode(policy.update(beijing(0, 9, 30), 0), ScheduleMode::Automatic);
    check_mode(policy.update(beijing(0, 10, 30), hour_ms), ScheduleMode::Automatic);
    check_mode(policy.update(beijing(0, 21, 30), 12 * hour_ms), ScheduleMode::Off);
}
void losing_clock_cancels_manual_instead_of_resuming_after_sync()
{
    SchedulePolicy policy;
    policy.touch(beijing(5, 12), 1000);
    check_mode(policy.update(beijing(5, 12), 1000), ScheduleMode::Manual);
    check_mode(policy.update(0, 2000), ScheduleMode::WaitingClock);
    check_mode(policy.update(beijing(5, 12), 3000), ScheduleMode::Off);
}
void backward_clock_correction_cannot_extend_the_original_lease()
{
    SchedulePolicy policy;
    policy.touch(beijing(5, 20, 30), 1000);
    const auto status = policy.update(beijing(5, 19, 30), 11000);
    check_mode(status, ScheduleMode::Manual);
    check(status.manual_remaining_ms == hour_ms - 10000, "backward clock correction extended the manual lease");
    check_mode(policy.update(beijing(5, 20, 30), 1000 + hour_ms), ScheduleMode::Off);
}
void correction_outside_window_immediately_cancels_manual()
{
    for (const auto corrected : {beijing(5, 9, 29, 59), beijing(5, 21, 30)}) {
        SchedulePolicy policy;
        policy.touch(beijing(5, 12), 1000);
        check_mode(policy.update(corrected, 2000), ScheduleMode::Off);
        check_mode(policy.update(beijing(5, 12), 3000), ScheduleMode::Off);
    }
}
void uptime_beyond_uint32_wrap_retains_the_correct_deadline()
{
    SchedulePolicy policy;
    const std::uint64_t start = std::uint64_t{UINT32_MAX} - 500;
    policy.touch(beijing(5, 20, 30), start);
    const auto status = policy.update(beijing(5, 20, 30, 1), start + 1000);
    check_mode(status, ScheduleMode::Manual);
    check(status.manual_remaining_ms == hour_ms - 1000, "32-bit uptime boundary corrupted remaining time");
    check_mode(policy.update(beijing(5, 21, 30), start + hour_ms), ScheduleMode::Off);
}
void generation_changes_only_on_measurement_enablement_edges()
{
    SchedulePolicy policy;
    const auto waiting = policy.update(0, 0);
    const auto off = policy.update(beijing(5, 12), 1000);
    check(waiting.generation == off.generation, "clock availability changed a disabled generation");
    policy.touch(beijing(5, 12), 1000);
    const auto manual = policy.update(beijing(5, 12), 1000);
    check(manual.generation != off.generation, "touch did not create a new measurement generation");
    policy.touch(beijing(5, 12, 1), 61000);
    check(policy.update(beijing(5, 12, 1), 61000).generation == manual.generation,
          "repeated touch invalidated uninterrupted measurements");
    const auto closed = policy.update(beijing(5, 21, 30), 10 * hour_ms);
    check(closed.generation != manual.generation, "closing retained the old measurement generation");
    check(policy.update(beijing(5, 21, 31), 10 * hour_ms + 60000).generation == closed.generation,
          "disabled polling repeatedly changed the measurement generation");
}
void touch_at_cutoff_cannot_reopen_or_reuse_the_previous_alert_generation()
{
    for (const int day : {0, 5}) {
        SchedulePolicy policy;
        policy.touch(beijing(day, 21, 29, 59), 1000);
        const auto previous = policy.update(beijing(day, 21, 29, 59), 1000);
        check(previous.enabled(), "cutoff test did not start enabled");
        // No sampler update occurs between cutoff and this new touch.
        policy.touch(beijing(day, 21, 30), 2000);
        const auto closed = policy.update(beijing(day, 21, 30), 2000);
        check_mode(closed, ScheduleMode::Off);
        check(closed.generation == previous.generation + 1, "touch skipped the off edge and reused old alert generation");
    }
}
void fresh_touch_after_monotonic_expiry_creates_a_new_generation()
{
    SchedulePolicy policy;
    policy.touch(beijing(5, 20, 30), 1000);
    const auto previous = policy.update(beijing(5, 20, 30), 1000);
    // Clock correction leaves the wall time inside the window after lease expiry.
    policy.touch(beijing(5, 20), 1000 + hour_ms);
    const auto reopened = policy.update(beijing(5, 20), 1000 + hour_ms);
    check_mode(reopened, ScheduleMode::Manual);
    check(reopened.generation == previous.generation + 2, "new touch reused the expired alert generation");
}

void card_pauses_the_automatic_window_and_next_morning_resumes()
{
    SchedulePolicy policy;
    const auto automatic = policy.update(beijing(0, 10), 1000);
    const auto paused = policy.toggle(beijing(0, 10), 1000);
    check_mode(paused, ScheduleMode::ManualOff);
    check(std::strcmp(distance_sensor::schedule_name(paused.mode), "manual_off") == 0,
          "card pause has no distinct schedule name");
    check(paused.generation == automatic.generation + 1, "card pause did not invalidate measurements");
    check_mode(policy.update(beijing(0, 21, 29, 59), 1000 + 11 * hour_ms + 1799000), ScheduleMode::ManualOff);
    const auto closed = policy.update(beijing(0, 21, 30), 1000 + 11 * hour_ms + 1800000);
    check_mode(closed, ScheduleMode::Off);
    check(closed.generation == paused.generation, "disabled boundary changed generation");
    check_mode(policy.update(beijing(1, 9, 29, 59), 1000 + 23 * hour_ms + 1799000), ScheduleMode::Off);
    check_mode(policy.update(beijing(1, 9, 30), 1000 + 23 * hour_ms + 1800000), ScheduleMode::Automatic);
}
void second_card_restores_enabled_state_until_evening_cutoff()
{
    SchedulePolicy policy;
    const auto paused = policy.toggle(beijing(0, 12), 1000);
    check_mode(paused, ScheduleMode::ManualOff);
    const auto enabled = policy.toggle(beijing(0, 12, 1), 61000);
    check_mode(enabled, ScheduleMode::Manual);
    check(enabled.manual_remaining_ms == 9 * hour_ms + 29 * 60000, "card reopening changed evening cutoff");
    check(enabled.generation == paused.generation + 1, "second card did not create a new measurement generation");
    check_mode(policy.update(beijing(0, 21, 30), 1000 + 9 * hour_ms + 1800000), ScheduleMode::Off);
}
void night_card_overrides_survive_midnight_until_morning()
{
    SchedulePolicy policy;
    auto status = policy.toggle(beijing(0, 22), 1000);
    check_mode(status, ScheduleMode::Manual);
    check(status.manual_remaining_ms == 11 * hour_ms + 1800000, "night card does not end at 09:30");
    status = policy.update(beijing(1, 0), 1000 + 2 * hour_ms);
    check_mode(status, ScheduleMode::Manual);
    check(status.manual_remaining_ms == 9 * hour_ms + 1800000, "midnight reset the night override");
    check_mode(policy.toggle(beijing(1, 1), 1000 + 3 * hour_ms), ScheduleMode::ManualOff);
    check_mode(policy.update(beijing(1, 9, 29, 59), 1000 + 11 * hour_ms + 1799000), ScheduleMode::ManualOff);
    check_mode(policy.update(beijing(1, 9, 30), 1000 + 11 * hour_ms + 1800000), ScheduleMode::Automatic);

    SchedulePolicy paused_before_midnight;
    paused_before_midnight.toggle(beijing(0, 22), 1000);
    check_mode(paused_before_midnight.toggle(beijing(0, 23), 1000 + hour_ms), ScheduleMode::ManualOff);
    check_mode(paused_before_midnight.update(beijing(1, 0), 1000 + 2 * hour_ms), ScheduleMode::ManualOff);
    const auto reopened = paused_before_midnight.toggle(beijing(1, 1), 1000 + 3 * hour_ms);
    check_mode(reopened, ScheduleMode::Manual);
    const auto automatic = paused_before_midnight.update(beijing(1, 9, 30), 1000 + 11 * hour_ms + 1800000);
    check_mode(automatic, ScheduleMode::Automatic);
    check(automatic.generation == reopened.generation, "uninterrupted night-to-automatic transition invalidated measurements");
}
void weekend_card_expires_without_automatic_opening_then_monday_resumes()
{
    for (const auto start : {beijing(4, 22), beijing(5, 12)}) {
        SchedulePolicy policy;
        check_mode(policy.toggle(start, 1000), ScheduleMode::Manual);
        const auto boundary = start == beijing(4, 22) ? beijing(5, 9, 30) : beijing(5, 21, 30);
        check_mode(policy.update(boundary, 2000), ScheduleMode::Off);
        check_mode(policy.update(beijing(6, 12), 3000), ScheduleMode::Off);
        check_mode(policy.update(beijing(7, 9, 30), 4000), ScheduleMode::Automatic);
    }
    SchedulePolicy sunday_night;
    sunday_night.toggle(beijing(6, 22), 1000);
    check_mode(sunday_night.toggle(beijing(6, 23), 2000), ScheduleMode::ManualOff);
    check_mode(sunday_night.update(beijing(7, 9, 30), 3000), ScheduleMode::Automatic);
}
void head_touch_cannot_override_a_card_pause_or_extend_a_card_lease()
{
    for (const int day : {0, 5}) {
        SchedulePolicy policy;
        if (day == 5) policy.touch(beijing(day, 12), 1000);
        check_mode(policy.toggle(beijing(day, 12), 1000), ScheduleMode::ManualOff);
        policy.touch(beijing(day, 12, 1), 61000);
        check_mode(policy.update(beijing(day, 12, 1), 61000), ScheduleMode::ManualOff);
        check_mode(policy.update(beijing(day, 21, 30), 2000), ScheduleMode::Off);
    }
    SchedulePolicy enabled;
    enabled.toggle(beijing(5, 20, 30), 1000);
    enabled.touch(beijing(5, 19, 30), 11000);
    check(enabled.update(beijing(5, 19, 30), 11000).manual_remaining_ms == hour_ms - 10000,
          "touch extended the card lease after backward clock correction");
    check_mode(enabled.update(beijing(5, 20, 30), 1000 + hour_ms), ScheduleMode::Off);
}
void invalid_clock_cannot_open_or_queue_card_override()
{
    for (const std::int64_t invalid : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{1577836799},
             std::int64_t{UINT32_MAX} + 1, std::numeric_limits<std::int64_t>::max()}) {
        SchedulePolicy policy;
        const auto status = policy.toggle(invalid, 1000);
        check_mode(status, ScheduleMode::WaitingClock);
        check(!status.clock_available && status.local_weekday == -1 && status.local_minute == -1,
              "invalid card clock invented a local calendar");
        check_mode(policy.update(beijing(5, 12), 2000), ScheduleMode::Off);
    }
    SchedulePolicy policy;
    check(policy.toggle(1577836800, 1000).enabled(), "card rejected minimum valid clock");
    SchedulePolicy upper;
    check(upper.toggle(UINT32_MAX, 1000).enabled(), "card rejected maximum valid clock");
}
void losing_clock_cancels_both_card_states()
{
    for (const bool pause : {false, true}) {
        SchedulePolicy policy;
        policy.toggle(beijing(5, 12), 1000);
        if (pause) policy.toggle(beijing(5, 12), 1000);
        check_mode(policy.update(0, 2000), ScheduleMode::WaitingClock);
        check_mode(policy.update(beijing(5, 12), 3000), ScheduleMode::Off);
    }
    SchedulePolicy paused;
    check_mode(paused.toggle(beijing(0, 12), 1000), ScheduleMode::ManualOff);
    check_mode(paused.update(0, 2000), ScheduleMode::WaitingClock);
    check_mode(paused.update(beijing(0, 12), 3000), ScheduleMode::Automatic);
}
void correction_outside_card_period_cancels_without_reviving_old_touch()
{
    for (const bool pause : {false, true}) {
        for (const auto corrected : {beijing(5, 9, 29, 59), beijing(5, 21, 30), beijing(6, 12)}) {
            SchedulePolicy policy;
            policy.touch(beijing(5, 12), 1000);
            policy.toggle(beijing(5, 12), 1000);
            if (!pause) policy.toggle(beijing(5, 12), 1000);
            check_mode(policy.update(corrected, 2000), ScheduleMode::Off);
            check_mode(policy.update(beijing(5, 12), 3000), ScheduleMode::Off);
        }
    }
    for (const auto corrected : {beijing(5, 21, 29, 59), beijing(6, 9, 30), beijing(6, 22)}) {
        SchedulePolicy night;
        night.toggle(beijing(5, 22), 1000);
        check_mode(night.update(corrected, 2000), ScheduleMode::Off);
        check_mode(night.update(beijing(5, 22), 3000), ScheduleMode::Off);
    }
}
void forward_correction_uses_the_earlier_wall_boundary()
{
    SchedulePolicy policy;
    policy.toggle(beijing(5, 12), 1000);
    const auto before_boundary = policy.update(beijing(5, 21, 29, 59), 2000);
    check_mode(before_boundary, ScheduleMode::Manual);
    check(before_boundary.manual_remaining_ms == 1000, "forward correction kept the later monotonic remainder");
    check_mode(policy.update(beijing(5, 21, 30), 3000), ScheduleMode::Off);
    check_mode(policy.update(beijing(5, 12), 4000), ScheduleMode::Off);
}
void backward_correction_and_repeated_cards_keep_the_initial_deadline()
{
    SchedulePolicy policy;
    policy.toggle(beijing(5, 20, 30), 1000);
    auto status = policy.update(beijing(5, 19, 30), 11000);
    check_mode(status, ScheduleMode::Manual);
    check(status.manual_remaining_ms == hour_ms - 10000, "backward correction extended card lease");
    check_mode(policy.toggle(beijing(5, 19, 31), 71000), ScheduleMode::ManualOff);
    status = policy.toggle(beijing(5, 19, 32), 131000);
    check_mode(status, ScheduleMode::Manual);
    check(status.manual_remaining_ms == hour_ms - 130000, "repeated cards extended the initial monotonic deadline");
    check_mode(policy.update(beijing(5, 20), 1000 + hour_ms), ScheduleMode::Off);

    SchedulePolicy night;
    night.toggle(beijing(6, 8, 30), 1000);
    const auto previous_evening = night.update(beijing(5, 23), 11000);
    check_mode(previous_evening, ScheduleMode::Manual);
    check(previous_evening.manual_remaining_ms == hour_ms - 10000, "backward midnight correction lost the continuous night period");
    check_mode(night.update(beijing(6, 0), 1000 + hour_ms), ScheduleMode::Off);

    SchedulePolicy paused;
    check_mode(paused.toggle(beijing(0, 20, 30), 1000), ScheduleMode::ManualOff);
    check_mode(paused.update(beijing(0, 19, 30), 11000), ScheduleMode::ManualOff);
    check_mode(paused.update(beijing(0, 20), 1000 + hour_ms), ScheduleMode::Automatic);
}
void card_uses_64_bit_uptime_and_discards_state_on_reboot()
{
    SchedulePolicy policy;
    const std::uint64_t start = std::uint64_t{UINT32_MAX} - 500;
    policy.toggle(beijing(5, 20, 30), start);
    const auto status = policy.update(beijing(5, 20, 30, 1), start + 1000);
    check_mode(status, ScheduleMode::Manual);
    check(status.manual_remaining_ms == hour_ms - 1000, "32-bit uptime boundary corrupted card deadline");
    check_mode(policy.update(beijing(5, 21, 30), start + hour_ms), ScheduleMode::Off);
    SchedulePolicy rebooted;
    check_mode(rebooted.update(beijing(5, 20, 30), 0), ScheduleMode::Off);
    check_mode(rebooted.update(beijing(0, 12), 1000), ScheduleMode::Automatic);
}
void card_at_cutoff_observes_expired_edge_before_reopening()
{
    for (const int day : {0, 5}) {
        SchedulePolicy policy;
        policy.touch(beijing(day, 21, 29, 59), 1000);
        const auto previous = policy.update(beijing(day, 21, 29, 59), 1000);
        const auto reopened = policy.toggle(beijing(day, 21, 30), 2000);
        check_mode(reopened, ScheduleMode::Manual);
        check(reopened.manual_remaining_ms == 12 * hour_ms, "cutoff card did not open the new night period");
        check(reopened.generation == previous.generation + 2, "cutoff card skipped the expired measurement generation");
    }
    SchedulePolicy morning;
    morning.toggle(beijing(0, 22), 1000);
    const auto previous = morning.toggle(beijing(0, 23), 2000);
    const auto paused = morning.toggle(beijing(1, 9, 30), 3000);
    check_mode(paused, ScheduleMode::ManualOff);
    check(paused.generation == previous.generation + 2, "morning card skipped the automatic opening edge");
}
void card_after_monotonic_expiry_observes_the_old_off_edge()
{
    SchedulePolicy policy;
    const auto previous = policy.toggle(beijing(5, 20, 30), 1000);
    const auto reopened = policy.toggle(beijing(5, 20), 1000 + hour_ms);
    check_mode(reopened, ScheduleMode::Manual);
    check(reopened.generation == previous.generation + 2, "card reused the expired measurement generation");
    check(policy.update(beijing(5, 20), 1000 + hour_ms).generation == reopened.generation,
          "polling an active card repeatedly changed generation");
}

} // namespace

int main()
{
    const struct Test { const char* name; void (*run)(); } tests[] = {
        {"weekday_window_has_exact_start_and_end", weekday_window_has_exact_start_and_end},
        {"weekends_have_no_automatic_window", weekends_have_no_automatic_window},
        {"utc_date_boundary_uses_the_next_beijing_day", utc_date_boundary_uses_the_next_beijing_day},
        {"invalid_clock_never_opens_or_queues_a_manual_window", invalid_clock_never_opens_or_queues_a_manual_window},
        {"weekend_touch_enables_only_until_2130", weekend_touch_enables_only_until_2130},
        {"outside_window_touches_do_not_enable_or_reserve_a_future_window", outside_window_touches_do_not_enable_or_reserve_a_future_window},
        {"manual_touch_never_carries_into_another_day", manual_touch_never_carries_into_another_day},
        {"automatic_window_touch_does_not_extend_the_evening", automatic_window_touch_does_not_extend_the_evening},
        {"reboot_discards_manual_override", reboot_discards_manual_override},
        {"usable_clock_keeps_schedule_working_without_network_input", usable_clock_keeps_schedule_working_without_network_input},
        {"losing_clock_cancels_manual_instead_of_resuming_after_sync", losing_clock_cancels_manual_instead_of_resuming_after_sync},
        {"backward_clock_correction_cannot_extend_the_original_lease", backward_clock_correction_cannot_extend_the_original_lease},
        {"correction_outside_window_immediately_cancels_manual", correction_outside_window_immediately_cancels_manual},
        {"uptime_beyond_uint32_wrap_retains_the_correct_deadline", uptime_beyond_uint32_wrap_retains_the_correct_deadline},
        {"generation_changes_only_on_measurement_enablement_edges", generation_changes_only_on_measurement_enablement_edges},
        {"touch_at_cutoff_cannot_reopen_or_reuse_the_previous_alert_generation", touch_at_cutoff_cannot_reopen_or_reuse_the_previous_alert_generation},
        {"fresh_touch_after_monotonic_expiry_creates_a_new_generation", fresh_touch_after_monotonic_expiry_creates_a_new_generation},
        {"card_pauses_the_automatic_window_and_next_morning_resumes", card_pauses_the_automatic_window_and_next_morning_resumes},
        {"second_card_restores_enabled_state_until_evening_cutoff", second_card_restores_enabled_state_until_evening_cutoff},
        {"night_card_overrides_survive_midnight_until_morning", night_card_overrides_survive_midnight_until_morning},
        {"weekend_card_expires_without_automatic_opening_then_monday_resumes", weekend_card_expires_without_automatic_opening_then_monday_resumes},
        {"head_touch_cannot_override_a_card_pause_or_extend_a_card_lease", head_touch_cannot_override_a_card_pause_or_extend_a_card_lease},
        {"invalid_clock_cannot_open_or_queue_card_override", invalid_clock_cannot_open_or_queue_card_override},
        {"losing_clock_cancels_both_card_states", losing_clock_cancels_both_card_states},
        {"correction_outside_card_period_cancels_without_reviving_old_touch", correction_outside_card_period_cancels_without_reviving_old_touch},
        {"forward_correction_uses_the_earlier_wall_boundary", forward_correction_uses_the_earlier_wall_boundary},
        {"backward_correction_and_repeated_cards_keep_the_initial_deadline", backward_correction_and_repeated_cards_keep_the_initial_deadline},
        {"card_uses_64_bit_uptime_and_discards_state_on_reboot", card_uses_64_bit_uptime_and_discards_state_on_reboot},
        {"card_at_cutoff_observes_expired_edge_before_reopening", card_at_cutoff_observes_expired_edge_before_reopening},
        {"card_after_monotonic_expiry_observes_the_old_off_edge", card_after_monotonic_expiry_observes_the_old_off_edge},
    };
    unsigned passed = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    const auto total = sizeof(tests) / sizeof(tests[0]);
    std::cout << passed << '/' << total << " distance schedule tests passed\n";
    return passed == total ? 0 : 1;
}
