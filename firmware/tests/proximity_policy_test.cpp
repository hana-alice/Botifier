#include "../main/distance_sensor/proximity_policy.h"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using distance_sensor::ProximityConfig;
using distance_sensor::ProximityPolicy;
using distance_sensor::ProximityState;

namespace {

// Keep the existing five-second timing regressions explicit while the shipping
// default is tuned for the user's faster physical reminder.
constexpr ProximityConfig five_second_config{400, 80, 5000, 1500};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_waiting(const ProximityPolicy& policy)
{
    check(policy.state() == ProximityState::Waiting, "expected Waiting");
    check(!policy.alert_active(), "waiting must not retain alert");
    check(policy.filtered_mm() == -1, "waiting must not retain a filtered distance");
    check(policy.near_elapsed_ms() == 0, "waiting must not retain dwell");
}

void reach_alert(ProximityPolicy& policy)
{
    for (std::uint32_t now = 0; now <= 7000; now += 1000) {
        policy.feed(true, 300, now);
    }
    check(policy.alert_active(), "stable proximity did not reach alert");
}

void default_configuration_alerts_on_the_third_valid_sample()
{
    ProximityPolicy policy;
    check(policy.config().near_duration_ms == 0, "default must not add a proximity dwell");
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    check_waiting(policy);
    policy.feed(true, 300, 200);
    check(policy.state() == ProximityState::Alert, "third valid close sample must immediately alert");
    check(policy.filtered_mm() == 300 && policy.near_elapsed_ms() == 0,
          "immediate alert must still use the warmed median");
    policy.feed(true, 600, 300);
    check(policy.alert_active(), "one recovery spike must not bypass median");
    policy.feed(true, 600, 400);
    check(policy.state() == ProximityState::Safe, "far median must clear the immediate alert");
}

void default_hot_transition_alerts_without_additional_dwell()
{
    ProximityPolicy policy;
    policy.feed(true, 600, 0);
    policy.feed(true, 600, 100);
    policy.feed(true, 600, 200);
    policy.feed(true, 399, 300);
    check(policy.state() == ProximityState::Safe, "first close sample must still pass through the median");
    policy.feed(true, 399, 400);
    check(policy.filtered_mm() == 399 && policy.alert_active(),
          "a hot safe-to-close median must immediately alert");
    check(policy.near_elapsed_ms() == 0, "hot immediate alert must not invent dwell");
}

void default_threshold_equality_and_initial_dead_band_are_safe()
{
    for (const auto distance : std::array<std::uint16_t, 4>{400, 430, 479, 480}) {
        ProximityPolicy policy;
        policy.feed(true, distance, 0);
        policy.feed(true, distance, 100);
        policy.feed(true, distance, 200);
        check(policy.state() == ProximityState::Safe,
              "zero dwell must not turn threshold equality or an initial dead band into proximity");
        check(!policy.alert_active(), "initial non-close median must not immediately alert");
    }
}

void default_invalid_measurement_clears_an_immediate_alert()
{
    ProximityPolicy policy;
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    policy.feed(true, 300, 200);
    check(policy.alert_active(), "default precondition must be an immediate alert");
    policy.feed(false, 0, 300);
    check_waiting(policy);
    policy.feed(true, 300, 400);
    policy.feed(true, 300, 500);
    check_waiting(policy);
    policy.feed(true, 300, 600);
    check(policy.alert_active(), "invalid recovery must rebuild three samples before immediate alert");
}

void three_samples_start_dwell()
{
    ProximityPolicy policy(five_second_config);
    check_waiting(policy);
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    check_waiting(policy);
    policy.feed(true, 300, 200);
    check(policy.state() == ProximityState::Near, "third close sample must establish Near");
    check(policy.filtered_mm() == 300, "incorrect initial median");
    check(policy.near_elapsed_ms() == 0, "dwell must start after median warmup");
}

void stable_distance_remains_safe()
{
    ProximityPolicy policy(five_second_config);
    for (std::uint32_t now = 0; now <= 12000; now += 1000) {
        policy.feed(true, 600, now);
        check(!policy.alert_active(), "stable far measurements must not alert");
    }
    check(policy.state() == ProximityState::Safe, "stable far measurements must be Safe");
    check(policy.filtered_mm() == 600, "incorrect far median");
    check(policy.near_elapsed_ms() == 0, "safe measurements must not accumulate dwell");
}

void dwell_boundary_and_latched_alert()
{
    ProximityPolicy policy(five_second_config);
    for (std::uint32_t now = 0; now <= 6000; now += 1000) {
        policy.feed(true, 300, now);
        check(!policy.alert_active(), "alert fired before five seconds of filtered proximity");
    }
    policy.feed(true, 300, 6999);
    check(policy.near_elapsed_ms() == 4999, "wrong dwell immediately before threshold");
    check(!policy.alert_active(), "alert fired one millisecond early");
    policy.feed(true, 300, 7000);
    check(policy.state() == ProximityState::Alert, "five-second boundary must alert");
    check(policy.near_elapsed_ms() == 5000, "wrong dwell at alert threshold");
    policy.feed(true, 300, 8000);
    check(policy.alert_active(), "continued proximity must retain alert");
    check(policy.near_elapsed_ms() == 6000, "continued proximity must retain continuous dwell");
}

void threshold_equality_and_initial_dead_band_are_safe()
{
    for (const auto distance : std::array<std::uint16_t, 5>{400, 430, 479, 480, 4000}) {
        ProximityPolicy policy(five_second_config);
        for (std::uint32_t now = 0; now <= 8000; now += 1000) {
            policy.feed(true, distance, now);
        }
        check(policy.state() == ProximityState::Safe, "initial non-close distance must be Safe");
        check(!policy.alert_active(), "initial dead band must not alert");
    }
}

void hysteresis_restores_and_requires_full_new_dwell()
{
    ProximityPolicy policy(five_second_config);
    reach_alert(policy);
    policy.feed(true, 479, 8000);
    policy.feed(true, 479, 9000);
    check(policy.filtered_mm() == 479 && policy.alert_active(), "dead band must retain alert");
    policy.feed(true, 480, 10000);
    check(policy.alert_active(), "one far sample must not bypass the median");
    policy.feed(true, 480, 11000);
    check(policy.state() == ProximityState::Safe, "480 mm median must restore Safe");
    check(policy.near_elapsed_ms() == 0, "recovery must reset dwell");
    policy.feed(true, 430, 12000);
    policy.feed(true, 430, 13000);
    check(policy.state() == ProximityState::Safe, "dead band must retain recovered Safe");
    policy.feed(true, 399, 14000);
    policy.feed(true, 399, 15000);
    check(policy.state() == ProximityState::Near, "below 400 mm must restart proximity");
    check(policy.near_elapsed_ms() == 0, "re-entered proximity must start a fresh dwell");
    for (std::uint32_t now = 16000; now <= 19000; now += 1000) {
        policy.feed(true, 399, now);
        check(!policy.alert_active(), "recovery must not reuse previous dwell");
    }
    policy.feed(true, 399, 20000);
    check(policy.alert_active(), "full dwell after recovery must alert again");
}

void isolated_close_spike_is_rejected()
{
    ProximityPolicy policy;
    policy.feed(true, 600, 0);
    policy.feed(true, 600, 100);
    policy.feed(true, 600, 200);
    policy.feed(true, 40, 300);
    check(policy.filtered_mm() == 600, "isolated close spike must not change median");
    check(policy.state() == ProximityState::Safe, "isolated close spike must not establish Near");
    policy.feed(true, 600, 400);
    policy.feed(true, 600, 500);
    check(policy.state() == ProximityState::Safe, "spike recovery must remain Safe");
}

void isolated_far_spike_does_not_break_dwell()
{
    ProximityPolicy policy(five_second_config);
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 1000);
    policy.feed(true, 300, 2000);
    policy.feed(true, 1200, 3000);
    check(policy.state() == ProximityState::Near, "isolated far spike must retain Near");
    check(policy.filtered_mm() == 300, "isolated far spike must not change median");
    for (std::uint32_t now = 4000; now <= 7000; now += 1000) {
        policy.feed(true, 300, now);
    }
    check(policy.alert_active(), "far spike must not restart otherwise continuous dwell");
}

void invalid_measurement_clears_alert_and_dwell()
{
    ProximityPolicy policy(five_second_config);
    reach_alert(policy);
    policy.feed(false, 0, 8000);
    check_waiting(policy);
    policy.feed(true, 300, 8100);
    policy.feed(true, 300, 8200);
    check_waiting(policy);
    policy.feed(true, 300, 8300);
    check(policy.state() == ProximityState::Near, "valid recovery must rebuild the median");
    for (std::uint32_t now = 9300; now <= 12300; now += 1000) {
        policy.feed(true, 300, now);
        check(!policy.alert_active(), "invalid measurement must reset previous dwell");
    }
    policy.feed(true, 300, 13300);
    check(policy.alert_active(), "full dwell after invalid measurement must alert");
}

void zero_and_out_of_range_values_are_invalid_even_if_marked_valid()
{
    for (const auto distance : std::array<std::uint16_t, 4>{0, 39, 4001, 65535}) {
        ProximityPolicy policy(five_second_config);
        reach_alert(policy);
        policy.feed(true, distance, 8000);
        check_waiting(policy);
    }
    ProximityPolicy policy(five_second_config);
    policy.feed(true, 40, 0);
    policy.feed(true, 40, 100);
    policy.feed(true, 40, 200);
    check(policy.state() == ProximityState::Near, "40 mm hardware boundary is valid proximity");
}

void fresh_sample_after_timeout_starts_a_new_window()
{
    ProximityPolicy policy(five_second_config);
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 500);
    policy.feed(true, 300, 1000);
    policy.feed(true, 300, 2501);
    check_waiting(policy);
    policy.feed(true, 300, 2601);
    check_waiting(policy);
    policy.feed(true, 300, 2701);
    check(policy.state() == ProximityState::Near, "fresh samples must rebuild proximity after gap");
    check(policy.near_elapsed_ms() == 0, "sampling gap must not count toward dwell");
}

void expire_boundary_and_no_measurement_are_safe()
{
    ProximityPolicy policy(five_second_config);
    policy.expire(std::numeric_limits<std::uint32_t>::max());
    check_waiting(policy);
    reach_alert(policy);
    policy.expire(8500);
    check(policy.alert_active(), "exactly 1500 ms is still within sample timeout");
    policy.expire(8501);
    check_waiting(policy);
    policy.expire(10000);
    check_waiting(policy);
    policy.feed(true, 300, 11000);
    check_waiting(policy);
}

void expire_does_not_advance_dwell_without_fresh_samples()
{
    ProximityPolicy policy(five_second_config);
    for (std::uint32_t now = 0; now <= 6000; now += 1000) {
        policy.feed(true, 300, now);
    }
    policy.expire(7000);
    check(policy.state() == ProximityState::Near, "expire must not trigger on an old measurement");
    check(policy.near_elapsed_ms() == 4000, "expire must not advance dwell without fresh input");
    policy.expire(7501);
    check_waiting(policy);
}

void uptime_rollover_preserves_continuous_proximity()
{
    ProximityPolicy policy(five_second_config);
    const auto start = std::numeric_limits<std::uint32_t>::max() - 3000u;
    for (std::uint32_t offset = 0; offset <= 6000; offset += 1000) {
        policy.feed(true, 300, start + offset);
        check(!policy.alert_active(), "rollover must not cause an early alert");
    }
    policy.feed(true, 300, start + 6999u);
    check(policy.near_elapsed_ms() == 4999, "rollover corrupted near elapsed time");
    policy.feed(true, 300, start + 7000u);
    check(policy.alert_active(), "continuous dwell through rollover must alert");
    check(policy.near_elapsed_ms() == 5000, "rollover corrupted alert boundary");
}

void uptime_rollover_preserves_timeout_boundary()
{
    ProximityPolicy policy(five_second_config);
    const auto start = std::numeric_limits<std::uint32_t>::max() - 500u;
    policy.feed(true, 300, start);
    policy.feed(true, 300, start + 100u);
    policy.feed(true, 300, start + 200u);
    policy.expire(start + 1700u);
    check(policy.state() == ProximityState::Near, "rollover must retain exact timeout boundary");
    policy.expire(start + 1701u);
    check_waiting(policy);
}

void explicit_invalidation_requires_new_samples()
{
    ProximityPolicy policy(five_second_config);
    reach_alert(policy);
    policy.invalidate();
    check_waiting(policy);
    policy.feed(true, 600, 8000);
    policy.feed(true, 600, 8100);
    check_waiting(policy);
    policy.feed(true, 600, 8200);
    check(policy.state() == ProximityState::Safe, "invalidation must allow fresh safe recovery");
}

void custom_configuration_uses_the_same_latch_rules()
{
    ProximityPolicy policy(ProximityConfig{500, 100, 1000, 200});
    for (std::uint32_t now = 0; now <= 1200; now += 100) {
        policy.feed(true, 450, now);
    }
    check(policy.alert_active(), "custom dwell and threshold were not applied");
    policy.feed(true, 599, 1300);
    policy.feed(true, 599, 1400);
    check(policy.alert_active(), "custom hysteresis must retain alert below recovery boundary");
    policy.feed(true, 600, 1500);
    policy.feed(true, 600, 1600);
    check(policy.state() == ProximityState::Safe, "custom recovery boundary was not applied");
    policy.expire(1801);
    check_waiting(policy);
}

void check_configuration(const ProximityConfig& actual, const ProximityConfig& expected)
{
    check(actual.threshold_mm == expected.threshold_mm &&
          actual.hysteresis_mm == expected.hysteresis_mm &&
          actual.near_duration_ms == expected.near_duration_ms &&
          actual.sample_timeout_ms == expected.sample_timeout_ms,
          "policy configuration differs from the accepted configuration");
}

void check_reconfigured(const ProximityPolicy& policy)
{
    check(policy.state() == ProximityState::Waiting, "changed configuration must reset the proximity latch");
    check(!policy.alert_active(), "changed configuration must not retain an alert");
    check(policy.near_elapsed_ms() == 0, "changed configuration must discard old dwell");
}

void live_threshold_changes_wait_for_a_fresh_sample()
{
    ProximityPolicy policy;
    policy.feed(true, 500, 0);
    policy.feed(true, 500, 100);
    policy.feed(true, 500, 200);
    check(policy.state() == ProximityState::Safe, "threshold test must start safe");
    const ProximityConfig higher{600, 80, 0, 1500};
    check(policy.configure(higher), "valid higher threshold was rejected");
    check_configuration(policy.config(), higher);
    check_reconfigured(policy);
    policy.expire(250);
    check_reconfigured(policy);
    policy.feed(true, 500, 300);
    check(policy.filtered_mm() == 500 && policy.alert_active(),
          "fresh sample must apply the higher threshold using the retained median window");

    check(policy.configure(ProximityConfig{400, 200, 0, 1500}), "valid lower threshold was rejected");
    check_reconfigured(policy);
    policy.feed(true, 500, 400);
    check(policy.state() == ProximityState::Safe,
          "a changed threshold must not carry the old near latch into its initial dead band");
}

void live_hysteresis_changes_apply_the_new_recovery_boundary()
{
    ProximityPolicy policy;
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    policy.feed(true, 300, 200);
    check(policy.alert_active(), "hysteresis test must start with an alert");
    check(policy.configure(ProximityConfig{400, 200, 0, 1500}), "larger recovery margin was rejected");
    check_reconfigured(policy);
    policy.feed(true, 300, 300);
    check(policy.alert_active(), "fresh close median must reestablish immediate alert");
    policy.feed(true, 599, 400);
    policy.feed(true, 599, 500);
    check(policy.filtered_mm() == 599 && policy.alert_active(), "new recovery dead band must retain alert");
    policy.feed(true, 600, 600);
    policy.feed(true, 600, 700);
    check(policy.state() == ProximityState::Safe, "new exact recovery boundary must restore Safe");

    check(policy.configure(ProximityConfig{400, 0, 0, 1500}), "zero recovery margin must be allowed");
    policy.feed(true, 399, 800);
    policy.feed(true, 399, 900);
    check(policy.alert_active(), "zero recovery margin must still alert below the threshold");
    policy.feed(true, 400, 1000);
    policy.feed(true, 400, 1100);
    check(policy.state() == ProximityState::Safe, "zero margin must recover at threshold equality");
}

void changed_dwell_starts_at_the_next_fresh_sample()
{
    ProximityPolicy policy(ProximityConfig{400, 80, 1000, 1500});
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    policy.feed(true, 300, 200);
    policy.feed(true, 300, 900);
    check(policy.near_elapsed_ms() == 700, "changed-dwell test must have prior accumulated proximity");
    check(policy.configure(ProximityConfig{400, 80, 500, 1500}), "shorter valid dwell was rejected");
    check_reconfigured(policy);
    policy.expire(1000);
    check_reconfigured(policy);
    policy.feed(true, 300, 1100);
    check(policy.state() == ProximityState::Near && policy.near_elapsed_ms() == 0,
          "new dwell must start at fresh input, not at the configuration change or old near timestamp");
    policy.feed(true, 300, 1599);
    check(!policy.alert_active() && policy.near_elapsed_ms() == 499, "new dwell fired before its exact boundary");
    policy.feed(true, 300, 1600);
    check(policy.alert_active(), "full new dwell must alert at its exact boundary");
}

void switching_to_zero_dwell_waits_for_fresh_input()
{
    ProximityPolicy policy(five_second_config);
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    policy.feed(true, 300, 200);
    check(policy.state() == ProximityState::Near, "zero-dwell update test must start with pending proximity");
    check(policy.configure(ProximityConfig{}), "zero dwell configuration was rejected");
    check_reconfigured(policy);
    policy.expire(300);
    check_reconfigured(policy);
    policy.feed(true, 300, 400);
    check(policy.alert_active(), "zero dwell must take effect on the next fresh close sample");
}

void identical_configuration_preserves_dwell_and_alert()
{
    const ProximityConfig config{400, 80, 1000, 1500};
    ProximityPolicy policy(config);
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    policy.feed(true, 300, 200);
    policy.feed(true, 300, 900);
    check(policy.configure(config), "identical valid configuration was rejected");
    check(policy.state() == ProximityState::Near && policy.near_elapsed_ms() == 700,
          "identical configuration must not reset accumulated dwell");
    policy.feed(true, 300, 1199);
    check(!policy.alert_active(), "identical configuration must not accelerate dwell");
    policy.feed(true, 300, 1200);
    check(policy.alert_active(), "identical configuration must preserve the original dwell start");
    check(policy.configure(policy.config()), "configuration getter must be safe as configure input");
    check(policy.alert_active() && policy.near_elapsed_ms() == 1000,
          "identical configuration must not clear an active alert");
}

void legal_configuration_boundaries_are_accepted()
{
    const ProximityConfig boundaries[] = {
        {40, 0, 0, 1},
        {40, 3960, 60000, 60000},
        {4000, 0, 0, 1},
        {400, 3600, 60000, 60000},
    };
    for (const auto& config : boundaries) {
        ProximityPolicy policy;
        check(policy.configure(config), "legal configuration boundary was rejected");
        check_configuration(policy.config(), config);
    }
    ProximityPolicy policy;
    check(policy.configure(ProximityConfig{4000, 0, 0, 1500}), "maximum hardware threshold was rejected");
    policy.feed(true, 4000, 0);
    policy.feed(true, 4000, 100);
    policy.feed(true, 4000, 200);
    check(policy.state() == ProximityState::Safe, "4000 mm equality must remain safe");
    policy.feed(true, 3999, 300);
    policy.feed(true, 3999, 400);
    check(policy.alert_active(), "valid distance below maximum threshold must alert");
}

void invalid_configurations_leave_configuration_and_running_state_untouched()
{
    const ProximityConfig invalid[] = {
        {0, 80, 1000, 1500},
        {39, 80, 1000, 1500},
        {4001, 0, 1000, 1500},
        {65535, 0, 1000, 1500},
        {40, 3961, 1000, 1500},
        {4000, 1, 1000, 1500},
        {400, 65535, 1000, 1500},
        {400, 80, 60001, 1500},
        {400, 80, std::numeric_limits<std::uint32_t>::max(), 1500},
        {400, 80, 1000, 0},
        {400, 80, 1000, 60001},
        {400, 80, 1000, std::numeric_limits<std::uint32_t>::max()},
    };
    const ProximityConfig original{400, 80, 1000, 1500};
    for (const auto& config : invalid) {
        ProximityPolicy policy(original);
        policy.feed(true, 300, 0);
        policy.feed(true, 300, 100);
        policy.feed(true, 300, 200);
        policy.feed(true, 300, 400);
        check(!policy.configure(config), "invalid configuration was accepted");
        check_configuration(policy.config(), original);
        check(policy.state() == ProximityState::Near && policy.filtered_mm() == 300 &&
              policy.near_elapsed_ms() == 200, "rejected configuration changed live policy state");
        policy.feed(true, 300, 1200);
        check(policy.alert_active(), "rejected configuration reset the original near timestamp");
        check(!policy.configure(config), "invalid configuration was accepted during an alert");
        check(policy.alert_active(), "rejected configuration cleared an active alert");
        check_configuration(policy.config(), original);
    }
}

void configuration_change_preserves_partial_median_warmup()
{
    ProximityPolicy policy;
    policy.feed(true, 500, 0);
    policy.feed(true, 500, 100);
    check(policy.configure(ProximityConfig{600, 80, 0, 1500}), "partial-warmup configuration was rejected");
    check_reconfigured(policy);
    policy.feed(true, 500, 200);
    check(policy.filtered_mm() == 500 && policy.alert_active(),
          "configuration change must retain valid warmup samples without treating the change as a sample");
}

void changed_timeout_uses_the_retained_last_sample_time()
{
    for (const auto age : std::array<std::uint32_t, 2>{200, 201}) {
        ProximityPolicy policy;
        policy.feed(true, 300, 0);
        policy.feed(true, 300, 100);
        policy.feed(true, 300, 200);
        check(policy.configure(ProximityConfig{400, 80, 0, 200}), "valid timeout change was rejected");
        check_reconfigured(policy);
        policy.expire(200 + age);
        policy.feed(true, 300, 200 + age);
        if (age == 200) {
            check(policy.alert_active(), "exact new timeout must retain the median history and last sample time");
        } else {
            check_waiting(policy);
            policy.feed(true, 300, 501);
            check_waiting(policy);
            policy.feed(true, 300, 601);
            check(policy.alert_active(), "expired configured window must require three new samples");
        }
    }
}

void configuration_change_cannot_revive_an_expired_window()
{
    ProximityPolicy policy;
    policy.feed(true, 300, 0);
    policy.feed(true, 300, 100);
    policy.feed(true, 300, 200);
    policy.expire(1701);
    check_waiting(policy);
    check(policy.configure(ProximityConfig{600, 80, 500, 1500}), "configuration after expiry was rejected");
    policy.feed(true, 300, 1800);
    policy.feed(true, 300, 1900);
    check_waiting(policy);
    policy.feed(true, 300, 2000);
    check(policy.state() == ProximityState::Near && policy.near_elapsed_ms() == 0,
          "configuration after expiry must rebuild the median and start a new dwell");
    policy.feed(true, 300, 2499);
    check(!policy.alert_active(), "expired history must not shorten the configured dwell");
    policy.feed(true, 300, 2500);
    check(policy.alert_active(), "fresh full dwell after configuration and expiry must alert");
}

} // namespace

int main()
{
    const struct Test {
        const char* name;
        void (*run)();
    } tests[] = {
        {"default_configuration_alerts_on_the_third_valid_sample", default_configuration_alerts_on_the_third_valid_sample},
        {"default_hot_transition_alerts_without_additional_dwell", default_hot_transition_alerts_without_additional_dwell},
        {"default_threshold_equality_and_initial_dead_band_are_safe", default_threshold_equality_and_initial_dead_band_are_safe},
        {"default_invalid_measurement_clears_an_immediate_alert", default_invalid_measurement_clears_an_immediate_alert},
        {"three_samples_start_dwell", three_samples_start_dwell},
        {"stable_distance_remains_safe", stable_distance_remains_safe},
        {"dwell_boundary_and_latched_alert", dwell_boundary_and_latched_alert},
        {"threshold_equality_and_initial_dead_band_are_safe", threshold_equality_and_initial_dead_band_are_safe},
        {"hysteresis_restores_and_requires_full_new_dwell", hysteresis_restores_and_requires_full_new_dwell},
        {"isolated_close_spike_is_rejected", isolated_close_spike_is_rejected},
        {"isolated_far_spike_does_not_break_dwell", isolated_far_spike_does_not_break_dwell},
        {"invalid_measurement_clears_alert_and_dwell", invalid_measurement_clears_alert_and_dwell},
        {"zero_and_out_of_range_values_are_invalid_even_if_marked_valid", zero_and_out_of_range_values_are_invalid_even_if_marked_valid},
        {"fresh_sample_after_timeout_starts_a_new_window", fresh_sample_after_timeout_starts_a_new_window},
        {"expire_boundary_and_no_measurement_are_safe", expire_boundary_and_no_measurement_are_safe},
        {"expire_does_not_advance_dwell_without_fresh_samples", expire_does_not_advance_dwell_without_fresh_samples},
        {"uptime_rollover_preserves_continuous_proximity", uptime_rollover_preserves_continuous_proximity},
        {"uptime_rollover_preserves_timeout_boundary", uptime_rollover_preserves_timeout_boundary},
        {"explicit_invalidation_requires_new_samples", explicit_invalidation_requires_new_samples},
        {"custom_configuration_uses_the_same_latch_rules", custom_configuration_uses_the_same_latch_rules},
        {"live_threshold_changes_wait_for_a_fresh_sample", live_threshold_changes_wait_for_a_fresh_sample},
        {"live_hysteresis_changes_apply_the_new_recovery_boundary", live_hysteresis_changes_apply_the_new_recovery_boundary},
        {"changed_dwell_starts_at_the_next_fresh_sample", changed_dwell_starts_at_the_next_fresh_sample},
        {"switching_to_zero_dwell_waits_for_fresh_input", switching_to_zero_dwell_waits_for_fresh_input},
        {"identical_configuration_preserves_dwell_and_alert", identical_configuration_preserves_dwell_and_alert},
        {"legal_configuration_boundaries_are_accepted", legal_configuration_boundaries_are_accepted},
        {"invalid_configurations_leave_configuration_and_running_state_untouched", invalid_configurations_leave_configuration_and_running_state_untouched},
        {"configuration_change_preserves_partial_median_warmup", configuration_change_preserves_partial_median_warmup},
        {"changed_timeout_uses_the_retained_last_sample_time", changed_timeout_uses_the_retained_last_sample_time},
        {"configuration_change_cannot_revive_an_expired_window", configuration_change_cannot_revive_an_expired_window},
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
    std::cout << passed << '/' << total << " proximity policy tests passed\n";
    return passed == total ? 0 : 1;
}
