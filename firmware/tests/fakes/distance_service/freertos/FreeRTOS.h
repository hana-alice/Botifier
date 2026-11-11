#pragma once

#include <cstdint>

using BaseType_t = int;
using TickType_t = std::uint32_t;
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
using portMUX_TYPE = int;

constexpr BaseType_t pdPASS = 1;
constexpr portMUX_TYPE portMUX_INITIALIZER_UNLOCKED = 0;

#define pdMS_TO_TICKS(milliseconds) (milliseconds)
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
