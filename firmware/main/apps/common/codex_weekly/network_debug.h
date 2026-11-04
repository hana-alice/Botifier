#pragma once
#include <esp_err.h>

namespace codex_weekly::network_debug {
inline constexpr int kPort = 8765;
// Read-only GET /api/status and authenticated local firmware upload, using
// X-Botifier-Key from CODEX settings.
// Starts in the background when Wi-Fi becomes available; never opens a socket
// from a display callback or exposes account credentials.
esp_err_t start();
} // namespace codex_weekly::network_debug
