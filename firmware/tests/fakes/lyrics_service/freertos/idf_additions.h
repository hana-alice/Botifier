#pragma once
#include "FreeRTOS.h"
int xTaskCreatePinnedToCoreWithCaps(TaskFunction_t, const char*, unsigned, void*, unsigned,
                                  TaskHandle_t*, int, unsigned);
