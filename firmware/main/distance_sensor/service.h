#pragma once

#include <esp_err.h>
#include "proximity_policy.h"

namespace distance_sensor {

struct Snapshot {
    bool valid = false;
    int raw_mm = -1;
    int filtered_mm = -1;
    std::uint32_t sequence = 0;
    std::uint32_t sampled_at_ms = 0;
    std::uint32_t age_ms = 0;
    std::uint32_t near_elapsed_ms = 0;
    ProximityState proximity_state = ProximityState::Waiting;
    const char* sensor_state = "starting"; // Process-lifetime string literals.
    int raw_status = 255;
    esp_err_t error = ESP_OK;
    bool stale = true;
    ProximityConfig config;

    // Age a read-only copy. Looking at diagnostics never advances the policy.
    Snapshot at(std::uint32_t now_ms) const noexcept
    {
        Snapshot current = *this;
        current.age_ms = now_ms - sampled_at_ms;
        current.stale = stale || current.age_ms > config.sample_timeout_ms;
        if (current.stale) {
            current.valid = false;
            current.raw_mm = -1;
            current.filtered_mm = -1;
            current.near_elapsed_ms = 0;
            current.proximity_state = ProximityState::Waiting;
        }
        return current;
    }
};

// One coherent sample copied under a short critical section; never polls I2C.
Snapshot snapshot();

// Starts a process-lifetime sampler, independent of Mooncake/AI screen changes.
// A missing sensor is reported over telemetry and never prevents normal startup.
esp_err_t start();

// Read-only, thread-safe warning state. Stale samples never keep the face alert.
bool alert_active();

}  // namespace distance_sensor
