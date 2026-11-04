#pragma once
#include <cstddef>
#include <cstdint>

// Pinned IDF 5.5.4 wire structure, with all fields retained. No ESP behavior is
// simulated by these declarations; tests execute the production prefix checks.
inline constexpr std::uint32_t ESP_APP_DESC_MAGIC_WORD = 0xabcd5432;
struct esp_app_desc_t {
    std::uint32_t magic_word;
    std::uint32_t secure_version;
    std::uint32_t reserv1[2];
    char version[32];
    char project_name[32];
    char time[16];
    char date[16];
    char idf_ver[32];
    std::uint8_t app_elf_sha256[32];
    std::uint16_t min_efuse_blk_rev_full;
    std::uint16_t max_efuse_blk_rev_full;
    std::uint8_t mmu_page_size;
    std::uint8_t reserv3[3];
    std::uint32_t reserv2[18];
};
static_assert(sizeof(esp_app_desc_t) == 256);
const esp_app_desc_t* esp_app_get_description();

