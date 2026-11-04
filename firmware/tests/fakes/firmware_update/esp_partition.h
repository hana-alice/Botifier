#pragma once
#include <cstddef>
#include <cstdint>
inline constexpr std::uint8_t ESP_PARTITION_TYPE_APP = 0;
inline constexpr std::uint8_t ESP_PARTITION_TYPE_DATA = 1;
inline constexpr std::uint8_t ESP_PARTITION_SUBTYPE_APP_FACTORY = 0;
inline constexpr std::uint8_t ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10;
inline constexpr std::uint8_t ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11;
inline constexpr std::uint8_t ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10;
inline constexpr std::uint8_t ESP_PARTITION_SUBTYPE_APP_OTA_MAX = 0x20;
struct esp_partition_t {
    std::uint8_t type = ESP_PARTITION_TYPE_APP;
    std::uint8_t subtype = 0;
    std::uint32_t address = 0;
    std::uint32_t size = 0;
    char label[17]{};
    bool encrypted = false;
    bool readonly = false;
};

