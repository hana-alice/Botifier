#pragma once

#include <esp_err.h>

namespace distance_sensor {

// Starts a process-lifetime sampler, independent of Mooncake/AI screen changes.
// A missing sensor is reported over telemetry and never prevents normal startup.
esp_err_t start();

// Read-only, thread-safe warning state. Stale samples never keep the face alert.
bool alert_active();

}  // namespace distance_sensor
