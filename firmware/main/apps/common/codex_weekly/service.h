#pragma once

#include <esp_err.h>
#include <array>
#include <cstdint>
#include <string_view>

namespace codex_weekly {

enum class State { Starting, WaitingWifi, WaitingClock, SignedOut, StartingLogin, AwaitingLogin,
                   Refreshing, Fetching, Ready, Unavailable, LoginExpired, Error };
enum class Error { None, Network, Clock, Tls, Http, RateLimited, InvalidResponse, AuthExpired,
                   LoginDisabled, Storage, Internal };

// Deliberately excludes account identifiers, bearer/refresh tokens and raw HTTP
// bodies. The local debug key is shown only on the robot's settings page; the
// diagnostic response must select its own explicit public field allowlist.
struct Snapshot {
    State state = State::Starting;
    Error error = Error::None;
    bool wifi_connected = false;
    bool signed_in = false;
    bool login_pending = false;
    int remaining_percent = -1;
    int last_http_status = 0;
    std::uint32_t observed_at = 0;
    std::uint32_t reset_at = 0;
    std::uint32_t last_success_ms = 0;
    std::uint32_t next_poll_in_ms = 0;
    std::uint32_t login_expires_in_ms = 0;
    std::array<char, 32> user_code{};
    std::array<char, 16> ip_address{};
    std::array<char, 33> debug_key{};
};

// Startup is process-wide and does not depend on BOTIFIER being open. All UI
// actions enqueue work; no TLS, NVS or blocking operation runs in an LVGL callback.
esp_err_t start();
Snapshot snapshot();
void begin_login();
void request_refresh();
void logout();
bool authenticate_debug_key(std::string_view key);
const char* state_text(State value);
const char* error_text(Error value);

} // namespace codex_weekly
