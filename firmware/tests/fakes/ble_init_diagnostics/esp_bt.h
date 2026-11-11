#pragma once
#include <esp_err.h>
#include <stdint.h>
typedef struct { uint32_t unchanged[4]; } esp_bt_controller_config_t;
typedef enum { ESP_BT_MODE_IDLE = 0, ESP_BT_MODE_BLE = 1, ESP_BT_MODE_BTDM = 3 } esp_bt_mode_t;
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *config);
esp_err_t esp_bt_controller_enable(esp_bt_mode_t mode);
