#pragma once

#include <cstdint>

using TickType_t = uint32_t;

// 100 Hz intentionally rounds a 5 ms delay to zero, exercising the driver's
// at-least-one-tick fallback rather than assuming a 1 kHz scheduler.
constexpr uint32_t configTICK_RATE_HZ = 100;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>((ms) * configTICK_RATE_HZ / 1000))
