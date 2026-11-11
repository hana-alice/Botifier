#pragma once
typedef int esp_err_t;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NO_MEM = 0x101, ESP_ERR_INVALID_STATE = 0x103;
constexpr esp_err_t ESP_ERR_TIMEOUT = 0x107, ESP_ERR_INVALID_RESPONSE = 0x108;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_HTTP_EAGAIN = 0x7007;
