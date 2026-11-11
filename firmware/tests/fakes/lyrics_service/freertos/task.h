#pragma once
#include "FreeRTOS.h"
void vTaskDelay(std::uint32_t);
std::uint32_t ulTaskNotifyTake(int, std::uint32_t);
void xTaskNotifyGive(TaskHandle_t);
