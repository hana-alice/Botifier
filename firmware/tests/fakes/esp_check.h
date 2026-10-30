#pragma once

#include <esp_err.h>

// Preserve evaluation/early-return behavior; firmware logging is not under test.
#define ESP_RETURN_ON_ERROR(expression, tag, ...)       \
    do {                                              \
        (void)(tag);                                  \
        const esp_err_t fake_error = (expression);     \
        if (fake_error != ESP_OK) return fake_error;   \
    } while (0)
