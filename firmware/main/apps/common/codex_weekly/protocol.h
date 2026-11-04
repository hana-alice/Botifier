#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace codex_weekly::protocol {

inline constexpr std::size_t kMaxResponseBytes = 32768;

struct DeviceCode {
    std::string device_auth_id;
    std::string user_code;
    std::uint32_t interval_seconds = 5;
};

struct Authorization {
    std::string code;
    std::string verifier;
};

struct Tokens {
    std::string access_token;
    std::string refresh_token;
    std::string account_id;
    std::uint32_t expires_at = 0;
};

struct Usage {
    int remaining_percent = -1;
    std::uint32_t reset_at = 0;
};

// Pure response parsing; no requests, credential storage, logging or clocks.
// On any failure the supplied output remains unchanged. JWT payload claims
// are routing metadata from a trusted TLS response, not signature validation.
bool parse_device_code(std::string_view json, DeviceCode& output);
bool parse_authorization(std::string_view json, Authorization& output);
bool parse_tokens(std::string_view json, std::uint32_t now,
                  std::string_view old_refresh, std::string_view old_account, Tokens& output);
// Refresh responses may omit any token. Preserve missing values, including the
// old access expiry, so a rotated refresh token survives an expired old access.
bool parse_refresh_tokens(std::string_view json, std::uint32_t now,
                          const Tokens& old, Tokens& output);
bool parse_usage(std::string_view json, std::uint32_t now, Usage& output);
std::string form_encode(std::string_view value);
bool refresh_rejected_permanently(int http_status, std::string_view json);

} // namespace codex_weekly::protocol
