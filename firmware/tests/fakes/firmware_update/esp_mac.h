#pragma once
#include <esp_err.h>
#include <cstdint>
enum esp_mac_type_t { ESP_MAC_WIFI_STA };
esp_err_t esp_read_mac(std::uint8_t* output, esp_mac_type_t type);

