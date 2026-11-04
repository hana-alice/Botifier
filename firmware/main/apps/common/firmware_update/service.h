#pragma once
#include <esp_err.h>
#include <esp_http_server.h>

namespace firmware_update {

// Adds authenticated GET/POST /api/firmware to the existing local HTTP server.
// The server task owns upload I/O; no UI, audio or servo callbacks are involved.
esp_err_t register_handlers(httpd_handle_t server);

} // namespace firmware_update
