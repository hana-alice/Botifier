#include "protocol.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace lyrics::protocol {
namespace {
constexpr std::uint32_t kMaxDurationMs = 24u * 60u * 60u * 1000u;
constexpr unsigned kAllFields = 15;
constexpr std::size_t kMaxCachedBytes = 96 * 1024;

bool digits(std::string_view value, std::uint64_t maximum, std::uint64_t& result)
{
    if (value.empty()) return false;
    result = 0;
    for (const auto c : value) {
        if (c < '0' || c > '9' || result > (maximum - static_cast<unsigned>(c - '0')) / 10) return false;
        result = result * 10 + static_cast<unsigned>(c - '0');
    }
    return result <= maximum;
}

bool decimal(std::string_view value, double minimum, double maximum, double& result)
{
    if (value.empty() || value.size() > 32) return false;
    std::size_t at = 0;
    const bool negative = value[at] == '-';
    if (negative && ++at == value.size()) return false;
    double number = 0;
    unsigned integer_digits = 0;
    while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
        number = number * 10 + (value[at++] - '0');
        if (++integer_digits > 10) return false;
    }
    if (!integer_digits) return false;
    if (at < value.size() && value[at] == '.') {
        ++at;
        const auto start = at;
        double scale = 0.1;
        while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
            number += (value[at++] - '0') * scale;
            scale *= 0.1;
        }
        if (at == start) return false;
    }
    if (at != value.size()) return false;
    result = negative ? -number : number;
    return std::isfinite(result) && result >= minimum && result <= maximum;
}

bool duration(std::string_view value, std::uint32_t& result)
{
    double seconds;
    if (!decimal(value, 0.001, kMaxDurationMs / 1000.0, seconds)) return false;
    result = static_cast<std::uint32_t>(std::llround(seconds * 1000));
    return result > 0;
}

bool same_track(const Track& a, const Track& b)
{
    return a.title == b.title && a.artist == b.artist && a.album == b.album &&
           a.duration_ms == b.duration_ms && a.player == b.player;
}

// Strict JSON framing, UTF-8, number grammar, duplicate keys and escape syntax
// are checked before ArduinoJson extracts fields. It otherwise accepts relaxed
// JSON and silently keeps the last duplicate key.
class JsonSyntax {
public:
    explicit JsonSyntax(std::string_view input, unsigned maximum_depth = 6) : input_(input), maximum_depth_(maximum_depth) {}
    bool complete() { const bool ok = value(0); spaces(); return ok && at_ == input_.size(); }
    std::size_t members() const { return members_; }
private:
    void spaces() { while (at_ < input_.size() && std::string_view(" \t\r\n").find(input_[at_]) != std::string_view::npos) ++at_; }
    bool take(char c) { spaces(); if (at_ == input_.size() || input_[at_] != c) return false; ++at_; return true; }
    bool hex4(unsigned& point)
    {
        point = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (at_ == input_.size()) return false;
            const auto c = input_[at_++];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            point = point * 16 + static_cast<unsigned>(d);
        }
        return true;
    }
    bool string()
    {
        if (!take('"')) return false;
        std::size_t segment = at_;
        while (at_ < input_.size()) {
            const auto c = static_cast<unsigned char>(input_[at_++]);
            if (c == '"') return valid_text(input_.substr(segment, at_ - segment - 1), kMaxResponseBytes);
            if (c < 0x20) return false;
            if (c == '\\') {
                if (!valid_text(input_.substr(segment, at_ - segment - 1), kMaxResponseBytes) || at_ == input_.size()) return false;
                const auto escaped = input_[at_++];
                if (escaped == 'u') {
                    unsigned point;
                    if (!hex4(point) || (point >= 0xdc00 && point <= 0xdfff)) return false;
                    if (point >= 0xd800 && point <= 0xdbff) {
                        if (input_.substr(at_, 2) != "\\u") return false;
                        at_ += 2;
                        if (!hex4(point) || point < 0xdc00 || point > 0xdfff) return false;
                    }
                } else if (std::string_view("\"\\/bfnrt").find(escaped) == std::string_view::npos) return false;
                segment = at_;
            }
        }
        return false;
    }
    bool number()
    {
        if (at_ < input_.size() && input_[at_] == '-') ++at_;
        if (at_ == input_.size()) return false;
        if (input_[at_] == '0') ++at_;
        else {
            if (input_[at_] < '1' || input_[at_] > '9') return false;
            while (at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9') ++at_;
        }
        if (at_ < input_.size() && input_[at_] == '.') {
            const auto start = ++at_;
            while (at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9') ++at_;
            if (at_ == start) return false;
        }
        if (at_ < input_.size() && (input_[at_] == 'e' || input_[at_] == 'E')) {
            ++at_;
            if (at_ < input_.size() && (input_[at_] == '-' || input_[at_] == '+')) ++at_;
            const auto start = at_;
            while (at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9') ++at_;
            if (at_ == start) return false;
        }
        return true;
    }
    bool value(unsigned depth)
    {
        spaces();
        if (at_ == input_.size() || depth > maximum_depth_) return false;
        const auto c = input_[at_];
        if (c == '{' || c == '[') {
            ++at_;
            const bool object = c == '{';
            if (take(object ? '}' : ']')) return true;
            do {
                if (object) { if (!string() || !take(':')) return false; ++members_; }
                if (!value(depth + 1)) return false;
                if (take(object ? '}' : ']')) return true;
            } while (take(','));
            return false;
        }
        if (c == '"') return string();
        for (const auto literal : {std::string_view("true"), std::string_view("false"), std::string_view("null")}) {
            if (input_.substr(at_, literal.size()) == literal) { at_ += literal.size(); return true; }
        }
        return number();
    }
    std::string_view input_;
    std::size_t at_ = 0, members_ = 0;
    unsigned maximum_depth_ = 6;
};

std::size_t member_count(JsonVariantConst value)
{
    std::size_t result = 0;
    if (value.is<JsonObjectConst>()) {
        result = value.as<JsonObjectConst>().size();
        for (JsonPairConst item : value.as<JsonObjectConst>()) result += member_count(item.value());
    } else if (value.is<JsonArrayConst>()) for (JsonVariantConst item : value.as<JsonArrayConst>()) result += member_count(item);
    return result;
}

bool json_text(JsonVariantConst value, std::string_view& text, std::size_t maximum, bool multiline = false)
{
    if (!value.is<JsonString>()) return false;
    const auto str = value.as<JsonString>();
    text = std::string_view(str.c_str(), str.size());
    return valid_text(text, maximum, multiline);
}

bool json_document(std::string_view body, JsonDocument& document, std::uint8_t nesting_limit = 6)
{
    if (body.empty() || body.size() > kMaxResponseBytes) return false;
    JsonSyntax syntax(body, nesting_limit);
    return syntax.complete() &&
           !deserializeJson(document, body.data(), body.size(), DeserializationOption::NestingLimit(nesting_limit)) &&
           document.is<JsonObjectConst>() && syntax.members() == member_count(document.as<JsonVariantConst>());
}

#include "han_simplified.inc"

std::uint16_t simplified_han(std::uint16_t point)
{
    if (point < 0x3400 || point > 0x9fff) return point;
    const auto page = (point >> 8) - 0x34;
    const auto* end = kHanEntries + kHanPageStarts[page + 1];
    const auto* entry = std::lower_bound(kHanEntries + kHanPageStarts[page], end, point & 255u,
        [](const auto& bytes, unsigned key) { return bytes[0] < key; });
    return entry != end && (*entry)[0] == (point & 255u) ?
        static_cast<std::uint16_t>((*entry)[1] | ((*entry)[2] << 8)) : point;
}

std::string normalized(std::string_view value, bool fold_chinese = true)
{
    std::string result;
    bool space = false;
    for (std::size_t at = 0; at < value.size(); ++at) {
        const auto c = static_cast<unsigned char>(value[at]);
        if (c == ' ') { space = !result.empty(); continue; }
        if (space) result.push_back(' ');
        space = false;
        // Metadata is validated UTF-8 by the callers. Only mapped BMP Han
        // characters change; other bytes, display text and query text survive.
        if (fold_chinese && c >= 0xe3 && c <= 0xe9 && at + 2 < value.size()) {
            const auto b = static_cast<unsigned char>(value[at + 1]);
            const auto d = static_cast<unsigned char>(value[at + 2]);
            if ((b & 0xc0) == 0x80 && (d & 0xc0) == 0x80) {
                const auto point = static_cast<std::uint16_t>(((c & 15) << 12) | ((b & 63) << 6) | (d & 63));
                const auto target = simplified_han(point);
                if (target != point) {
                    result.push_back(static_cast<char>(0xe0 | (target >> 12)));
                    result.push_back(static_cast<char>(0x80 | ((target >> 6) & 63)));
                    result.push_back(static_cast<char>(0x80 | (target & 63)));
                    at += 2;
                    continue;
                }
            }
        }
        result.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c));
    }
    return result;
}

constexpr std::uint64_t kMaxCandidateId = INT64_MAX;

bool search_track(const Track& track)
{
    return track.duration_ms && track.duration_ms <= kMaxDurationMs &&
           valid_text(track.title, kMaxAttributeBytes) && !normalized(track.title).empty() &&
           valid_text(track.artist, kMaxAttributeBytes) && !normalized(track.artist).empty() &&
           valid_text(track.album, kMaxAttributeBytes);
}

bool catalog_track(const Track& track)
{
    return search_track(track) && !normalized(track.album).empty();
}

bool catalog_duration(JsonVariantConst value, std::uint32_t& milliseconds)
{
    if (!value.is<double>()) return false;
    const auto seconds = value.as<double>();
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > kMaxDurationMs / 1000.0) return false;
    milliseconds = static_cast<std::uint32_t>(std::llround(seconds * 1000));
    return milliseconds > 0;
}

bool catalog_duration_matches(std::uint32_t milliseconds, const Track& expected)
{
    const auto difference = milliseconds > expected.duration_ms ? milliseconds - expected.duration_ms : expected.duration_ms - milliseconds;
    return difference <= std::min<std::uint32_t>(10000, expected.duration_ms * 3u / 100u);
}

bool catalog_candidate(const Candidate& candidate)
{
    return candidate.valid && candidate.id && candidate.id <= kMaxCandidateId && candidate.access_key.empty() &&
           candidate.duration_ms && candidate.duration_ms <= kMaxDurationMs;
}

bool json_integer(JsonVariantConst value, std::uint64_t maximum, std::uint64_t& number)
{
    if (!value.is<std::uint64_t>()) return false;
    number = value.as<std::uint64_t>();
    return number > 0 && number <= maximum;
}

bool access_key(std::string_view value)
{
    if (value.empty() || value.size() > 128) return false;
    for (const auto c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                                    (c >= 'A' && c <= 'Z') || c == '_' || c == '-')) return false;
    return true;
}

bool artist_parts(std::string_view value, std::vector<std::string>& parts)
{
    std::size_t at = 0;
    for (std::size_t i = 0; i <= value.size();) {
        std::size_t separator = i == value.size() ? 0 : value[i] == ',' || value[i] == ';' ? 1 :
            value.substr(i, 3) == " & " ? 3 : 0;
        if (i == value.size() || separator) {
            auto part = normalized(value.substr(at, i - at));
            if (part.empty() || parts.size() == 8) return false;
            parts.push_back(std::move(part));
            i += separator; at = i;
            if (!separator) break;
        } else ++i;
    }
    std::sort(parts.begin(), parts.end());
    return !parts.empty();
}

bool same_artists(std::string_view actual, std::string_view expected)
{
    std::vector<std::string> a, b;
    return artist_parts(actual, a) && artist_parts(expected, b) && a == b;
}

struct Selection {
    Candidate candidate{true, 0, {}};
    bool album_match = false, ambiguous = false, recording_conflict = false;
    std::uint32_t difference = UINT32_MAX;
    unsigned quality = 0;
    std::string recording;

    void consider(Candidate next, bool album, std::uint32_t delta, unsigned score = 0, std::string identity = {})
    {
        if (!candidate.id || album > album_match || (album == album_match && delta < difference)) {
            candidate = std::move(next); album_match = album; difference = delta; ambiguous = false;
            quality = score; recording = std::move(identity); recording_conflict = false;
        } else if (album == album_match && delta == difference) {
            // KuGou ranks lyric editions, not recording identities. Score may
            // only break ties within the same raw identity and exact duration.
            if (next.duration_ms != candidate.duration_ms || identity != recording) {
                ambiguous = recording_conflict = true;
            } else if (!recording_conflict && candidate.access_key.empty() && next.access_key.empty()) {
                // Equivalent catalog editions can have separate IDs (for
                // example a single and a compilation). Keep one bounded
                // alternative instead of treating IDs as recording identity.
                if (next.id != candidate.id) {
                    if (next.id < candidate.id) {
                        next.fallback_id = candidate.id;
                        candidate = std::move(next);
                    } else if (!candidate.fallback_id || next.id < candidate.fallback_id) {
                        candidate.fallback_id = next.id;
                    }
                }
            } else if (!recording_conflict && score > quality) {
                candidate = std::move(next); quality = score; ambiguous = false;
            } else if (score == quality && (next.id != candidate.id || next.access_key != candidate.access_key)) {
                ambiguous = true;
            }
        }
    }
    Candidate result() { return ambiguous ? Candidate{true, 0, {}} : std::move(candidate); }
};

std::string album_title(std::string_view value)
{
    auto text = normalized(value);
    std::size_t open = std::string::npos, close = text.size();
    if (!text.empty() && text.back() == ')') { open = text.rfind('('); --close; }
    else if (text.size() >= 3 && text.compare(text.size() - 3, 3, "）") == 0) {
        open = text.rfind("（"); close -= 3;
    }
    if (open == std::string::npos) return text;
    const auto start = open + (text[open] == '(' ? 1 : 3);
    const auto tag = normalized(std::string_view(text).substr(start, close - start));
    if (tag != "album version" && tag != "album ver." && tag != "album ver" &&
        tag != "唱片版" && tag != "专辑版") return text;
    text.resize(open);
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

bool same_title(std::string_view actual, std::string_view expected, bool same_album)
{
    return normalized(actual) == normalized(expected) ||
        (same_album && album_title(actual) == album_title(expected));
}

void consider_track(Selection& selected, Candidate candidate, std::string_view title,
                    std::string_view artist, std::string_view album, std::uint64_t milliseconds,
                    const Track& expected, unsigned score = 0, std::string identity = {})
{
    const auto difference = milliseconds > expected.duration_ms ? milliseconds - expected.duration_ms :
                                                               expected.duration_ms - milliseconds;
    const bool same_album = !expected.album.empty() && normalized(album) == normalized(expected.album);
    if (!same_title(title, expected.title, same_album) || !same_artists(artist, expected.artist) || difference > 2000) return;
    selected.consider(std::move(candidate), same_album,
                      static_cast<std::uint32_t>(difference), score, std::move(identity));
}

Response lrc_response(Response result, std::string_view lrc, const Track& expected)
{
    if (lrc.empty()) { result.state = LyricState::NotFound; return result; }
    if (!expected.duration_ms || expected.duration_ms > kMaxDurationMs) return result;
    auto lines = std::make_shared<std::vector<Line>>();
    const bool timed = parse_lrc(lrc, expected.duration_ms, *lines);
    // Keep strict parse_lrc for callers requiring a compatible timeline. A
    // second full parse can retain useful text, but only without claiming sync.
    if (!timed && !parse_lrc(lrc, kMaxDurationMs, *lines)) return result;
    result.state = timed ? LyricState::Synced : LyricState::Unsynced;
    result.lines = std::move(lines);
    return result;
}

bool decode_base64(std::string_view encoded, std::string& decoded)
{
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (encoded.empty() || encoded.size() % 4 || encoded.size() > kMaxResponseBytes) return false;
    decoded.clear(); decoded.reserve(encoded.size() / 4 * 3);
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        unsigned values[4]{};
        const bool pad2 = encoded[i + 2] == '=', pad3 = encoded[i + 3] == '=';
        if ((pad2 && !pad3) || ((pad2 || pad3) && i + 4 != encoded.size())) return false;
        for (unsigned j = 0; j < 4; ++j) {
            if (j >= 2 && encoded[i + j] == '=') continue;
            const auto index = alphabet.find(encoded[i + j]);
            if (index == std::string_view::npos) return false;
            values[j] = static_cast<unsigned>(index);
        }
        if ((pad2 && (values[1] & 15)) || (pad3 && !pad2 && (values[2] & 3))) return false;
        decoded.push_back(static_cast<char>((values[0] << 2) | (values[1] >> 4)));
        if (!pad2) decoded.push_back(static_cast<char>((values[1] << 4) | (values[2] >> 2)));
        if (!pad3) decoded.push_back(static_cast<char>((values[2] << 6) | values[3]));
    }
    return valid_text(decoded, kMaxResponseBytes, true);
}

bool timestamp(std::string_view value, std::uint32_t& time_ms)
{
    const auto colon = value.find(':');
    if (colon == std::string_view::npos || colon < 1 || colon > 4) return false;
    std::uint64_t minutes;
    if (!digits(value.substr(0, colon), 1440, minutes)) return false;
    const auto rest = value.substr(colon + 1);
    if (rest.size() != 2 && rest.size() != 5 && rest.size() != 6) return false;
    std::uint64_t seconds;
    if (!digits(rest.substr(0, 2), 59, seconds)) return false;
    std::uint64_t fraction = 0;
    if (rest.size() > 2) {
        if (rest[2] != '.' || !digits(rest.substr(3), 999, fraction)) return false;
        if (rest.size() == 5) fraction *= 10;
    }
    const auto result = minutes * 60000 + seconds * 1000 + fraction;
    if (result > kMaxDurationMs) return false;
    time_ms = static_cast<std::uint32_t>(result);
    return true;
}
} // namespace

std::uint32_t simplify_display_character(std::uint32_t point)
{
    // A character-only preference must not turn 著名 into 着名.
    if (point == 0x8457) return point;
    // Windows' character table leaves this unambiguous traditional form alone.
    // Keep the display supplement out of metadata/version matching.
    if (point == 0x5f8c) return 0x540e; // 後 -> 后
    return point <= UINT16_MAX ? simplified_han(static_cast<std::uint16_t>(point)) : point;
}

bool phone_title_matches(std::string_view title, const std::vector<Line>& lines, std::uint32_t position_ms)
{
    const auto text = normalized(title);
    if (text.empty()) return false;
    for (std::size_t i = 0; i < lines.size();) {
        if (lines[i].time_ms > static_cast<std::uint64_t>(position_ms) + 750) break;
        auto end = i + 1;
        while (end < lines.size() && lines[end].time_ms == lines[i].time_ms) ++end;
        // BLE title and playback notifications need not arrive together. This
        // small window tolerates delivery skew, not another verse or a search
        // substring. Same-time translations are separate valid candidates.
        if (end == lines.size() || static_cast<std::uint64_t>(lines[end].time_ms) + 1500 > position_ms)
            for (auto row = i; row < end; ++row)
                if (normalized(lines[row].text) == text) return true;
        i = end;
    }
    return false;
}

bool valid_text(std::string_view value, std::size_t maximum, bool multiline)
{
    if (value.size() > maximum) return false;
    for (std::size_t at = 0; at < value.size();) {
        const auto c = static_cast<unsigned char>(value[at++]);
        if (c < 0x20 || c == 0x7f) {
            if (multiline && (c == '\r' || c == '\n' || c == '\t')) continue;
            return false;
        }
        if (c < 0x80) continue;
        unsigned point, count, minimum;
        if (c >= 0xc2 && c <= 0xdf) { point = c & 31; count = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { point = c & 15; count = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { point = c & 7; count = 3; minimum = 0x10000; }
        else return false;
        while (count--) {
            if (at == value.size()) return false;
            const auto next = static_cast<unsigned char>(value[at++]);
            if ((next & 0xc0) != 0x80) return false;
            point = (point << 6) | (next & 63);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) ||
            (point >= 0x80 && point <= 0x9f)) return false;
    }
    return true;
}

bool parse_playback(std::string_view value, bool& playing, double& rate, std::uint32_t& elapsed_ms)
{
    if (!valid_text(value, kMaxAttributeBytes)) return false;
    const auto first = value.find(','), last = value.rfind(',');
    if (first == std::string_view::npos || first == last) return false;
    std::uint64_t state;
    double new_rate, elapsed;
    if (!digits(value.substr(0, first), 3, state) ||
        !decimal(value.substr(first + 1, last - first - 1), -16, 16, new_rate) ||
        !decimal(value.substr(last + 1), 0, kMaxDurationMs / 1000.0, elapsed)) return false;
    playing = state != 0 && new_rate != 0;
    rate = playing ? new_rate : 0;
    elapsed_ms = static_cast<std::uint32_t>(std::llround(elapsed * 1000));
    return true;
}

std::string percent_encode(std::string_view value)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') result.push_back(static_cast<char>(c));
        else { result.push_back('%'); result.push_back(hex[c >> 4]); result.push_back(hex[c & 15]); }
    }
    return result;
}

std::string request_url(const Track& track, bool include_album)
{
    char seconds[24];
    std::snprintf(seconds, sizeof(seconds), "%lu.%03lu", static_cast<unsigned long>(track.duration_ms / 1000),
                  static_cast<unsigned long>(track.duration_ms % 1000));
    return "https://lrclib.net/api/get?track_name=" + percent_encode(track.title) +
           "&artist_name=" + percent_encode(track.artist) + (include_album ? "&album_name=" + percent_encode(track.album) : "") +
           "&duration=" + seconds;
}

std::string netease_search_url(const Track& track)
{
    if (!search_track(track)) return {};
    return "https://music.163.com/api/search/get?s=" + percent_encode(track.title + " " + track.artist) +
           "&type=1&limit=8&offset=0";
}

std::string netease_lyric_url(std::uint64_t id)
{
    return !id || id > kMaxCandidateId ? std::string{} :
        "https://music.163.com/api/song/lyric?id=" + std::to_string(id) + "&lv=-1&kv=-1&tv=-1";
}

std::string kugou_search_url(const Track& track)
{
    if (!search_track(track)) return {};
    return "https://lyrics.kugou.com/search?ver=1&man=yes&client=pc&keyword=" +
           percent_encode(track.title + " " + track.artist) + "&duration=" + std::to_string(track.duration_ms);
}

std::string kugou_lyric_url(const Candidate& candidate)
{
    if (!candidate.valid || !candidate.id || candidate.id > kMaxCandidateId || !access_key(candidate.access_key)) return {};
    return "https://lyrics.kugou.com/download?ver=1&client=pc&id=" + std::to_string(candidate.id) +
           "&fmt=lrc&charset=utf8&accesskey=" + percent_encode(candidate.access_key);
}

std::string lrclib_catalog_search_url(const Track& track)
{
    if (!catalog_track(track)) return {};
    return "https://lrclib.net/api/v2/search?track_name=" + percent_encode(track.title) +
           "&artist_name=" + percent_encode(track.artist) + "&album_name=" + percent_encode(track.album);
}

std::string lrclib_catalog_lyric_url(const Candidate& candidate)
{
    return catalog_candidate(candidate) ? "https://lrclib.net/api/get/" + std::to_string(candidate.id) : std::string{};
}

Candidate parse_lrclib_catalog_search(std::string_view body, const Track& expected)
{
    if (!catalog_track(expected) || body.empty() || body.size() > kMaxResponseBytes) return {};
    JsonSyntax syntax(body);
    JsonDocument document;
    // The catalog is the one array-valued endpoint. Existing source parsers
    // retain their object-only envelope contract and original depth budgets.
    if (!syntax.complete() || deserializeJson(document, body.data(), body.size(), DeserializationOption::NestingLimit(6)) ||
        !document.is<JsonArrayConst>() || syntax.members() != member_count(document.as<JsonVariantConst>())) return {};
    const auto records = document.as<JsonArrayConst>();
    if (records.size() > 20) return {};
    std::array<std::pair<std::uint64_t,std::uint32_t>,20> seen{};
    std::size_t count = 0;
    Selection selected;
    Candidate identity{true, 0, {}};
    bool identity_conflict = false, identity_has_lyrics = true, valid_record = records.size() == 0;
    for (JsonVariantConst item : records) {
        std::uint64_t id;
        std::uint32_t milliseconds;
        std::string_view title, artist, album;
        if (!item.is<JsonObjectConst>() || !json_integer(item["id"], kMaxCandidateId, id) ||
            !catalog_duration(item["duration"], milliseconds) ||
            !json_text(item["trackName"], title, kMaxAttributeBytes) || normalized(title).empty() ||
            !json_text(item["artistName"], artist, kMaxAttributeBytes) || normalized(artist).empty() ||
            !json_text(item["albumName"], album, kMaxAttributeBytes) || normalized(album).empty() ||
            !item["instrumental"].is<bool>() || !item["hasSyncedLyrics"].is<bool>()) continue;
        valid_record = true;
        for (std::size_t i = 0; i < count; ++i)
            if (seen[i].first == id && seen[i].second != milliseconds) return {true, 0, {}};
        seen[count++] = {id, milliseconds};
        const auto difference = milliseconds > expected.duration_ms ? milliseconds - expected.duration_ms : expected.duration_ms - milliseconds;
        if (!same_title(title, expected.title, difference <= 2000) || !same_artists(artist, expected.artist) ||
            normalized(album) != normalized(expected.album)) continue;
        // Count every exact identity, even plain-only/instrumental records:
        // having lyrics must not conceal another possible recording.
        if (!identity.id) identity = {true, id, {}, milliseconds};
        else if (identity.id != id) identity_conflict = true;
        const bool synced = !item["instrumental"].as<bool>() && item["hasSyncedLyrics"].as<bool>();
        identity_has_lyrics = identity_has_lyrics && synced;
        if (!synced || !catalog_duration_matches(milliseconds, expected)) continue;
        selected.consider({true, id, {}, milliseconds}, true, difference, 0,
            normalized(title, false) + '\0' + normalized(artist, false));
    }
    if (!valid_record) return {};
    auto result = selected.result();
    if (result.id || !identity.id || identity_conflict || !identity_has_lyrics) return result;
    // Some AMS totals differ from the catalog even when all identity fields
    // agree. Preserve the strict winner first; only a unique identity can
    // bypass that duration heuristic, never the downloaded timeline checks.
    identity.unique_identity = true;
    return identity;
}

Candidate parse_netease_search(std::string_view body, const Track& expected, bool allow_phone_identity)
{
    JsonDocument document;
    // Search includes album.artist.alias and artists[].alias containers. These
    // are ordinary provider metadata at seven container levels (including
    // the root), so budget eight here without widening other response types.
    if (!search_track(expected) || !json_document(body, document, 8) || !document["code"].is<int>() ||
        document["code"].as<int>() != 200 || !document["result"].is<JsonObjectConst>()) return {};
    const auto result = document["result"].as<JsonObjectConst>();
    if (result["songs"].isNull() && result["songCount"].is<unsigned>() && result["songCount"].as<unsigned>() == 0)
        return {true, 0, {}};
    if (!result["songs"].is<JsonArrayConst>()) return {};
    const auto songs = result["songs"].as<JsonArrayConst>();
    if (songs.size() > 8) return {};
    Selection selected, phone;
    bool valid_record = songs.size() == 0;
    for (JsonVariantConst item : songs) {
        std::uint64_t id, milliseconds;
        std::string_view title, album;
        if (!item.is<JsonObjectConst>() || !json_integer(item["id"], kMaxCandidateId, id) ||
            !json_integer(item["duration"], kMaxDurationMs, milliseconds) ||
            !json_text(item["name"], title, kMaxAttributeBytes) || normalized(title).empty() ||
            !item["album"].is<JsonObjectConst>() || !json_text(item["album"]["name"], album, kMaxAttributeBytes) ||
            !item["artists"].is<JsonArrayConst>()) continue;
        const auto artists = item["artists"].as<JsonArrayConst>();
        if (artists.size() == 0 || artists.size() > 8) continue;
        std::string joined, published;
        bool valid_artists = true;
        for (JsonVariantConst artist : artists) {
            std::string_view name;
            if (!artist.is<JsonObjectConst>() || !json_text(artist["name"], name, kMaxAttributeBytes) || normalized(name).empty()) {
                valid_artists = false; break;
            }
            if (!joined.empty()) { joined.push_back(';'); published.push_back('/'); }
            joined.append(name.data(), name.size());
            published.append(name.data(), name.size());
        }
        if (!valid_artists) continue;
        valid_record = true;
        const auto identity = normalized(title, false) + '\0' + normalized(joined, false);
        consider_track(selected, {true, id, {}, static_cast<std::uint32_t>(milliseconds)},
                       title, joined, album, milliseconds, expected, 0, identity);
        const auto difference = milliseconds > expected.duration_ms ? milliseconds - expected.duration_ms : expected.duration_ms - milliseconds;
        if (!allow_phone_identity || !phone_metadata_candidate(expected) || expected.album.empty() || difference > 2000 ||
            normalized(album) != normalized(expected.album)) continue;
        // NetEase's lock-screen lyrics put the current line in title and the
        // complete artist/title signature in artist. Reconstruct from source
        // fields; never split arbitrary artist names such as AC/DC.
        published += '/'; published.append(title.data(), title.size());
        const auto signature = normalized(expected.artist);
        bool compound = normalized(published) == signature;
        const auto translations = item["transNames"];
        if (!compound && translations.is<JsonArrayConst>() && translations.size() <= 8) {
            for (JsonVariantConst item_name : translations.as<JsonArrayConst>()) {
                std::string_view name;
                if (!json_text(item_name, name, kMaxAttributeBytes) || name.empty()) continue;
                if (normalized(published + " (" + std::string(name) + ")") == signature ||
                    normalized(published + "（" + std::string(name) + "）") == signature) {
                    compound = true; break;
                }
            }
        }
        if (compound) {
            Candidate candidate{true, id, {}, static_cast<std::uint32_t>(milliseconds)};
            candidate.phone_identity = true;
            phone.consider(std::move(candidate), true, static_cast<std::uint32_t>(difference), 0, identity);
        }
    }
    if (!valid_record) return {};
    return selected.candidate.id ? selected.result() : phone.result();
}

Candidate parse_kugou_search(std::string_view body, const Track& expected)
{
    JsonDocument document;
    if (!search_track(expected) || !json_document(body, document) || !document["status"].is<int>() ||
        document["status"].as<int>() != 200 || !document["candidates"].is<JsonArrayConst>()) return {};
    const auto candidates = document["candidates"].as<JsonArrayConst>();
    if (candidates.size() > 10) return {};
    Selection selected;
    bool valid_record = candidates.size() == 0;
    for (JsonVariantConst item : candidates) {
        std::uint64_t id, milliseconds;
        std::string_view title, artist, key, album, language;
        if (!item.is<JsonObjectConst>() || !json_integer(item["duration"], kMaxDurationMs, milliseconds) ||
            !json_text(item["song"], title, kMaxAttributeBytes) || normalized(title).empty() ||
            !json_text(item["singer"], artist, kMaxAttributeBytes) || normalized(artist).empty() ||
            !json_text(item["accesskey"], key, 128) || !access_key(key)) continue;
        if (item["id"].is<JsonString>()) {
            std::string_view value;
            if (!json_text(item["id"], value, 19) || !digits(value, kMaxCandidateId, id) || !id) continue;
        } else if (!json_integer(item["id"], kMaxCandidateId, id)) continue;
        if (!item["album"].isNull() && !json_text(item["album"], album, kMaxAttributeBytes)) continue;
        unsigned score = 0;
        if (!item["score"].isNull()) {
            if (!item["score"].is<unsigned>() || item["score"].as<unsigned>() > 100) continue;
            score = item["score"].as<unsigned>();
        }
        if (!item["language"].isNull() && !json_text(item["language"], language, 64)) continue;
        valid_record = true;
        const auto kind = normalized(language);
        if (kind == "伴奏" || kind == "纯音乐" || kind == "instrumental") continue;
        auto identity = normalized(title, false) + '\0' + normalized(artist, false) + '\0' + normalized(album, false);
        consider_track(selected, {true, id, std::string(key), static_cast<std::uint32_t>(milliseconds)},
                       title, artist, album, milliseconds, expected, score, std::move(identity));
    }
    return valid_record ? selected.result() : Candidate{};
}

bool parse_lrc(std::string_view body, std::uint32_t duration_ms, std::vector<Line>& lines)
{
    lines.clear();
    if (body.empty() || !duration_ms || duration_ms > kMaxDurationMs || !valid_text(body, kMaxResponseBytes, true)) return false;
    std::vector<Line> parsed;
    std::int64_t offset = 0;
    bool offset_seen = false;
    std::size_t at = 0;
    while (at < body.size()) {
        auto end = body.find('\n', at);
        if (end == std::string_view::npos) end = body.size();
        auto row = body.substr(at, end - at);
        at = end == body.size() ? end : end + 1;
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (row.empty() || row.find_first_not_of(" \t") == std::string_view::npos) continue;
        if (row.size() > kMaxAttributeBytes + 16 * 12) return false;
        if (row.substr(0, 8) == "[offset:") {
            if (offset_seen || row.back() != ']') return false;
            auto number = row.substr(8, row.size() - 9);
            bool negative = !number.empty() && number.front() == '-';
            if (negative || (!number.empty() && number.front() == '+')) number.remove_prefix(1);
            std::uint64_t absolute;
            if (!digits(number, 60000, absolute)) return false;
            offset = negative ? -static_cast<std::int64_t>(absolute) : static_cast<std::int64_t>(absolute);
            offset_seen = true;
            continue;
        }
        std::uint32_t times[16];
        unsigned count = 0;
        while (!row.empty() && row.front() == '[') {
            const auto close = row.find(']');
            if (close == std::string_view::npos) return false;
            std::uint32_t time;
            if (!timestamp(row.substr(1, close - 1), time)) {
                // Standard descriptive LRC tags are ignored; a malformed time
                // marker is not a metadata tag and invalidates the response.
                const auto tag = row.substr(1, close - 1);
                const auto separator = tag.find(':');
                if (count || close + 1 != row.size() || separator == std::string_view::npos || separator == 0) return false;
                for (const auto c : tag.substr(0, separator)) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
                row = {};
                break;
            }
            if (count == 16) return false;
            times[count++] = time;
            row.remove_prefix(close + 1);
        }
        if (!count) { if (!row.empty()) return false; continue; }
        if (!valid_text(row, kMaxAttributeBytes)) return false;
        while (!row.empty() && row.front() == ' ') row.remove_prefix(1);
        while (!row.empty() && row.back() == ' ') row.remove_suffix(1);
        for (unsigned i = 0; i < count; ++i) {
            if (parsed.size() == kMaxLines) return false;
            parsed.push_back({times[i], std::string(row)});
        }
    }
    std::size_t text_count = 0, text_bytes = 0;
    std::uint32_t first_text = UINT32_MAX, last_text = 0;
    for (auto& line : parsed) {
        const auto adjusted = static_cast<std::int64_t>(line.time_ms) + offset;
        if (!line.text.empty() && adjusted > static_cast<std::int64_t>(duration_ms) + 2000) return false;
        line.time_ms = static_cast<std::uint32_t>(std::max<std::int64_t>(0, adjusted));
        if (!line.text.empty()) {
            ++text_count; text_bytes += line.text.size();
            first_text = std::min(first_text, line.time_ms); last_text = std::max(last_text, line.time_ms);
        }
    }
    // A stray timestamp or one dirty fragment must not hide a useful status.
    if (text_count < 2 || text_bytes < 8 || last_text < first_text + 1000) return false;
    std::stable_sort(parsed.begin(), parsed.end(), [](const Line& a, const Line& b) { return a.time_ms < b.time_ms; });
    lines = std::move(parsed);
    return true;
}

Response parse_response(int status, std::string_view body, const Track& expected, std::string_view retry_after)
{
    Response result; result.http_status = status;
    if (status == 429) {
        result.state = LyricState::NetworkError;
        std::uint64_t seconds;
        result.retry_after_ms = digits(retry_after, 3600, seconds) ? static_cast<std::uint32_t>(std::max<std::uint64_t>(30, seconds) * 1000) : 60000;
        return result;
    }
    if (status == 404) { result.state = LyricState::NotFound; return result; }
    if (status != 200) { result.state = LyricState::NetworkError; return result; }
    JsonDocument document;
    if (!json_document(body, document)) return result;
    std::string_view title, artist, album;
    if (!json_text(document["trackName"], title, kMaxAttributeBytes) ||
        !json_text(document["artistName"], artist, kMaxAttributeBytes) ||
        !json_text(document["albumName"], album, kMaxAttributeBytes) || title.empty() || artist.empty() ||
        !document["duration"].is<double>() || !document["instrumental"].is<bool>()) return result;
    const auto seconds = document["duration"].as<double>();
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > kMaxDurationMs / 1000.0) return result;
    const bool same_album = !expected.album.empty() && normalized(album) == normalized(expected.album);
    if (!same_title(title, expected.title, same_album) || !same_artists(artist, expected.artist) ||
        (!expected.album.empty() && normalized(album) != normalized(expected.album)) ||
        std::abs(seconds * 1000 - expected.duration_ms) > 2000) {
        result.state = LyricState::NotFound; return result;
    }
    if (document["instrumental"].as<bool>()) { result.state = LyricState::Instrumental; return result; }
    if (document["syncedLyrics"].isNull()) { result.state = LyricState::NotFound; return result; }
    std::string_view lrc;
    if (!json_text(document["syncedLyrics"], lrc, kMaxResponseBytes, true)) return result;
    return lrc_response(std::move(result), lrc, expected);
}

Response parse_netease_lyrics(int status, std::string_view body, const Track& expected)
{
    if (status != 200) return parse_response(status, {}, expected);
    Response result; result.http_status = status;
    JsonDocument document;
    if (!search_track(expected) || !json_document(body, document) || !document["code"].is<int>() ||
        document["code"].as<int>() != 200) return result;
    for (const auto key : {"nolyric", "uncollected"}) {
        if (!document[key].isNull() && !document[key].is<bool>()) return result;
        if (document[key].as<bool>()) { result.state = LyricState::NotFound; return result; }
    }
    if (document["lrc"].isNull()) { result.state = LyricState::NotFound; return result; }
    std::string_view lrc;
    if (!document["lrc"].is<JsonObjectConst>() || !json_text(document["lrc"]["lyric"], lrc, kMaxResponseBytes, true)) return result;
    return lrc_response(std::move(result), lrc, expected);
}

Response parse_kugou_lyrics(int status, std::string_view body, const Track& expected)
{
    if (status != 200) return parse_response(status, {}, expected);
    Response result; result.http_status = status;
    JsonDocument document;
    std::string_view content;
    if (!search_track(expected) || !json_document(body, document) || !document["status"].is<int>() ||
        document["status"].as<int>() != 200 || !json_text(document["content"], content, kMaxResponseBytes)) return result;
    if (content.empty()) { result.state = LyricState::NotFound; return result; }
    std::string decoded;
    if (!decode_base64(content, decoded)) return result;
    return lrc_response(std::move(result), decoded, expected);
}

Response parse_lrclib_catalog_lyrics(int status, std::string_view body, const Track& expected,
                                    const Candidate& candidate, std::string_view retry_after)
{
    if (status != 200) return parse_response(status, {}, expected, retry_after);
    Response rejected; rejected.http_status = status;
    if (!catalog_track(expected) || !catalog_candidate(candidate) ||
        (!candidate.unique_identity && !catalog_duration_matches(candidate.duration_ms, expected))) return rejected;
    {
        // Release this document before parse_response allocates its lyric
        // document and timeline, keeping the two JSON copies out of the peak.
        JsonDocument document;
        std::uint64_t id;
        std::uint32_t milliseconds;
        if (!json_document(body, document) || !json_integer(document["id"], kMaxCandidateId, id) ||
            !catalog_duration(document["duration"], milliseconds)) return rejected;
        const auto difference = milliseconds > candidate.duration_ms ? milliseconds - candidate.duration_ms : candidate.duration_ms - milliseconds;
        if (id != candidate.id || difference > 1 ||
            (!candidate.unique_identity && !catalog_duration_matches(milliseconds, expected))) {
            rejected.state = LyricState::NotFound; return rejected;
        }
    }
    auto catalog = expected;
    catalog.duration_ms = candidate.duration_ms;
    auto response = parse_response(status, body, catalog, retry_after);
    if (response.state == LyricState::Instrumental) { response.state = LyricState::NotFound; return response; }
    if (response.state == LyricState::Synced && response.lines) {
        for (const auto& line : *response.lines) if (!line.text.empty() && line.time_ms > expected.duration_ms + 2000u) {
            response.state = LyricState::Unsynced; break;
        }
    }
    return response;
}

bool same_request_identity(const Track& a, const Track& b, bool phone_metadata)
{
    return (phone_metadata || a.title == b.title) && a.artist == b.artist && a.album == b.album &&
           a.duration_ms == b.duration_ms && a.player == b.player;
}

void Engine::remember_success()
{
    if (input_mode_ == InputMode::Probe || input_mode_ == InputMode::AwaitingTitle || input_mode_ == InputMode::Verified) return;
    if (latest_.state != LyricState::Instrumental &&
        (latest_.state != LyricState::Synced || !latest_.lines || latest_.lines->empty())) return;
    std::size_t bytes = latest_.track.player.capacity() + latest_.track.title.capacity() +
                        latest_.track.artist.capacity() + latest_.track.album.capacity() + 4;
    if (latest_.lines) {
        bytes += latest_.lines->capacity() * sizeof(Line);
        for (const auto& line : *latest_.lines) bytes += line.text.capacity() + 1;
    }
    if (bytes > kMaxCachedBytes) return;
    CachedLyrics previous;
    for (auto& entry : cache_) if (entry.bytes && !same_track(entry.track, latest_.track)) {
        previous = std::move(entry); break;
    }
    cache_[0] = {latest_.track, latest_.state, latest_.lines, bytes};
    cache_[1] = bytes + previous.bytes <= kMaxCachedBytes ? std::move(previous) : CachedLyrics{};
}

void Engine::restore_cached(std::uint64_t now)
{
    if (input_mode_ == InputMode::Probe || input_mode_ == InputMode::AwaitingTitle || input_mode_ == InputMode::Verified) return;
    if (seen_ != kAllFields || latest_.state == LyricState::Synced || latest_.state == LyricState::Instrumental) return;
    for (std::size_t i = 0; i < cache_.size(); ++i) if (cache_[i].bytes && same_track(cache_[i].track, latest_.track)) {
        if (i) std::swap(cache_[0], cache_[i]);
        latest_.state = cache_[0].state; latest_.lines = cache_[0].lines;
        latest_.http_status = 200; latest_.last_error = 0;
        latest_.phase = latest_.elapsed_ms = 0;
        latest_.socket_errno = latest_.tls_error = latest_.tls_flags = 0;
        // A cache hit already validates the entire exact signature. Partial
        // metadata never reaches here and cannot expose a previous song.
        metadata_at_ = now >= kMetadataSettleMs ? now - kMetadataSettleMs : 0;
        requested_ = true; next_request_at_ = 0;
        return;
    }
}

void Engine::invalidate(std::uint64_t now, bool clear_fields)
{
    ++latest_.generation;
    input_mode_ = InputMode::Normal; latest_.phone_lyrics = false;
    latest_.lines.reset(); latest_.state = LyricState::Waiting;
    latest_.http_status = latest_.last_error = 0; latest_.track_ready = false;
    latest_.phase = latest_.elapsed_ms = 0;
    latest_.socket_errno = latest_.tls_error = latest_.tls_flags = 0;
    seen_ = attempts_ = 0; requested_ = false; next_request_at_ = 0; metadata_at_ = now;
    if (clear_fields) {
        const auto player = latest_.track.player;
        latest_.track = {}; latest_.track.player = player;
        anchor_ms_ = 0; position_at_ = now; rate_ = 0; latest_.playing = false;
        latest_.paused_since_ms = now;
    }
}

void Engine::set_enabled(bool enabled, std::uint64_t now)
{
    if (latest_.enabled == enabled) return;
    latest_.enabled = enabled;
    latest_.connected = false;
    latest_.link = enabled ? LinkState::Pairing : LinkState::Off;
    has_queue_ = false;
    invalidate(now, true);
    latest_.track.player.clear(); latest_.last_event_ms = now;
}

void Engine::link(LinkState state, int error, std::uint64_t now)
{
    if (!latest_.enabled) return;
    if (state != LinkState::Ready && (latest_.connected || seen_)) {
        invalidate(now, true); has_queue_ = false;
    }
    latest_.link = state; latest_.connected = state == LinkState::Ready;
    latest_.last_error = error; latest_.last_event_ms = now;
}

void Engine::invalidate_track(std::uint64_t now)
{
    if (!latest_.enabled || !latest_.connected) return;
    invalidate(now, true); latest_.last_event_ms = now;
}

bool Engine::track_snapshot(const Track& track, std::uint64_t now)
{
    if (!latest_.enabled || !latest_.connected || track.duration_ms == 0 || track.duration_ms > kMaxDurationMs ||
        !valid_text(track.title, kMaxAttributeBytes) || !valid_text(track.artist, kMaxAttributeBytes) ||
        !valid_text(track.album, kMaxAttributeBytes) || !valid_text(track.player, kMaxAttributeBytes) ||
        normalized(track.title).empty() || normalized(track.artist).empty()) return false;
    Track complete = track;
    if (complete.player.empty()) complete.player = latest_.track.player;
    const bool stable = seen_ == kAllFields && same_request_identity(latest_.track, complete, true);
    const bool phone = stable && (input_mode_ == InputMode::Probe || input_mode_ == InputMode::AwaitingTitle ||
                                  input_mode_ == InputMode::Verified);
    const bool rejected = stable && input_mode_ == InputMode::Rejected;
    const bool changed = seen_ != kAllFields || !same_request_identity(latest_.track, complete, phone);
    if (seen_ == kAllFields && changed) invalidate(now, true);
    if (changed) input_mode_ = rejected ? InputMode::Rejected :
        phone_metadata_candidate(complete) ? InputMode::Probe : InputMode::Normal;
    latest_.track = std::move(complete);
    seen_ = kAllFields;
    if (changed) metadata_at_ = now;
    verify_phone_title(now);
    restore_cached(now);
    latest_.last_event_ms = now;
    return true;
}

bool Engine::attribute(std::uint8_t entity, std::uint8_t attr, std::string_view value, std::uint64_t now)
{
    if (!latest_.enabled || !latest_.connected || !valid_text(value, kMaxAttributeBytes)) return false;
    if (entity == 0 && attr == 1) {
        bool playing; double rate; std::uint32_t elapsed;
        if (!parse_playback(value, playing, rate, elapsed)) return false;
        if (latest_.playing && !playing) latest_.paused_since_ms = now;
        else if (!playing && !latest_.paused_since_ms) latest_.paused_since_ms = now;
        if (playing) latest_.paused_since_ms = 0;
        latest_.playing = playing; rate_ = rate; anchor_ms_ = elapsed; position_at_ = now;
    } else if (entity == 0 && attr == 0) {
        if (value.empty()) return false;
        if (latest_.track.player != value) { invalidate(now, true); latest_.track.player = std::string(value); }
    } else if (entity == 1 && attr == 0) {
        std::uint64_t index;
        if (!digits(value, UINT32_MAX, index)) return false;
        if (has_queue_ && queue_index_ != index) invalidate(now, true);
        has_queue_ = true; queue_index_ = static_cast<std::uint32_t>(index);
    } else if (entity == 2 && attr <= 3) {
        std::uint32_t milliseconds = 0;
        if (attr == 3 && !duration(value, milliseconds)) return false;
        if ((attr == 0 || attr == 2) && normalized(value).empty()) return false;
        const unsigned bit = 1u << attr;
        const bool fresh = !(seen_ & bit);
        std::string* field = attr == 0 ? &latest_.track.artist : attr == 1 ? &latest_.track.album : attr == 2 ? &latest_.track.title : nullptr;
        const bool changed = field ? *field != value : latest_.track.duration_ms != milliseconds;
        const bool phone_title = attr == 2 && seen_ == kAllFields &&
            (input_mode_ == InputMode::Probe || input_mode_ == InputMode::AwaitingTitle || input_mode_ == InputMode::Verified);
        if ((seen_ & bit) && changed && !phone_title) {
            const bool rejected = attr == 2 && input_mode_ == InputMode::Rejected;
            invalidate(now, true);
            if (rejected) input_mode_ = InputMode::Rejected;
        }
        if (field) *field = std::string(value); else latest_.track.duration_ms = milliseconds;
        seen_ |= bit;
        if ((fresh || changed) && !phone_title) metadata_at_ = now;
        if (seen_ == kAllFields && input_mode_ == InputMode::Normal && phone_metadata_candidate(latest_.track))
            input_mode_ = InputMode::Probe;
        restore_cached(now);
    } else return false;
    verify_phone_title(now);
    latest_.last_event_ms = now;
    return true;
}

bool Engine::metadata_ready(std::uint64_t now) const
{
    return latest_.enabled && latest_.connected && seen_ == kAllFields && now >= metadata_at_ && now - metadata_at_ >= kMetadataSettleMs;
}

std::uint32_t Engine::position(std::uint64_t now) const
{
    const double advanced = latest_.playing && now >= position_at_ ? (now - position_at_) * rate_ : 0;
    const double maximum = latest_.track.duration_ms ? latest_.track.duration_ms : kMaxDurationMs;
    return static_cast<std::uint32_t>(std::max(0.0, std::min(maximum, anchor_ms_ + advanced)));
}

Snapshot Engine::snapshot(std::uint64_t now) const
{
    auto result = latest_; result.position_ms = position(now); result.track_ready = metadata_ready(now);
    if (input_mode_ == InputMode::AwaitingTitle) {
        result.lines.reset(); result.state = LyricState::Loading;
    }
    return result;
}

void Engine::verify_phone_title(std::uint64_t now)
{
    if (input_mode_ != InputMode::AwaitingTitle) return;
    if (now >= next_request_at_) {
        invalidate(now, false); input_mode_ = InputMode::Rejected; seen_ = kAllFields;
    } else if (latest_.lines && phone_title_matches(latest_.track.title, *latest_.lines, position(now))) {
        input_mode_ = InputMode::Verified; latest_.phone_lyrics = true; next_request_at_ = 0;
    }
}

bool Engine::take_request(std::uint64_t now, Request& request)
{
    verify_phone_title(now);
    if (!metadata_ready(now) || requested_ || attempts_ >= 3 || now < next_request_at_) return false;
    request = {latest_.track, latest_.generation,
        input_mode_ == InputMode::Probe || input_mode_ == InputMode::Verified}; requested_ = true; ++attempts_;
    if (request.phone_metadata) next_request_at_ = now + 45000;
    latest_.state = LyricState::Loading;
    return true;
}

bool Engine::current(const Request& request) const
{
    return latest_.enabled && latest_.connected && request.generation == latest_.generation &&
        same_request_identity(request.track, latest_.track, request.phone_metadata &&
            (input_mode_ == InputMode::Probe || input_mode_ == InputMode::AwaitingTitle || input_mode_ == InputMode::Verified));
}

bool Engine::complete(const Request& request, Response response, std::uint64_t now)
{
    if (!current(request)) return false;
    if (response.phone_identity && !request.phone_metadata) {
        response.state = LyricState::Invalid; response.lines.reset();
    }
    const bool verified = request.phone_metadata && response.phone_identity && phone_metadata_candidate(request.track) && response.lines &&
        !response.lines->empty() && (response.state == LyricState::Synced || response.state == LyricState::Unsynced);
    const bool retryable = response.state == LyricState::NetworkError;
    if (request.phone_metadata && !verified && !retryable && !same_track(request.track, latest_.track)) {
        // A provisional title exemption may keep HTTP alive, but only source
        // proof permits publishing across those title changes. Retry the latest
        // complete raw metadata once through the normal path, without probing.
        invalidate(now, false); input_mode_ = InputMode::Rejected; seen_ = kAllFields;
        return false;
    }
    if (verified) {
        input_mode_ = InputMode::AwaitingTitle;
    }
    else if (request.phone_metadata) input_mode_ = retryable ? InputMode::Probe : InputMode::Rejected;
    latest_.phone_lyrics = false;
    latest_.state = response.state; latest_.http_status = response.http_status; latest_.last_error = response.error;
    latest_.phase = response.phase; latest_.elapsed_ms = response.elapsed_ms;
    latest_.socket_errno = response.socket_errno; latest_.tls_error = response.tls_error; latest_.tls_flags = response.tls_flags;
    latest_.lines = response.state == LyricState::Synced || response.state == LyricState::Unsynced ?
        std::move(response.lines) : nullptr;
    verify_phone_title(now);
    remember_success();
    if (response.state == LyricState::NetworkError && attempts_ < 3) {
        requested_ = false; next_request_at_ = now + std::max<std::uint32_t>(30000, response.retry_after_ms);
    }
    return true;
}

} // namespace lyrics::protocol
