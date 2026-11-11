#pragma once

#include "types.h"
#include <algorithm>
#include <limits>
#include <string_view>

namespace lyrics::presentation {

constexpr std::size_t kTextLimit = 768;

inline bool wants_surface(const Snapshot& value, std::uint64_t)
{
    if (!value.enabled || !value.connected || value.link != LinkState::Ready ||
        !value.track_ready || value.track.title.empty()) return false;
    return true; // A paused, complete current track remains useful information.
}

struct CueEntrance { int offset_y = 0; std::uint8_t opacity = 255; };
constexpr std::uint32_t kCueEntranceMs = 360;

inline CueEntrance cue_entrance(std::uint32_t elapsed_ms)
{
    const auto remaining = kCueEntranceMs - std::min(kCueEntranceMs, elapsed_ms);
    const auto cube = static_cast<std::uint64_t>(remaining) * remaining * remaining;
    constexpr auto scale = static_cast<std::uint64_t>(kCueEntranceMs) * kCueEntranceMs * kCueEntranceMs;
    return {static_cast<int>((8 * cube + scale - 1) / scale),
            static_cast<std::uint8_t>(255 - 159 * cube / scale)};
}

inline std::uint32_t smooth_position(std::uint32_t from, std::uint32_t to, std::uint32_t elapsed_ms)
{
    constexpr std::uint32_t duration_ms = 120;
    const auto delta = static_cast<std::int64_t>(to) - from;
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(from) +
        delta * std::min(duration_ms, elapsed_ms) / duration_ms);
}

struct Group {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool empty() const { return begin == end; }
};

struct Cue {
    Group previous;
    Group current;
    Group next;
};

inline Group group_at(const std::vector<Line>& lines, std::size_t index)
{
    if (index >= lines.size()) return {};
    const auto timestamp = lines[index].time_ms;
    Group result{index, index + 1};
    while (result.begin > 0 && lines[result.begin - 1].time_ms == timestamp) --result.begin;
    while (result.end < lines.size() && lines[result.end].time_ms == timestamp) ++result.end;
    return result;
}

// LRC times are line starts. Equal-time translation lines are one cue, and no
// line is selected before its timestamp, including after a backwards seek.
inline Cue select_cue(const std::vector<Line>& lines, std::uint32_t position_ms)
{
    const auto after = std::upper_bound(lines.begin(), lines.end(), position_ms,
        [](std::uint32_t time, const Line& line) { return time < line.time_ms; });
    Cue result;
    const auto index = static_cast<std::size_t>(after - lines.begin());
    if (index == 0) {
        result.next = group_at(lines, 0);
        return result;
    }
    result.current = group_at(lines, index - 1);
    if (result.current.begin > 0) result.previous = group_at(lines, result.current.begin - 1);
    result.next = group_at(lines, result.current.end);
    return result;
}

struct Character {
    std::uint32_t codepoint = 0;
    std::size_t bytes = 1;
    bool valid = false;
};

inline Character character_at(std::string_view text, std::size_t offset)
{
    if (offset >= text.size()) return {};
    const auto lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80) return {lead, 1, true};
    const std::size_t bytes = lead >= 0xC2 && lead <= 0xDF ? 2 :
                              lead >= 0xE0 && lead <= 0xEF ? 3 :
                              lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    if (!bytes || offset + bytes > text.size()) return {};
    std::uint32_t codepoint = lead & (bytes == 2 ? 0x1Fu : bytes == 3 ? 0x0Fu : 0x07u);
    for (std::size_t i = 1; i < bytes; ++i) {
        const auto part = static_cast<unsigned char>(text[offset + i]);
        if ((part & 0xC0u) != 0x80u) return {};
        codepoint = (codepoint << 6) | (part & 0x3Fu);
    }
    if ((bytes == 3 && codepoint < 0x800) || (bytes == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) return {};
    return {codepoint, bytes, true};
}

inline void pop_character(std::string& text)
{
    if (text.empty()) return;
    auto index = text.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0u) == 0x80u) --index;
    text.resize(index);
}

// A second defensive bound keeps even a pathological group of translations
// small. Invalid bytes and control characters never reach LVGL labels.
inline std::string safe_text(std::string_view text, std::size_t limit = kTextLimit)
{
    std::string result;
    result.reserve(std::min(text.size(), limit));
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto character = character_at(text, offset);
        const auto bytes = character.valid ? character.bytes : 1;
        const bool whitespace = character.valid && (character.codepoint == '\t' || character.codepoint == '\r');
        const bool control = character.valid && character.codepoint != '\n' &&
            (character.codepoint < 0x20 || (character.codepoint >= 0x7F && character.codepoint <= 0x9F));
        if (!control || whitespace) {
            const auto part = !character.valid ? std::string_view("?") :
                              whitespace ? std::string_view(" ") : text.substr(offset, bytes);
            if (result.size() + part.size() > limit) break;
            result.append(part);
        }
        offset += bytes;
    }
    if (offset < text.size() && limit >= 3) {
        while (result.size() + 3 > limit) pop_character(result);
        result += "...";
    }
    return result;
}

inline std::string group_text(const std::vector<Line>& lines, Group group)
{
    std::string result;
    for (auto i = group.begin; i < std::min(group.end, lines.size()); ++i) {
        if (lines[i].text.empty()) continue;
        if (!result.empty()) result += '\n';
        const auto remaining = kTextLimit > result.size() ? kTextLimit - result.size() : 0;
        if (remaining < 3) break;
        result += safe_text(lines[i].text, remaining);
        if (result.size() >= kTextLimit) break;
    }
    return result;
}

// `measure` uses the actual production font. Wrapping is computed on cue
// changes only; labels never scroll or run a per-character timing animation.
template<class Measure>
std::string fit_text(std::string_view input, int width, std::size_t max_lines, Measure measure)
{
    if (width <= 0 || max_lines == 0) return {};
    const auto text = safe_text(input);
    std::vector<std::string> rows;
    std::string row;
    std::size_t offset = 0;
    while (offset < text.size() && rows.size() < max_lines) {
        const auto character = character_at(text, offset);
        if (character.codepoint == '\n') {
            rows.push_back(row);
            row.clear();
            ++offset;
            continue;
        }
        const auto part = text.substr(offset, character.bytes);
        if (row.empty() && part == " ") { offset += character.bytes; continue; }
        const auto candidate = row + part;
        if (!row.empty() && measure(candidate) > width) {
            const auto space = row.find_last_of(' ');
            if (space != std::string::npos && space != 0 && part != " ") {
                rows.push_back(row.substr(0, space));
                row.erase(0, space + 1);
            } else {
                rows.push_back(row);
                row.clear();
            }
            continue;
        }
        row = candidate;
        offset += character.bytes;
    }
    if (!row.empty() && rows.size() < max_lines) rows.push_back(row);
    if (offset < text.size() && !rows.empty()) {
        auto& last = rows.back();
        while (!last.empty() && (last.back() == ' ' || measure(last + "...") > width)) pop_character(last);
        if (measure("...") <= width) last += "...";
    }
    std::string result;
    for (const auto& line : rows) {
        if (!result.empty()) result += '\n';
        result += line;
    }
    return result;
}

} // namespace lyrics::presentation
