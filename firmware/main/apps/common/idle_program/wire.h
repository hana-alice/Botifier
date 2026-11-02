#pragma once

#include "policy.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

namespace idle_program::wire {

inline constexpr std::size_t kMaxFrameBytes = 512;
enum class Kind { Weekly, Config };
struct Message {
    Kind kind = Kind::Weekly;
    WeeklyUpdate weekly;
    Config config;
    bool save = false;
};

enum class LineResult { None, Ready, TooLong };
class Lines {
public:
    LineResult push(char value) noexcept
    {
        if (ready_) { size_ = 0; ready_ = false; }
        if (value == '\n') {
            if (discarding_) { discarding_ = false; size_ = 0; return LineResult::TooLong; }
            if (size_ == 0) return LineResult::None;
            data_[size_] = '\0';
            ready_ = true;
            return LineResult::Ready;
        }
        if (discarding_) return LineResult::None;
        if (size_ == kMaxFrameBytes) { discarding_ = true; size_ = 0; return LineResult::None; }
        data_[size_++] = value;
        return LineResult::None;
    }
    const char* data() const noexcept { return data_.data(); }
    std::size_t size() const noexcept { return size_; }
private:
    std::array<char, kMaxFrameBytes + 1> data_{};
    std::size_t size_ = 0;
    bool discarding_ = false, ready_ = false;
};

namespace detail {
class Cursor {
public:
    Cursor(const char* data, std::size_t size) noexcept : data_(data), size_(size) {}
    void whitespace() noexcept
    {
        while (at_ < size_ && (data_[at_] == ' ' || data_[at_] == '\t' || data_[at_] == '\r')) ++at_;
    }
    bool take(char value) noexcept
    {
        whitespace();
        if (at_ == size_ || data_[at_] != value) return false;
        ++at_;
        return true;
    }
    bool end() noexcept { whitespace(); return at_ == size_; }
    bool string(char* out, std::size_t capacity) noexcept
    {
        if (!take('"')) return false;
        std::size_t used = 0;
        while (at_ < size_ && data_[at_] != '"') {
            const auto value = static_cast<unsigned char>(data_[at_++]);
            if (value < 0x20 || value >= 0x7f || value == '\\' || used + 1 >= capacity) return false;
            out[used++] = static_cast<char>(value);
        }
        if (at_ == size_) return false;
        ++at_;
        out[used] = '\0';
        return true;
    }
    bool boolean(bool& value) noexcept
    {
        whitespace();
        if (literal("true", 4)) { value = true; return true; }
        if (literal("false", 5)) { value = false; return true; }
        return false;
    }
    bool integer(std::uint32_t& value) noexcept
    {
        whitespace();
        if (at_ == size_ || data_[at_] < '0' || data_[at_] > '9') return false;
        const auto begin = at_;
        value = 0;
        while (at_ < size_ && data_[at_] >= '0' && data_[at_] <= '9') {
            const auto digit = static_cast<std::uint32_t>(data_[at_++] - '0');
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10u) return false;
            value = value * 10u + digit;
        }
        return at_ - begin == 1 || data_[begin] != '0';
    }
private:
    bool literal(const char* text, std::size_t length) noexcept
    {
        if (size_ - at_ < length || std::memcmp(data_ + at_, text, length) != 0) return false;
        at_ += length;
        return true;
    }
    const char* data_;
    std::size_t size_, at_ = 0;
};
} // namespace detail

// Strict bounded flat JSON: only the protocol's ASCII keys, literal booleans,
// strings and unsigned decimal integers are accepted. No floating point coercion.
inline bool parse(const char* data, std::size_t size, const Config& current, Message& output) noexcept
{
    constexpr char prefix[] = "SC_IDLE ";
    if (!data || size > kMaxFrameBytes || size < sizeof(prefix) ||
        std::memcmp(data, prefix, sizeof(prefix) - 1) != 0) return false;
    detail::Cursor cursor(data + sizeof(prefix) - 1, size - (sizeof(prefix) - 1));
    Message result;
    result.config = current;
    std::uint32_t fields = 0;
    constexpr const char* keys[] = {"v", "type", "available", "sequence", "remaining_percent", "reset_at",
        "observed_at", "ttl_ms", "enabled", "eye_interval_ms", "rest_duration_ms", "idle_grace_ms",
        "pet_gap_ms", "weekly_duration_ms", "weekly_interval_ms", "save"};
    if (!cursor.take('{') || cursor.take('}')) return false;
    while (true) {
        char key[32]{};
        if (!cursor.string(key, sizeof(key)) || !cursor.take(':')) return false;
        unsigned field = 0;
        while (field < 16 && std::strcmp(key, keys[field]) != 0) ++field;
        if (field == 16 || (fields & (1u << field))) return false;
        fields |= 1u << field;
        std::uint32_t number = 0;
        if (field == 1) {
            char type[16]{};
            if (!cursor.string(type, sizeof(type))) return false;
            if (std::strcmp(type, "weekly") == 0) result.kind = Kind::Weekly;
            else if (std::strcmp(type, "config") == 0) result.kind = Kind::Config;
            else return false;
        } else if (field == 2 || field == 8 || field == 15) {
            bool value = false;
            if (!cursor.boolean(value)) return false;
            if (field == 2) result.weekly.available = value;
            else if (field == 8) result.config.enabled = value;
            else result.save = value;
        } else {
            if (!cursor.integer(number)) return false;
            switch (field) {
            case 0: if (number != 1) return false; break;
            case 3: result.weekly.sequence = number; break;
            case 4: if (number > 100) return false; result.weekly.remaining_percent = static_cast<int>(number); break;
            case 5: result.weekly.reset_at = number; break;
            case 6: result.weekly.observed_at = number; break;
            case 7: result.weekly.ttl_ms = number; break;
            case 9: result.config.eye_interval_ms = number; break;
            case 10: result.config.rest_duration_ms = number; break;
            case 11: result.config.idle_grace_ms = number; break;
            case 12: result.config.pet_gap_ms = number; break;
            case 13: result.config.weekly_duration_ms = number; break;
            case 14: result.config.weekly_interval_ms = number; break;
            default: return false;
            }
        }
        if (cursor.take('}')) break;
        if (!cursor.take(',')) return false;
    }
    if (!cursor.end() || (fields & 3u) != 3u) return false;
    if (result.kind == Kind::Weekly) {
        const auto required = result.weekly.available ? 0xffu : 0x0fu;
        if (fields != required || !valid_weekly(result.weekly)) return false;
    } else if ((fields & 0xfcu) != 0 || !valid_config(result.config)) return false;
    output = result;
    return true;
}

} // namespace idle_program::wire
