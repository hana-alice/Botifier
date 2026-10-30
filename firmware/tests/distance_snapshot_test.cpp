#include <distance_sensor/service.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using distance_sensor::ProximityPolicy;
using distance_sensor::ProximityState;
using distance_sensor::Snapshot;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Snapshot alert_sample()
{
    Snapshot sample;
    sample.valid = true;
    sample.raw_mm = 318;
    sample.filtered_mm = 320;
    sample.sequence = 77;
    sample.sampled_at_ms = 10000;
    sample.near_elapsed_ms = 1200;
    sample.proximity_state = ProximityState::Alert;
    sample.sensor_state = "ranging";
    sample.raw_status = 9;
    sample.error = ESP_OK;
    sample.stale = false;
    return sample;
}

void check_cleared(const Snapshot& sample)
{
    check(sample.stale, "expired snapshot must be stale");
    check(!sample.valid, "stale snapshot must not be valid");
    check(sample.raw_mm == -1, "stale raw distance must be unavailable, not zero");
    check(sample.filtered_mm == -1, "stale filtered distance must be unavailable");
    check(sample.near_elapsed_ms == 0, "stale snapshot must clear proximity dwell");
    check(sample.proximity_state == ProximityState::Waiting, "stale snapshot must not retain an alert");
}

void initial_snapshot_never_invents_a_zero_distance()
{
    const Snapshot initial;
    check_cleared(initial.at(0));
    const auto later = initial.at(1200);
    check_cleared(later);
    check(later.age_ms == 1200, "initial sample age must be diagnostic only");
    check(later.sequence == 0 && later.sampled_at_ms == 0, "initial snapshot invented a measurement");
}

void fresh_valid_snapshot_retains_measurement_and_policy_metadata()
{
    const auto original = alert_sample();
    const auto current = original.at(10030);
    check(!current.stale && current.valid, "fresh measurement must stay valid");
    check(current.age_ms == 30, "fresh measurement has incorrect age");
    check(current.raw_mm == 318 && current.filtered_mm == 320, "fresh distances were lost");
    check(current.proximity_state == ProximityState::Alert, "fresh policy state was changed");
    check(current.near_elapsed_ms == 1200, "reading a snapshot must not advance dwell");
    check(current.sequence == 77 && current.sampled_at_ms == 10000, "fresh frame identity was changed");
    check(current.raw_status == 9 && current.error == ESP_OK, "fresh measurement status was changed");
    check(current.sensor_state == original.sensor_state, "sensor state literal was not preserved");
    check(current.config.threshold_mm == original.config.threshold_mm &&
          current.config.hysteresis_mm == original.config.hysteresis_mm &&
          current.config.near_duration_ms == original.config.near_duration_ms &&
          current.config.sample_timeout_ms == original.config.sample_timeout_ms,
          "aging a snapshot must preserve its policy configuration");
}

void default_timeout_preserves_1500ms_and_clears_1501ms()
{
    const auto sample = alert_sample();
    const auto boundary = sample.at(11500);
    check(boundary.age_ms == 1500, "wrong age at default timeout boundary");
    check(!boundary.stale && boundary.valid, "exactly 1500 ms must remain fresh");
    check(boundary.raw_mm == 318 && boundary.filtered_mm == 320, "boundary distances were cleared early");
    check(boundary.near_elapsed_ms == 1200 && boundary.proximity_state == ProximityState::Alert,
          "boundary policy state was cleared early");
    const auto expired = sample.at(11501);
    check(expired.age_ms == 1501, "wrong age just after default timeout");
    check_cleared(expired);
    check(expired.sequence == 77 && expired.sampled_at_ms == 10000,
          "expiry must preserve the last frame identity for diagnostics");
}

void aging_only_changes_the_returned_copy()
{
    auto original = alert_sample();
    const auto expired = original.at(12000);
    check_cleared(expired);
    check(original.valid && !original.stale, "at mutated the published validity flags");
    check(original.raw_mm == 318 && original.filtered_mm == 320, "at mutated the published distances");
    check(original.proximity_state == ProximityState::Alert && original.near_elapsed_ms == 1200,
          "at mutated the published policy state");
    check(original.age_ms == 0, "at mutated the published age");
    const auto fresh_again = original.at(10100);
    check(fresh_again.valid && !fresh_again.stale && fresh_again.age_ms == 100,
          "an earlier observation must not inherit a previous returned copy's expiry");
}

void fresh_invalid_optical_frames_keep_diagnostics_without_alerting()
{
    ProximityPolicy policy;
    for (std::uint32_t now = 0; now <= 3000; now += 100) {
        Snapshot reading;
        reading.sampled_at_ms = now;
        reading.sequence = now / 100;
        reading.valid = false;
        reading.raw_mm = 300; // Close distance, but the optical status is invalid.
        reading.raw_status = 4;
        reading.sensor_state = "ranging";
        reading.stale = false;
        const auto current = reading.at(now + 25);
        check(!current.stale && !current.valid, "fresh optical failure must remain invalid");
        check(current.raw_mm == 300 && current.raw_status == 4, "optical diagnostics were discarded");
        check(current.filtered_mm == -1 && current.near_elapsed_ms == 0 &&
              current.proximity_state == ProximityState::Waiting, "invalid optics invented filtered proximity");
        policy.feed(current.valid, static_cast<std::uint16_t>(current.raw_mm), now);
        check(!policy.alert_active(), "invalid optical frames triggered the default policy");
        check(policy.state() == ProximityState::Waiting && policy.near_elapsed_ms() == 0,
              "invalid optical frames accumulated default-policy dwell");
    }
    Snapshot failed_read;
    failed_read.sampled_at_ms = 3100;
    failed_read.sensor_state = "sensor_read_error";
    failed_read.error = ESP_ERR_INVALID_RESPONSE;
    failed_read.stale = false;
    check(failed_read.at(3125).error == ESP_ERR_INVALID_RESPONSE, "fresh sensor error code was discarded");
}

void custom_timeout_uses_its_own_exact_boundary()
{
    auto sample = alert_sample();
    sample.config.sample_timeout_ms = 200;
    const auto boundary = sample.at(10200);
    check(!boundary.stale && boundary.valid && boundary.age_ms == 200,
          "exact custom timeout must remain fresh");
    check(boundary.proximity_state == ProximityState::Alert, "custom boundary cleared an alert early");
    const auto expired = sample.at(10201);
    check_cleared(expired);
    check(expired.age_ms == 201, "custom timeout did not use the snapshot configuration");
}

void uint32_rollover_preserves_age_and_timeout_boundary()
{
    auto sample = alert_sample();
    sample.sampled_at_ms = std::numeric_limits<std::uint32_t>::max() - 100u;
    const auto current = sample.at(sample.sampled_at_ms + 125u);
    check(current.age_ms == 125 && current.valid && !current.stale, "rollover corrupted a fresh age");
    check(current.raw_mm == 318 && current.filtered_mm == 320, "rollover discarded a fresh measurement");
    const auto boundary = sample.at(sample.sampled_at_ms + 1500u);
    check(boundary.age_ms == 1500 && boundary.valid && !boundary.stale,
          "rollover corrupted the exact timeout boundary");
    const auto expired = sample.at(sample.sampled_at_ms + 1501u);
    check(expired.age_ms == 1501, "rollover corrupted the first stale age");
    check_cleared(expired);
}

void existing_stale_flag_cannot_be_revalidated_by_a_small_age()
{
    auto sample = alert_sample();
    sample.stale = true;
    const auto current = sample.at(10001);
    check(current.age_ms == 1, "wrong age for explicitly stale snapshot");
    check_cleared(current);
}

} // namespace

int main()
{
    const struct Test {
        const char* name;
        void (*run)();
    } tests[] = {
        {"initial_snapshot_never_invents_a_zero_distance", initial_snapshot_never_invents_a_zero_distance},
        {"fresh_valid_snapshot_retains_measurement_and_policy_metadata", fresh_valid_snapshot_retains_measurement_and_policy_metadata},
        {"default_timeout_preserves_1500ms_and_clears_1501ms", default_timeout_preserves_1500ms_and_clears_1501ms},
        {"aging_only_changes_the_returned_copy", aging_only_changes_the_returned_copy},
        {"fresh_invalid_optical_frames_keep_diagnostics_without_alerting", fresh_invalid_optical_frames_keep_diagnostics_without_alerting},
        {"custom_timeout_uses_its_own_exact_boundary", custom_timeout_uses_its_own_exact_boundary},
        {"uint32_rollover_preserves_age_and_timeout_boundary", uint32_rollover_preserves_age_and_timeout_boundary},
        {"existing_stale_flag_cannot_be_revalidated_by_a_small_age", existing_stale_flag_cannot_be_revalidated_by_a_small_age},
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
    std::cout << passed << '/' << total << " distance snapshot tests passed\n";
    return passed == total ? 0 : 1;
}
