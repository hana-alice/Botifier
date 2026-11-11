#pragma once
#include <cstdint>
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
constexpr int pdPASS = 1, pdTRUE = 1;
#define pdMS_TO_TICKS(ms) (ms)
