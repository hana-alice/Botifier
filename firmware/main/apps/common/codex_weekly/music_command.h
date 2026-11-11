#pragma once

#include <cstddef>
#include <string_view>

namespace codex_weekly::network_debug::detail {
inline constexpr std::size_t kMusicBodyLimit = 64;
inline constexpr long long kMusicTimeoutUs = 5000000;

// Intentionally only one JSON member and a literal boolean. A general object
// parser would silently coalesce duplicate keys before we could reject them.
inline bool parse_music_command(std::string_view body, bool& enabled)
{
    if (body.empty() || body.size() > kMusicBodyLimit) return false;
    std::size_t position = 0;
    const auto whitespace = [&] {
        while (position < body.size()) {
            const char c = body[position];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position;
        }
    };
    const auto consume = [&](std::string_view token) {
        whitespace();
        if (body.substr(position, token.size()) != token) return false;
        position += token.size(); return true;
    };
    if (!consume("{") || !consume("\"enabled\"") || !consume(":")) return false;
    bool value = false;
    if (consume("true")) value = true;
    else if (!consume("false")) return false;
    if (!consume("}")) return false;
    whitespace();
    if (position != body.size()) return false;
    enabled = value;
    return true;
}
} // namespace codex_weekly::network_debug::detail
