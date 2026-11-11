#include "network_debug.h"
#include "service.h"
#include "music_command.h"
#include <apps/common/firmware_update/service.h>
#include <apps/common/idle_program/service.h>
#include <apps/common/lyrics/service.h>
#include <apps/common/lyrics/view.h>
#include <apps/common/runtime_diagnostics.h>
#include <apps/common/panic_diagnostics.h>
#include <distance_sensor/service.h>
#include <hal/hal.h>
#include <ArduinoJson.h>
#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <wifi_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <array>
#include <mutex>
#include <string>

namespace codex_weekly::network_debug {
namespace {
std::mutex lifecycle;
TaskHandle_t worker = nullptr;
httpd_handle_t server = nullptr;

// MUSIC_COMMAND_BEGIN: host tests compile this actual authentication/handler block.
bool authenticated(httpd_req_t* request)
{
    std::array<char, 33> key{};
    return httpd_req_get_hdr_value_len(request, "X-Botifier-Key") == 32 &&
           httpd_req_get_hdr_value_str(request, "X-Botifier-Key", key.data(), key.size()) == ESP_OK &&
           authenticate_debug_key(std::string_view(key.data(), 32));
}

esp_err_t music_reject(httpd_req_t* request, const char* status, const char* body)
{
    httpd_resp_set_status(request, status);
    httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    // Close instead of letting HTTPD drain an unchecked/unbounded rejected body.
    return ESP_FAIL;
}

esp_err_t music_handler(httpd_req_t* request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Connection", "close");
    httpd_resp_set_type(request, "application/json");
    if (!authenticated(request))
        return music_reject(request, "401 Unauthorized", "{\"error\":\"device_key\"}");
    if (httpd_req_get_hdr_value_len(request, "Transfer-Encoding"))
        return music_reject(request, "400 Bad Request", "{\"error\":\"transfer_encoding\"}");
    std::array<char, 17> type{};
    if (httpd_req_get_hdr_value_len(request, "Content-Type") != 16 ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type.data(), type.size()) != ESP_OK ||
        std::string_view(type.data(), 16) != "application/json")
        return music_reject(request, "415 Unsupported Media Type", "{\"error\":\"content_type\"}");
    if (!request->content_len || request->content_len > detail::kMusicBodyLimit)
        return music_reject(request, "413 Payload Too Large", "{\"error\":\"body_size\"}");
    std::array<char, 24> length_header{};
    const auto header_size = httpd_req_get_hdr_value_len(request, "Content-Length");
    if (!header_size || header_size >= length_header.size() ||
        httpd_req_get_hdr_value_str(request, "Content-Length", length_header.data(), length_header.size()) != ESP_OK)
        return music_reject(request, "400 Bad Request", "{\"error\":\"content_length\"}");
    std::size_t length = 0;
    for (std::size_t i = 0; i < header_size; ++i) {
        const char c = length_header[i];
        if (c < '0' || c > '9' || length > detail::kMusicBodyLimit)
            return music_reject(request, "400 Bad Request", "{\"error\":\"content_length\"}");
        length = length * 10 + static_cast<unsigned>(c - '0');
    }
    if (length != request->content_len)
        return music_reject(request, "400 Bad Request", "{\"error\":\"content_length\"}");

    const auto deadline = esp_timer_get_time() + detail::kMusicTimeoutUs;
    const int socket = httpd_req_to_sockfd(request);
    struct SocketTimeout {
        int socket;
        timeval saved{};
        bool valid = false;
        ~SocketTimeout() { if (valid) setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &saved, sizeof(saved)); }
    } timeout{socket};
    socklen_t timeout_size = sizeof(timeout.saved);
    timeout.valid = socket >= 0 &&
        getsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout.saved, &timeout_size) == 0;
    if (!timeout.valid)
        return music_reject(request, "500 Internal Server Error", "{\"error\":\"socket_timeout\"}");
    std::array<char, detail::kMusicBodyLimit> body{};
    std::size_t used = 0;
    while (used < length) {
        const auto remaining = deadline - esp_timer_get_time();
        if (remaining <= 0)
            return music_reject(request, "408 Request Timeout", "{\"error\":\"body_timeout\"}");
        // Bound the entire body read to five seconds, including fragmented input.
        const timeval wait{static_cast<long>(remaining / 1000000), static_cast<long>(remaining % 1000000)};
        if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait)) != 0)
            return music_reject(request, "500 Internal Server Error", "{\"error\":\"socket_timeout\"}");
        const int got = httpd_req_recv(request, body.data() + used, length - used);
        if (got == HTTPD_SOCK_ERR_TIMEOUT || esp_timer_get_time() >= deadline)
            return music_reject(request, "408 Request Timeout", "{\"error\":\"body_timeout\"}");
        if (got <= 0 || static_cast<std::size_t>(got) > length - used)
            return music_reject(request, "400 Bad Request", "{\"error\":\"body_closed\"}");
        used += static_cast<std::size_t>(got);
    }
    bool enabled = false;
    if (!detail::parse_music_command(std::string_view(body.data(), used), enabled))
        return music_reject(request, "400 Bad Request", "{\"error\":\"command\"}");
    lyrics::set_enabled(enabled);
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_send(request, "{\"accepted\":true}", HTTPD_RESP_USE_STRLEN);
}
// MUSIC_COMMAND_END

void diagnostic_boot(JsonObject output, const botifier_diag_boot_t& boot)
{
    output["valid"] = boot.valid;
    output["boot"] = boot.boot_counter;
    output["reset"] = boot.reset_reason;
    auto lanes = output["lanes"].to<JsonArray>();
    for (const auto& lane : boot.lanes) {
        // Fixed positions keep the authenticated response bounded:
        // valid, seq, stage, error, marks, sample_stage, sample_mark,
        // sample_uptime_ms, internal_free, internal_largest, internal_minimum.
        auto values = lanes.add<JsonArray>();
        values.add(lane.valid); values.add(lane.sequence); values.add(lane.stage); values.add(lane.error);
        values.add(lane.marks); values.add(lane.sample_stage); values.add(lane.sample_mark);
        values.add(lane.sample_uptime_ms); values.add(lane.free_internal);
        values.add(lane.largest_internal); values.add(lane.minimum_internal);
    }
}

// PANIC_STATUS_BEGIN: independently versioned, fixed numeric fields only.
void panic_record(JsonObject output, const botifier_panic_record_t& record)
{
    output["valid"] = record.valid;
    output["info_valid"] = record.info_valid;
    output["core"] = record.core;
    output["exception"] = record.exception;
    output["addr"] = record.addr;
    output["pseudo_excause"] = record.pseudo_excause;
    output["aborted"] = record.aborted;
    output["frame_valid"] = record.frame_valid;
    output["pc"] = record.pc;
    output["exccause"] = record.exccause;
    output["excvaddr"] = record.excvaddr;
    output["a0"] = record.a0;
    output["a1"] = record.a1;
}
// PANIC_STATUS_END

esp_err_t status_handler(httpd_req_t* request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    if (!authenticated(request)) {
        return httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, "Device key required");
    }
    // Select fields explicitly. Never serialize the account-service Snapshot
    // wholesale: its device-only debug key and login code must stay private.
    const auto quota = snapshot();
    const auto idle = idle_program::snapshot();
    const auto distance = distance_sensor::snapshot();
    const auto music = lyrics::snapshot();
    JsonDocument doc;
    doc["v"] = 1;
    doc["uptime_ms"] = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    doc["firmware"] = esp_app_get_description()->version;
    doc["wifi_connected"] = quota.wifi_connected;
    doc["reset_reason"] = static_cast<int>(esp_reset_reason());
    int battery = -1;
    bool charging = false, discharging = false;
    const bool battery_valid = GetHAL().getBatteryStatus(battery, charging, discharging);
    auto power = doc["power"].to<JsonObject>();
    power["valid"] = battery_valid;
    power["battery_percent"] = battery_valid ? battery : -1;
    power["charging"] = battery_valid && charging;
    power["discharging"] = battery_valid && discharging;
    auto weekly = doc["weekly"].to<JsonObject>();
    weekly["source"] = "device_wifi";
    weekly["state"] = state_text(quota.state);
    weekly["error"] = error_text(quota.error);
    weekly["http_status"] = quota.last_http_status;
    weekly["signed_in"] = quota.signed_in;
    weekly["remaining_percent"] = quota.remaining_percent;
    weekly["observed_at"] = quota.observed_at;
    weekly["reset_at"] = quota.reset_at;
    weekly["next_poll_in_ms"] = quota.next_poll_in_ms;
    weekly["last_success_ms"] = quota.last_success_ms;
    auto programmes = doc["idle"].to<JsonObject>();
    programmes["active"] = idle.active;
    programmes["blocked"] = idle.blocked;
    programmes["program"] = idle_program::program_text(idle.program);
    programmes["eye_phase"] = idle_program::eye_phase_text(idle.eye_phase);
    programmes["eye_remaining_ms"] = idle.eye_remaining_ms;
    programmes["phase_elapsed_ms"] = idle.phase_elapsed_ms;
    programmes["completed_rests"] = idle.completed_rests;
    programmes["weekly_status"] = idle_program::weekly_status_text(idle.weekly_status);
    auto ranging = doc["distance"].to<JsonObject>();
    ranging["valid"] = distance.valid;
    ranging["stale"] = distance.stale;
    ranging["distance_mm"] = distance.filtered_mm;
    ranging["age_ms"] = distance.age_ms;
    ranging["sensor_state"] = distance.sensor_state;
    ranging["error"] = static_cast<int>(distance.error);
    ranging["monitoring"] = distance.schedule.enabled();
    ranging["schedule"] = distance_sensor::schedule_name(distance.schedule.mode);
    ranging["clock_available"] = distance.schedule.clock_available;
    ranging["local_weekday"] = distance.schedule.local_weekday;
    ranging["local_minute"] = distance.schedule.local_minute;
    ranging["manual_remaining_ms"] = distance.schedule.manual_remaining_ms;
    // Connection/timing only: no phone identity, track names or lyric bodies.
    auto media = doc["music"].to<JsonObject>();
    media["enabled"] = music.enabled;
    media["connected"] = music.connected;
    media["playing"] = music.playing;
    media["track_ready"] = music.track_ready;
    media["link"] = lyrics::link_text(music.link);
    media["lyrics"] = lyrics::state_text(music.state);
    media["generation"] = music.generation;
    media["position_ms"] = music.position_ms;
    media["line_count"] = music.lines ? music.lines->size() : 0;
    media["error"] = music.last_error;
    media["http_status"] = music.http_status;
    media["fetch_phase"] = music.phase;
    media["fetch_elapsed_ms"] = music.elapsed_ms;
    media["socket_errno"] = music.socket_errno;
    media["tls_error"] = music.tls_error;
    media["tls_flags"] = music.tls_flags;
    // Explicit opt-in after the existing device-key authentication. Normal
    // health/OTA polling remains numeric and never includes listening metadata.
    std::array<char, 2> music_detail{};
    if (httpd_req_get_hdr_value_len(request, "X-Botifier-Music-Detail") == 1 &&
        httpd_req_get_hdr_value_str(request, "X-Botifier-Music-Detail", music_detail.data(), music_detail.size()) == ESP_OK &&
        music_detail[0] == '1') {
        auto track = doc["music_track"].to<JsonObject>();
        track["player"] = music.track.player;
        track["title"] = music.track.title;
        track["artist"] = music.track.artist;
        track["album"] = music.track.album;
        track["duration_ms"] = music.track.duration_ms;
    }
    const auto render = lyrics::render_stats();
    auto rendering = doc["music_render"].to<JsonObject>();
    rendering["active"] = render.active;
    rendering["elapsed_ms"] = render.elapsed_ms;
    rendering["cycles"] = render.cycles;
    rendering["render_ms"] = render.render_ms;
    rendering["max_render_ms"] = render.max_render_ms;
    rendering["flushed_pixels"] = render.flushed_pixels;
    rendering["scaled_glyph_rasters"] = render.scaled_glyph_rasters;
    rendering["cue_rasters"] = render.cue_rasters;
    rendering["cache_failures"] = render.cache_failures;
    const auto diagnostic = botifier_diag_snapshot();
    auto diagnostics = doc["diagnostics"].to<JsonObject>();
    diagnostics["v"] = diagnostic.version;
    const auto capabilities = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    diagnostics["free"] = heap_caps_get_free_size(capabilities);
    diagnostics["largest"] = heap_caps_get_largest_free_block(capabilities);
    diagnostics["minimum"] = heap_caps_get_minimum_free_size(capabilities);
    diagnostic_boot(diagnostics["current"].to<JsonObject>(), diagnostic.current);
    diagnostic_boot(diagnostics["previous"].to<JsonObject>(), diagnostic.previous);
    const auto panic = botifier_panic_snapshot();
    auto panic_status = doc["panic"].to<JsonObject>();
    panic_status["v"] = panic.version;
    panic_record(panic_status["current"].to<JsonObject>(), panic.current);
    panic_record(panic_status["previous"].to<JsonObject>(), panic.previous);
    std::string body;
    body.reserve(2048);
    serializeJson(doc, body);
    // Two boots with fixed-width numeric lane arrays add a bounded payload.
    // Retain a hard ceiling after the previous 2048-byte status allowance.
    if (doc.overflowed() || body.size() > 4096)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Status unavailable");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, body.data(), body.size());
}

void wait_for_network(void*)
{
    while (true) {
        // Reserve the HTTP task stack after lwIP initialization, before the
        // outbound TLS startup peak. INADDR_ANY does not need an IP yet.
        if (!WifiManager::GetInstance().IsInitialized()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        {
            httpd_config_t config = HTTPD_DEFAULT_CONFIG();
            config.server_port = kPort;
            config.ctrl_port = kPort + 1;
            config.stack_size = 6144;
            config.max_open_sockets = 3;
            config.max_uri_handlers = 4;
            config.lru_purge_enable = true;
            config.recv_wait_timeout = 5;
            config.send_wait_timeout = 2;
            const auto free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            const auto largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            const auto result = httpd_start(&server, &config);
            if (result == ESP_OK) {
                httpd_uri_t route{};
                route.uri = "/api/status";
                route.method = HTTP_GET;
                route.handler = status_handler;
                httpd_uri_t music_route{};
                music_route.uri = "/api/music";
                music_route.method = HTTP_POST;
                music_route.handler = music_handler;
                if (httpd_register_uri_handler(server, &route) == ESP_OK &&
                    httpd_register_uri_handler(server, &music_route) == ESP_OK &&
                    firmware_update::register_handlers(server) == ESP_OK) {
                    ESP_LOGI("botifier-debug", "Authenticated status and firmware update listening on port %d", kPort);
                    vTaskDelete(nullptr);
                }
                httpd_stop(server);
                server = nullptr;
            } else {
                ESP_LOGW("botifier-debug", "HTTP start failed: %d; internal free %u, largest %u", result,
                         static_cast<unsigned>(free_before), static_cast<unsigned>(largest_before));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
} // namespace

esp_err_t start()
{
    std::lock_guard<std::mutex> lock(lifecycle);
    if (worker) return ESP_OK;
    return xTaskCreatePinnedToCore(wait_for_network, "botifier-debug", 4096, nullptr, 1, &worker, 0) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
} // namespace codex_weekly::network_debug
