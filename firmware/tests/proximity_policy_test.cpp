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

void three_samples_start_dwell()
{
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
        ProximityPolicy policy;
        for (std::uint32_t now = 0; now <= 8000; now += 1000) {
            policy.feed(true, distance, now);
        }
        check(policy.state() == ProximityState::Safe, "initial non-close distance must be Safe");
        check(!policy.alert_active(), "initial dead band must not alert");
    }
}

void hysteresis_restores_and_requires_full_new_dwell()
{
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
        ProximityPolicy policy;
        reach_alert(policy);
        policy.feed(true, distance, 8000);
        check_waiting(policy);
    }
    ProximityPolicy policy;
    policy.feed(true, 40, 0);
    policy.feed(true, 40, 100);
    policy.feed(true, 40, 200);
    check(policy.state() == ProximityState::Near, "40 mm hardware boundary is valid proximity");
}

void fresh_sample_after_timeout_starts_a_new_window()
{
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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
    ProximityPolicy policy;
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

} // namespace

int main()
{
    const struct Test {
        const char* name;
        void (*run)();
    } tests[] = {
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
