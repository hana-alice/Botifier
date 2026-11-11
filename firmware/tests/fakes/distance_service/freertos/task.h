#pragma once

#include <freertos/FreeRTOS.h>

BaseType_t xTaskCreate(TaskFunction_t function, const char* name, std::uint32_t stack_words,
                       void* parameter, unsigned priority, TaskHandle_t* handle);
void vTaskDelay(TickType_t ticks);
