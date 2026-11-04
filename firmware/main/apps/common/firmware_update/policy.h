#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace firmware_update::detail {
inline constexpr std::size_t kPrefixBytes = 288;
inline constexpr std::size_t kChunkBytes = 4096;
inline constexpr std::uint32_t kUploadTimeoutMs = 120000;

inline int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
inline bool parse_sha256(std::string_view value, std::array<std::uint8_t, 32>& output)
{
    if (value.size() != 64) return false;
    std::array<std::uint8_t, 32> decoded{};
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const int high = hex_digit(value[i * 2]), low = hex_digit(value[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        decoded[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    output = decoded; return true;
}
inline bool matches_mac(std::string_view value, const std::array<std::uint8_t, 6>& expected)
{
    if (value.size() != 17) return false;
    unsigned difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const int high = hex_digit(value[i * 3]), low = hex_digit(value[i * 3 + 1]);
        if (high < 0 || low < 0 || (i < 5 && value[i * 3 + 2] != ':')) return false;
        difference |= static_cast<unsigned>((high << 4) | low) ^ expected[i];
    }
    return difference == 0;
}
inline bool equal_sha256(const std::array<std::uint8_t, 32>& left, const std::uint8_t* right)
{
    unsigned difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}
inline std::array<char, 65> hex_sha256(const std::uint8_t* bytes)
{
    constexpr char hex[] = "0123456789abcdef";
    std::array<char, 65> result{};
    for (std::size_t i = 0; i < 32; ++i) {
        result[i * 2] = hex[bytes[i] >> 4]; result[i * 2 + 1] = hex[bytes[i] & 15];
    }
    return result;
}

} // namespace firmware_update::detail
