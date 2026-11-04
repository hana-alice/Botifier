#include "protocol.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace codex_weekly::protocol {
namespace {

constexpr unsigned kMaxDepth = 12;
constexpr std::size_t kMaxTokenBytes = 8192;

bool whitespace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

// ArduinoJson also accepts single-quoted/unquoted keys and trailing input.
// Check the bounded JSON grammar first, leaving value extraction to the library.
class JsonSyntax {
public:
    explicit JsonSyntax(std::string_view input) : input_(input) {}
    bool complete()
    {
        const bool result = value(0);
        spaces();
        return result && at_ == input_.size();
    }
    std::size_t members() const { return members_; }

private:
    void spaces() { while (at_ < input_.size() && whitespace(input_[at_])) ++at_; }
    bool take(char expected)
    {
        spaces();
        if (at_ >= input_.size() || input_[at_] != expected) return false;
        ++at_;
        return true;
    }
    bool literal(std::string_view text)
    {
        if (input_.substr(at_, text.size()) != text) return false;
        at_ += text.size();
        return true;
    }
    bool hex4(unsigned& result)
    {
        result = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (at_ >= input_.size()) return false;
            const unsigned char c = static_cast<unsigned char>(input_[at_++]);
            const int digit = c >= '0' && c <= '9' ? c - '0' :
                              c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                              c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) return false;
            result = result * 16u + static_cast<unsigned>(digit);
        }
        return true;
    }
    bool string()
    {
        if (!take('"')) return false;
        while (at_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[at_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c == '\\') {
                if (at_ >= input_.size()) return false;
                const char escaped = input_[at_++];
                if (escaped == 'u') {
                    unsigned point;
                    if (!hex4(point) || (point >= 0xdc00 && point <= 0xdfff)) return false;
                    if (point >= 0xd800 && point <= 0xdbff) {
                        if (input_.substr(at_, 2) != "\\u") return false;
                        at_ += 2;
                        if (!hex4(point) || point < 0xdc00 || point > 0xdfff) return false;
                    }
                } else if (std::string_view("\"\\/bfnrt").find(escaped) == std::string_view::npos) return false;
            } else if (c >= 0x80) {
                unsigned point = c;
                unsigned bytes = 0, minimum = 0;
                if (c >= 0xc2 && c <= 0xdf) { point &= 0x1f; bytes = 1; minimum = 0x80; }
                else if (c >= 0xe0 && c <= 0xef) { point &= 0x0f; bytes = 2; minimum = 0x800; }
                else if (c >= 0xf0 && c <= 0xf4) { point &= 7; bytes = 3; minimum = 0x10000; }
                else return false;
                while (bytes--) {
                    if (at_ >= input_.size()) return false;
                    const auto next = static_cast<unsigned char>(input_[at_++]);
                    if ((next & 0xc0) != 0x80) return false;
                    point = (point << 6u) | (next & 0x3f);
                }
                if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
            }
        }
        return false;
    }
    bool digits()
    {
        const auto start = at_;
        while (at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9') ++at_;
        return at_ != start;
    }
    bool number()
    {
        if (at_ < input_.size() && input_[at_] == '-') ++at_;
        if (at_ >= input_.size()) return false;
        if (input_[at_] == '0') ++at_;
        else if (input_[at_] < '1' || input_[at_] > '9' || !digits()) return false;
        if (at_ < input_.size() && input_[at_] == '.') { ++at_; if (!digits()) return false; }
        if (at_ < input_.size() && (input_[at_] == 'e' || input_[at_] == 'E')) {
            ++at_;
            if (at_ < input_.size() && (input_[at_] == '+' || input_[at_] == '-')) ++at_;
            if (!digits()) return false;
        }
        return true;
    }
    bool value(unsigned depth)
    {
        spaces();
        if (at_ >= input_.size() || depth > kMaxDepth) return false;
        const char c = input_[at_];
        if (c == '{' || c == '[') {
            const bool object = c == '{';
            ++at_;
            if (take(object ? '}' : ']')) return true;
            do {
                if (object) {
                    if (!string() || !take(':')) return false;
                    ++members_;
                }
                if (!value(depth + 1u)) return false;
                if (take(object ? '}' : ']')) return true;
            } while (take(','));
            return false;
        }
        if (c == '"') return string();
        if (c == 't') return literal("true");
        if (c == 'f') return literal("false");
        if (c == 'n') return literal("null");
        return number();
    }

    std::string_view input_;
    std::size_t at_ = 0, members_ = 0;
};

std::size_t member_count(JsonVariantConst value)
{
    std::size_t result = 0;
    if (value.is<JsonObjectConst>()) {
        const auto object = value.as<JsonObjectConst>();
        result = object.size();
        for (JsonPairConst pair : object) result += member_count(pair.value());
    } else if (value.is<JsonArrayConst>()) {
        for (JsonVariantConst item : value.as<JsonArrayConst>()) result += member_count(item);
    }
    return result;
}

bool object_document(std::string_view input, JsonDocument& document)
{
    if (input.empty() || input.size() > kMaxResponseBytes) return false;
    JsonSyntax syntax(input);
    if (!syntax.complete() || deserializeJson(document, input.data(), input.size(),
                                            DeserializationOption::NestingLimit(kMaxDepth))) return false;
    // Duplicate keys collapse in ArduinoJson. This count also rejects aliases
    // written using different JSON escapes without a second key decoder.
    return document.is<JsonObjectConst>() && member_count(document.as<JsonVariantConst>()) == syntax.members();
}

bool safe_value(std::string_view text, std::size_t maximum)
{
    if (text.empty() || text.size() > maximum) return false;
    for (const unsigned char c : text) if (c <= 0x20 || c >= 0x7f) return false;
    return true;
}

bool string_field(JsonVariantConst value, std::size_t maximum, std::string& output)
{
    if (!value.is<const char*>()) return false;
    const auto text = value.as<JsonString>();
    const std::string_view view(text.c_str(), text.size());
    if (!safe_value(view, maximum)) return false;
    output.assign(view.data(), view.size());
    return true;
}

bool unsigned_field(JsonVariantConst value, std::uint32_t& output)
{
    if (!value.is<std::uint32_t>()) return false;
    output = value.as<std::uint32_t>();
    return true;
}

bool interval_field(JsonVariantConst value, std::uint32_t& output)
{
    if (value.isUnbound()) return true;
    std::uint32_t number = 0;
    if (!unsigned_field(value, number)) {
        if (!value.is<const char*>()) return false;
        const auto string = value.as<JsonString>();
        std::string_view text(string.c_str(), string.size());
        while (!text.empty() && whitespace(text.front())) text.remove_prefix(1);
        while (!text.empty() && whitespace(text.back())) text.remove_suffix(1);
        if (text.empty()) return false;
        for (const unsigned char c : text) {
            if (c < '0' || c > '9') return false;
            const auto digit = static_cast<std::uint32_t>(c - '0');
            if (number > (60u - digit) / 10u) return false;
            number = number * 10u + digit;
        }
    }
    if (number < 1 || number > 60) return false;
    output = number;
    return true;
}

int base64_digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '-' ? 62 : c == '_' ? 63 : -1;
}

bool decode_base64(std::string_view text, std::string& output)
{
    if (text.empty() || text.size() > kMaxTokenBytes || text.size() % 4u == 1u) return false;
    unsigned buffer = 0, bits = 0;
    output.clear();
    output.reserve(text.size() * 3u / 4u);
    for (const unsigned char c : text) {
        const int digit = base64_digit(c);
        if (digit < 0) return false;
        buffer = (buffer << 6u) | static_cast<unsigned>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>(buffer >> bits));
            buffer &= (1u << bits) - 1u;
        }
    }
    return buffer == 0;
}

bool jwt_payload(std::string_view token, JsonDocument& payload)
{
    if (!safe_value(token, kMaxTokenBytes)) return false;
    const auto first = token.find('.');
    if (first == std::string_view::npos) return false;
    const auto second = token.find('.', first + 1u);
    if (second == std::string_view::npos || token.find('.', second + 1u) != std::string_view::npos) return false;
    std::string decoded;
    JsonDocument header;
    if (!decode_base64(token.substr(0, first), decoded) || !object_document(decoded, header) ||
        !decode_base64(token.substr(second + 1u), decoded) ||
        !decode_base64(token.substr(first + 1u, second - first - 1u), decoded)) return false;
    return object_document(decoded, payload);
}

bool account_claim(const JsonDocument& payload, std::string& account)
{
    const auto auth = payload["https://api.openai.com/auth"];
    return auth.is<JsonObjectConst>() && string_field(auth["chatgpt_account_id"], 256, account);
}

} // namespace

bool parse_device_code(std::string_view json, DeviceCode& output)
{
    JsonDocument document;
    if (!object_document(json, document)) return false;
    DeviceCode result;
    if (!string_field(document["device_auth_id"], 512, result.device_auth_id) ||
        !interval_field(document["interval"], result.interval_seconds)) return false;
    const auto code = document["user_code"].as<JsonVariantConst>();
    const auto alias = document["usercode"].as<JsonVariantConst>();
    if (!code.isUnbound()) {
        if (!string_field(code, 31, result.user_code)) return false;
        if (!alias.isUnbound()) {
            std::string duplicate;
            if (!string_field(alias, 31, duplicate) || duplicate != result.user_code) return false;
        }
    } else if (!string_field(alias, 31, result.user_code)) return false;
    output = std::move(result);
    return true;
}

bool parse_authorization(std::string_view json, Authorization& output)
{
    JsonDocument document;
    if (!object_document(json, document)) return false;
    Authorization result;
    if (!string_field(document["authorization_code"], 4096, result.code) ||
        !string_field(document["code_verifier"], 4096, result.verifier)) return false;
    output = std::move(result);
    return true;
}

namespace {

bool parse_token_response(std::string_view json, std::uint32_t now, std::string_view old_refresh,
                          std::string_view old_account, const Tokens* old, Tokens& output)
{
    JsonDocument document;
    if (!object_document(json, document)) return false;
    Tokens result;
    const auto access_token = document["access_token"].as<JsonVariantConst>();
    const bool inherited_access = access_token.isUnbound() && old;
    if (inherited_access) {
        if (!old->access_token.empty() && !safe_value(old->access_token, kMaxTokenBytes)) return false;
        result.access_token = old->access_token;
        result.expires_at = old->expires_at;
    } else if (!string_field(access_token, kMaxTokenBytes, result.access_token)) return false;
    const auto refresh = document["refresh_token"].as<JsonVariantConst>();
    if (refresh.isUnbound()) {
        if (!safe_value(old_refresh, kMaxTokenBytes)) return false;
        result.refresh_token.assign(old_refresh.data(), old_refresh.size());
    } else if (!string_field(refresh, kMaxTokenBytes, result.refresh_token)) return false;

    const auto identity = document["id_token"].as<JsonVariantConst>();
    if (identity.isUnbound()) {
        if (!safe_value(old_account, 256)) return false;
        result.account_id.assign(old_account.data(), old_account.size());
    } else {
        std::string token;
        JsonDocument payload;
        if (!string_field(identity, kMaxTokenBytes, token) || !jwt_payload(token, payload) ||
            !account_claim(payload, result.account_id)) return false;
        const auto exp = payload["exp"].as<JsonVariantConst>();
        std::uint32_t ignored;
        if (!exp.isUnbound() && !unsigned_field(exp, ignored)) return false;
    }
    JsonDocument access;
    if (!inherited_access && jwt_payload(result.access_token, access)) {
        const auto exp = access["exp"].as<JsonVariantConst>();
        if (!exp.isUnbound() && (!unsigned_field(exp, result.expires_at) || result.expires_at <= now)) return false;
    }
    // Opaque access tokens have an unknown expiry. The service tracks the
    // successful refresh timestamp separately; no lifetime is fabricated here.
    output = std::move(result);
    return true;
}

} // namespace

bool parse_tokens(std::string_view json, std::uint32_t now, std::string_view old_refresh,
                  std::string_view old_account, Tokens& output)
{
    return parse_token_response(json, now, old_refresh, old_account, nullptr, output);
}

bool parse_refresh_tokens(std::string_view json, std::uint32_t now, const Tokens& old, Tokens& output)
{
    return parse_token_response(json, now, old.refresh_token, old.account_id, &old, output);
}

bool parse_usage(std::string_view json, std::uint32_t now, Usage& output)
{
    JsonDocument document;
    if (!object_document(json, document) || !document["rate_limit"].is<JsonObjectConst>()) return false;
    const auto rate = document["rate_limit"].as<JsonObjectConst>();
    Usage result;
    unsigned matches = 0;
    for (const char* name : {"primary_window", "secondary_window"}) {
        const auto window = rate[name];
        if (window.isUnbound() || window.isNull()) continue;
        if (!window.is<JsonObjectConst>()) return false;
        std::uint32_t seconds;
        if (!unsigned_field(window["limit_window_seconds"], seconds)) return false;
        if (seconds != 604800u) continue;
        std::uint32_t used, reset;
        if (++matches > 1 || !unsigned_field(window["used_percent"], used) || used > 100 ||
            !unsigned_field(window["reset_at"], reset) || reset <= now) return false;
        result.remaining_percent = 100 - static_cast<int>(used);
        result.reset_at = reset;
    }
    if (matches != 1) return false;
    output = result;
    return true;
}

std::string form_encode(std::string_view value)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') result.push_back(static_cast<char>(c));
        else {
            result.push_back('%');
            result.push_back(hex[c >> 4u]);
            result.push_back(hex[c & 15u]);
        }
    }
    return result;
}

bool refresh_rejected_permanently(int http_status, std::string_view json)
{
    if (http_status == 401) return true;
    if (http_status < 100 || http_status > 599) return false;
    JsonDocument document;
    if (!object_document(json, document)) return false;
    const auto error = document["error"].as<JsonVariantConst>();
    const auto source = error.is<const char*>() ? error : error.is<JsonObjectConst>() && error["code"].is<const char*>()
                         ? error["code"] : document["code"].as<JsonVariantConst>();
    std::string code;
    if (!string_field(source, 128, code)) return false;
    for (auto& c : code) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return (http_status == 400 && code == "invalid_grant") || code == "refresh_token_expired" ||
           code == "refresh_token_reused" || code == "refresh_token_invalidated";
}

} // namespace codex_weekly::protocol
