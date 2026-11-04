#pragma once
#include <cstdint>
using TickType_t = std::uint32_t;
inline constexpr std::uint32_t configTICK_RATE_HZ = 100;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>((ms) * configTICK_RATE_HZ / 1000))

