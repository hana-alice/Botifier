#include "network_debug.h"
#include "service.h"
#include <apps/common/firmware_update/service.h>
#include <apps/common/idle_program/service.h>
#include <distance_sensor/service.h>
#include <hal/hal.h>
#include <ArduinoJson.h>
#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <array>
#include <mutex>
#include <string>

namespace codex_weekly::network_debug {
namespace {
std::mutex lifecycle;
TaskHandle_t worker = nullptr;
httpd_handle_t server = nullptr;

esp_err_t status_handler(httpd_req_t* request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    std::array<char, 33> key{};
    if (httpd_req_get_hdr_value_len(request, "X-Botifier-Key") != 32 ||
        httpd_req_get_hdr_value_str(request, "X-Botifier-Key", key.data(), key.size()) != ESP_OK ||
        !authenticate_debug_key(std::string_view(key.data(), 32))) {
        return httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, "Device key required");
    }
    // Select fields explicitly. Never serialize the account-service Snapshot
    // wholesale: its device-only debug key and login code must stay private.
    const auto quota = snapshot();
    const auto idle = idle_program::snapshot();
    const auto distance = distance_sensor::snapshot();
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
    std::string body;
    body.reserve(1024);
    serializeJson(doc, body);
    if (body.size() > 2048) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Status unavailable");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, body.data(), body.size());
}

void wait_for_network(void*)
{
    while (true) {
        if (snapshot().wifi_connected) {
            httpd_config_t config = HTTPD_DEFAULT_CONFIG();
            config.server_port = kPort;
            config.ctrl_port = kPort + 1;
            config.stack_size = 6144;
            config.max_open_sockets = 3;
            config.max_uri_handlers = 3;
            config.lru_purge_enable = true;
            config.recv_wait_timeout = 5;
            config.send_wait_timeout = 2;
            if (httpd_start(&server, &config) == ESP_OK) {
                httpd_uri_t route{};
                route.uri = "/api/status";
                route.method = HTTP_GET;
                route.handler = status_handler;
                if (httpd_register_uri_handler(server, &route) == ESP_OK &&
                    firmware_update::register_handlers(server) == ESP_OK) {
                    ESP_LOGI("botifier-debug", "Authenticated status and firmware update listening on port %d", kPort);
                    vTaskDelete(nullptr);
                }
                httpd_stop(server);
                server = nullptr;
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
