#include <apps/common/idle_program/policy.h>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace idle_program;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
constexpr Context eligible{true, false, false};

Policy make_policy(Config config = {}, std::uint32_t now = 0)
{
    Policy policy;
    check(policy.set_config(config, now), "valid configuration rejected");
    policy.start(now);
    policy.set_context(eligible, now);
    policy.tick(now);
    return policy;
}

void run(Policy& policy, std::uint32_t& now, std::uint32_t duration, Context context = eligible)
{
    while (duration) {
        const auto step = std::min(duration, 100u);
        now += step;
        duration -= step;
        policy.set_context(context, now);
        policy.tick(now);
    }
}

WeeklyUpdate quota(std::uint32_t sequence = 1, std::uint32_t observed = 1700000000u)
{
    return {true, sequence, 37, observed + 3600u, observed, 900000};
}

void defaults_and_configuration_bounds()
{
    Config config;
    check(config.eye_interval_ms == 1200000 && config.rest_duration_ms == 20000 &&
              config.idle_grace_ms == 10000 && config.pet_gap_ms == 3000 &&
              config.weekly_duration_ms == 12000 && config.weekly_interval_ms == 30000,
          "default schedule changed");
    check(valid_config(config), "defaults invalid");
    config.eye_interval_ms = 999;
    check(!valid_config(config), "sub-second eye interval accepted");
    config.eye_interval_ms = 86400001;
    check(!valid_config(config), "over-one-day interval accepted");
    config.eye_interval_ms = 20000;
    config.rest_duration_ms = 1000;
    config.idle_grace_ms = config.pet_gap_ms = 0;
    check(valid_config(config), "debug configuration rejected");
}

void twenty_minute_due_waits_for_idle_eligibility()
{
    Policy policy;
    policy.start(0);
    policy.tick(1200000);
    auto state = policy.snapshot(1200000);
    check(state.blocked && state.program == Program::Pet && state.eye_remaining_ms == 0,
          "eye due ran without a fresh eligible context");
    std::uint32_t now = 1200000;
    policy.set_context(eligible, now);
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "eye rest began before 10 seconds idle");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeRest, "idle overdue rest did not start at 10 seconds");
}

void full_twenty_second_rest_and_two_second_completion()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    auto state = policy.snapshot(now);
    check(state.program == Program::EyeRest && state.remaining_ms == 20000, "rest did not start at full 20 seconds");
    run(policy, now, 19999);
    state = policy.snapshot(now);
    check(state.program == Program::EyeRest && state.remaining_ms == 1 && state.completed_rests == 0,
          "rest completed before its full duration");
    run(policy, now, 1);
    state = policy.snapshot(now);
    check(state.program == Program::RestDone && state.remaining_ms == 2000 && state.completed_rests == 1 &&
              state.eye_remaining_ms == 20000, "completed rest did not reset one future interval");
    run(policy, now, 1999);
    check(policy.snapshot(now).program == Program::RestDone, "completion celebration ended early");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Pet, "completion celebration did not return to pet");
    check(policy.snapshot(now).completed_rests == 1, "reading snapshots added completions");
}

void activity_cancels_without_completion_and_holds_idle()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    run(policy, now, 10000);
    policy.note_activity(now);
    const auto generation = policy.snapshot(now).generation;
    for (unsigned touch = 0; touch < 1000; ++touch) {
        now += 20;
        policy.note_activity(now);
        policy.set_context(eligible, now);
        policy.tick(now);
        const auto state = policy.snapshot(now);
        check(state.program == Program::Pet && state.completed_rests == 0 && state.eye_remaining_ms == 0,
              "held touch restarted or completed a cancelled rest");
        check(state.generation == generation, "repeated pet activity changed programme generation");
    }
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "cancelled rest ignored fresh idle grace");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeRest && policy.snapshot(now).remaining_ms == 20000,
          "cancelled due did not restart a complete rest");
}

void blocked_context_and_asynchronous_staleness_cancel()
{
    for (const auto blocked : {Context{false, false, false}, Context{true, true, false}, Context{true, false, true}}) {
        Config config;
        config.eye_interval_ms = 20000;
        auto policy = make_policy(config);
        std::uint32_t now = 0;
        run(policy, now, 20000);
        policy.set_context(blocked, now);
        check(policy.snapshot(now).blocked && policy.snapshot(now).program == Program::Pet &&
                  policy.snapshot(now).completed_rests == 0, "explicit task/alert did not cancel rest");
        run(policy, now, 20000, blocked);
        run(policy, now, 100);
        check(policy.snapshot(now).program == Program::Pet, "unblock reused time spent in a foreground task");
        run(policy, now, 10000);
        check(policy.snapshot(now).program == Program::EyeRest, "due was lost after foreground cancellation");
    }
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    check(policy.snapshot(now + 499u).program == Program::EyeRest, "499 ms context was rejected");
    check(policy.snapshot(now + 500u).blocked && policy.snapshot(now + 500u).program == Program::Pet,
          "500 ms stale asynchronous context remained eligible");
    // A late UI refresh must not hide the interruption and award completion.
    now += 20000;
    policy.set_context(eligible, now);
    policy.tick(now);
    check(policy.snapshot(now).completed_rests == 0 && policy.snapshot(now).program == Program::Pet,
          "late context refresh retroactively completed a rest");
}

void overdue_intervals_never_form_a_backlog()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 180000, {true, true, false});
    run(policy, now, 10100);
    check(policy.snapshot(now).program == Program::EyeRest, "overdue single rest did not start");
    run(policy, now, 20000);
    check(policy.snapshot(now).completed_rests == 1, "one due generated multiple completions");
    run(policy, now, 19999);
    check(policy.snapshot(now).program != Program::EyeRest && policy.snapshot(now).completed_rests == 1,
          "missed intervals were replayed as a backlog");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeRest, "next interval was not measured from completion");
}

void twelve_three_twelve_three_information_cadence()
{
    auto policy = make_policy();
    check(policy.update_weekly(quota(), 0), "quota rejected");
    std::uint32_t now = 0;
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "information appeared before idle grace");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).remaining_ms == 12000 &&
              policy.snapshot(now).eye_remaining_ms == 1190000,
          "first 12-second countdown did not display the independent eye deadline");
    run(policy, now, 11999);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).remaining_ms == 1,
          "countdown card ended early");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Pet, "countdown did not return to pet after 12 seconds");
    run(policy, now, 2999);
    check(policy.snapshot(now).program == Program::Pet, "information cards skipped the three-second pet gap");
    run(policy, now, 1);
    check(now == 25000 && policy.snapshot(now).program == Program::Weekly && policy.snapshot(now).remaining_ms == 12000,
          "first weekly card incorrectly waited for its normal interval");
    run(policy, now, 12000);
    check(policy.snapshot(now).program == Program::Pet, "weekly card did not end after 12 seconds");
    run(policy, now, 3000);
    check(policy.snapshot(now).program == Program::EyeCountdown, "second cycle did not alternate back to countdown");
    run(policy, now, 15000);
    check(now == 55000 && policy.snapshot(now).program == Program::Weekly,
          "weekly starts did not preserve the 30-second lower bound");
    check(policy.snapshot(now).completed_rests == 0 && policy.snapshot(now).eye_remaining_ms == 1145000,
          "information cards advanced or completed the independent eye-rest timer");
}

void alternating_cards_are_fair_and_weekly_interval_is_a_lower_bound()
{
    auto policy = make_policy();
    policy.update_weekly(quota(), 0);
    std::uint32_t now = 0;
    run(policy, now, 10000);
    std::uint32_t countdown_ms = 0, weekly_ms = 0, pet_ms = 0;
    for (unsigned sample = 0; sample < 1500; ++sample) {
        const auto state = policy.snapshot(now);
        if (state.program == Program::EyeCountdown) countdown_ms += 100;
        else if (state.program == Program::Weekly) weekly_ms += 100;
        else if (state.program == Program::Pet) pet_ms += 100;
        else check(false, "ordinary rotation created an eye-rest completion");
        run(policy, now, 100);
    }
    check(countdown_ms == 60000 && weekly_ms == 60000 && pet_ms == 30000,
          "five normal cycles failed 12/3/12/3 fairness and 80 percent information occupancy");

    Config config;
    config.weekly_interval_ms = 60000;
    policy = make_policy(config);
    policy.update_weekly(quota(), 0);
    now = 0;
    run(policy, now, 25000);
    check(policy.snapshot(now).program == Program::Weekly, "first weekly card was not immediately eligible");
    run(policy, now, 30000);
    check(policy.snapshot(now).program == Program::EyeCountdown,
          "weekly interval delay extended plain pet instead of showing another countdown");
    run(policy, now, 29999);
    check(policy.snapshot(now).program == Program::Pet, "weekly card ignored its longer configured lower bound");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Weekly, "weekly lower bound prevented a due card forever");
}

void eye_due_preempts_each_information_card_and_pet_gap()
{
    for (const auto deadline : {15000u, 27000u}) {
        Config config;
        config.eye_interval_ms = deadline;
        auto policy = make_policy(config);
        policy.update_weekly(quota(), 0);
        std::uint32_t now = 0;
        run(policy, now, deadline - 1000u);
        check(policy.snapshot(now).program == (deadline == 15000 ? Program::EyeCountdown : Program::Weekly),
              "preemption test did not enter the intended information card");
        run(policy, now, 1000);
        check(policy.snapshot(now).program == Program::EyeRest && policy.snapshot(now).remaining_ms == 20000 &&
                  policy.snapshot(now).completed_rests == 0,
              "due eye rest did not replace the information card with a complete 20-second rest");
    }
    Config config;
    config.eye_interval_ms = 20000;
    config.pet_gap_ms = 30000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    check(policy.snapshot(now).program == Program::EyeRest, "eye due waited for an ordinary pet gap");
}

void interrupted_information_is_discarded_and_next_card_waits_for_idle()
{
    auto policy = make_policy();
    policy.update_weekly(quota(), 0);
    std::uint32_t now = 0;
    run(policy, now, 12000);
    check(policy.snapshot(now).program == Program::EyeCountdown, "interruption test did not start a countdown card");
    policy.set_context({true, true, false}, now);
    run(policy, now, 5000, {true, true, false});
    policy.set_context(eligible, now);
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "foreground interruption did not reset idle grace");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Weekly && policy.snapshot(now).remaining_ms == 12000 &&
              policy.snapshot(now).completed_rests == 0,
          "interrupted countdown was queued or counted as a completed eye rest");
    run(policy, now, 1000);
    policy.note_activity(now);
    run(policy, now, 10000);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).completed_rests == 0,
          "touch interruption replayed the weekly card instead of moving to the next card");
}

void interrupted_weekly_does_not_extend_the_pure_pet_gap()
{
    for (const bool foreground : {false, true}) {
        auto policy = make_policy();
        policy.update_weekly(quota(), 0);
        std::uint32_t now = 0;
        run(policy, now, 25000);
        check(policy.snapshot(now).program == Program::Weekly, "interruption test did not start weekly");
        if (foreground) {
            policy.set_context({true, true, false}, now);
            run(policy, now, 1000, {true, true, false});
            policy.set_context(eligible, now);
            run(policy, now, 10000);
        } else {
            WeeklyUpdate unavailable;
            unavailable.sequence = 2;
            policy.update_weekly(unavailable, now);
            run(policy, now, 3000);
        }
        check(policy.snapshot(now).program == Program::EyeCountdown,
              "interrupted weekly did not advance to a countdown");
        run(policy, now, 12000);
        check(policy.snapshot(now).program == Program::Pet, "countdown did not finish before fallback");
        run(policy, now, 2999);
        check(policy.snapshot(now).program == Program::Pet, "fallback skipped the configured pet gap");
        run(policy, now, 1);
        check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).remaining_ms == 12000 &&
                  policy.snapshot(now).completed_rests == 0,
              "pending weekly lower bound extended pure pet beyond three seconds");
        run(policy, now, 15000);
        check(policy.snapshot(now).program == Program::Weekly,
              "fallback countdown starved weekly after its lower bound became due");
    }
}

void weekly_duplicates_old_observations_and_ttl_do_not_refresh()
{
    auto policy = make_policy();
    auto value = quota();
    value.ttl_ms = 60000;
    check(policy.update_weekly(value, 0), "first quota rejected");
    check(policy.snapshot(59999).weekly_status == WeeklyStatus::Fresh, "TTL ended early");
    check(!policy.update_weekly(value, 59999), "duplicate sequence was accepted");
    check(policy.snapshot(60000).weekly_status == WeeklyStatus::Stale && policy.snapshot(60000).weekly_left == -1,
          "duplicate refreshed expired quota or stale quota exposed fresh percent");
    value.sequence = 2;
    value.ttl_ms = 900000;
    check(policy.update_weekly(value, 60000) && policy.snapshot(60000).weekly_status == WeeklyStatus::Stale,
          "same observed timestamp with a new sequence extended old TTL");
    value.sequence = 3;
    --value.observed_at;
    check(!policy.update_weekly(value, 60001), "backward observed_at was accepted");
    value = quota(3, 1700000061u);
    check(policy.update_weekly(value, 61000) && policy.snapshot(61000).weekly_status == WeeklyStatus::Fresh,
          "new non-backward query could not refresh quota");
}

void reset_deadline_absolute_clock_and_shortened_reconnect_ttl()
{
    auto policy = make_policy();
    auto value = quota();
    value.reset_at = value.observed_at + 30u;
    value.ttl_ms = 60000;
    policy.update_weekly(value, 0);
    check(policy.snapshot(29999).weekly_status == WeeklyStatus::Fresh &&
              policy.snapshot(30000).weekly_status == WeeklyStatus::Stale, "reset did not expire earlier than TTL");
    policy = make_policy();
    value = quota();
    value.ttl_ms = 60000;
    policy.update_weekly(value, 0, value.observed_at + 60u);
    check(policy.snapshot(0).weekly_status == WeeklyStatus::Stale, "already-expired absolute-clock data became fresh");
    policy = make_policy();
    value.ttl_ms = 120000; // Reconnected host supplied only two minutes of its cache budget.
    policy.update_weekly(value, 0);
    check(policy.snapshot(119999).weekly_status == WeeklyStatus::Fresh &&
              policy.snapshot(120000).weekly_status == WeeklyStatus::Stale, "reconnection expanded a shortened TTL");
}

void unavailable_is_explicit_and_missing_data_is_not_a_card()
{
    auto policy = make_policy();
    std::uint32_t now = 0;
    run(policy, now, 60000);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).weekly_status == WeeklyStatus::Waiting &&
              policy.snapshot(now).completed_rests == 0,
          "missing quota fabricated a weekly card");
    WeeklyUpdate unavailable;
    unavailable.sequence = 1;
    check(policy.update_weekly(unavailable, now), "explicit unavailable rejected");
    run(policy, now, 10000);
    check(policy.snapshot(now).program == Program::Weekly && policy.snapshot(now).weekly_status == WeeklyStatus::Unavailable &&
              policy.snapshot(now).weekly_left == -1, "explicit unavailable was presented as a percentage");
}

void stale_quota_rotates_without_a_current_percentage()
{
    auto policy = make_policy();
    auto value = quota();
    value.ttl_ms = 60000;
    policy.update_weekly(value, 0);
    std::uint32_t now = 0;
    run(policy, now, 60000);
    auto state = policy.snapshot(now);
    check(state.program == Program::Weekly && state.weekly_status == WeeklyStatus::Stale && state.weekly_left == -1,
          "expired quota failed to rotate explicitly as stale without an old percentage");
    run(policy, now, 7000);
    check(policy.snapshot(now).program == Program::Pet, "stale weekly card failed to return to pet");
    run(policy, now, 18000);
    state = policy.snapshot(now);
    check(state.program == Program::Weekly && state.weekly_status == WeeklyStatus::Stale && state.weekly_left == -1,
          "stale status did not preserve the normal weekly rotation cadence");
}

void stored_config_v1_migrates_cadence_and_v2_preserves_settings()
{
    Config loaded;
    const StoredConfig legacy{1, 0, 600000, 45000, 1234, 30000, 6000, 60000};
    check(decode_config(legacy, loaded) && !loaded.enabled && loaded.eye_interval_ms == 600000 &&
              loaded.rest_duration_ms == 45000 && loaded.idle_grace_ms == 10000 && loaded.pet_gap_ms == 3000 &&
              loaded.weekly_duration_ms == 12000 && loaded.weekly_interval_ms == 30000,
          "version-one migration retained hidden old cadence or lost visible user settings");
    const StoredConfig current{2, 1, 900000, 30000, 5000, 4000, 15000, 45000};
    check(decode_config(current, loaded) && loaded.enabled && loaded.eye_interval_ms == 900000 &&
              loaded.rest_duration_ms == 30000 && loaded.idle_grace_ms == 5000 && loaded.pet_gap_ms == 4000 &&
              loaded.weekly_duration_ms == 15000 && loaded.weekly_interval_ms == 45000,
          "version-two settings did not survive decoding");
    auto invalid = current;
    invalid.version = 3;
    check(!decode_config(invalid, loaded) && loaded.weekly_interval_ms == 45000, "unknown storage version mutated current settings");
    invalid = current;
    invalid.enabled = 2;
    check(!decode_config(invalid, loaded), "invalid stored boolean accepted");
}

void clock_wrap_and_read_only_snapshots()
{
    Config config;
    config.eye_interval_ms = 20000;
    std::uint32_t now = std::numeric_limits<std::uint32_t>::max() - 25000u;
    auto policy = make_policy(config, now);
    const auto started = now;
    auto value = quota();
    value.ttl_ms = 60000;
    policy.update_weekly(value, now);
    run(policy, now, 20000);
    run(policy, now, 20000);
    check(now < started && policy.snapshot(now).program == Program::RestDone && policy.snapshot(now).completed_rests == 1,
          "countdown/completion failed across uint32 wrap");
    check(policy.snapshot(now).weekly_status == WeeklyStatus::Fresh, "quota aged incorrectly across uint32 wrap");
    check(policy.snapshot(now - 1u).blocked, "backdated snapshot was treated as current context");
    const auto generation = policy.snapshot(now).generation;
    policy.note_activity(now - 1u);
    check(policy.snapshot(now).generation == generation, "backdated activity rewound policy state");
    run(policy, now, 20000);
    check(policy.snapshot(now).weekly_status == WeeklyStatus::Stale, "wrapped monotonic TTL failed to expire");
}

} // namespace

int main()
{
    const std::array tests{defaults_and_configuration_bounds, twenty_minute_due_waits_for_idle_eligibility,
        full_twenty_second_rest_and_two_second_completion, activity_cancels_without_completion_and_holds_idle,
        blocked_context_and_asynchronous_staleness_cancel, overdue_intervals_never_form_a_backlog,
        twelve_three_twelve_three_information_cadence, alternating_cards_are_fair_and_weekly_interval_is_a_lower_bound,
        eye_due_preempts_each_information_card_and_pet_gap, interrupted_information_is_discarded_and_next_card_waits_for_idle,
        interrupted_weekly_does_not_extend_the_pure_pet_gap,
        weekly_duplicates_old_observations_and_ttl_do_not_refresh, reset_deadline_absolute_clock_and_shortened_reconnect_ttl,
        unavailable_is_explicit_and_missing_data_is_not_a_card, stale_quota_rotates_without_a_current_percentage,
        stored_config_v1_migrates_cadence_and_v2_preserves_settings, clock_wrap_and_read_only_snapshots};
    try { for (const auto test : tests) test(); }
    catch (const std::exception& error) { std::cerr << "idle_program_test: " << error.what() << '\n'; return 1; }
    std::cout << "idle_program_test: " << tests.size() << " policy groups passed\n";
    return 0;
}
