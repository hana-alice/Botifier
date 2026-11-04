#pragma once
#include <cstdint>

// Pinned IDF 5.5.4 packed wire layout; MSVC cannot consume GNU packed enums.
enum esp_chip_id_t : std::uint16_t { ESP_CHIP_ID_ESP32 = 0, ESP_CHIP_ID_ESP32S3 = 9 };
inline constexpr std::uint8_t ESP_IMAGE_HEADER_MAGIC = 0xe9;
inline constexpr unsigned ESP_IMAGE_MAX_SEGMENTS = 16;
#pragma pack(push, 1)
struct esp_image_header_t {
    std::uint8_t magic;
    std::uint8_t segment_count;
    std::uint8_t spi_mode;
    std::uint8_t spi_speed : 4;
    std::uint8_t spi_size : 4;
    std::uint32_t entry_addr;
    std::uint8_t wp_pin;
    std::uint8_t spi_pin_drv[3];
    esp_chip_id_t chip_id;
    std::uint8_t min_chip_rev;
    std::uint16_t min_chip_rev_full;
    std::uint16_t max_chip_rev_full;
    std::uint8_t reserved[4];
    std::uint8_t hash_appended;
};
#pragma pack(pop)
struct esp_image_segment_header_t { std::uint32_t load_addr; std::uint32_t data_len; };
static_assert(sizeof(esp_image_header_t) == 24);
static_assert(sizeof(esp_image_segment_header_t) == 8);

