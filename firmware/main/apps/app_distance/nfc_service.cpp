#include "nfc_service.h"
#include "nfc_gate.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <mutex>

namespace nfc_sense {
namespace {

constexpr std::uint32_t poll_period_ms = 150;
constexpr std::uint32_t retry_ms = 1000;
constexpr std::uint32_t telemetry_period_ms = 500;
std::mutex state_mutex;
std::mutex lifecycle_mutex;
Snapshot latest;
CardGate gate;
TaskHandle_t worker_task = nullptr;
std::uint32_t epoch = 1;

std::uint32_t now_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

struct Control {
    bool enabled;
    std::uint32_t epoch;
};

Control control()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return {latest.active && latest.config.enabled, epoch};
}

void notify_worker()
{
    TaskHandle_t task;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        task = worker_task;
    }
    if (task) xTaskNotifyGive(task);
}

// Requires state_mutex. Never touches the reader or queues an event.
void block_locked()
{
    ++epoch;
    gate.block();
    latest.waiting_for_removal = true;
}

void emit_telemetry(std::uint32_t& previous_ms)
{
    const auto timestamp = now_ms();
    if (timestamp - previous_ms < telemetry_period_ms) return;
    const auto current = snapshot();
    previous_ms = timestamp;
    char uid[21]{};
    const auto length = std::min(current.card.uid_length, current.card.uid.size());
    for (std::size_t i = 0; i < length; ++i) {
        std::snprintf(uid + i * 2, sizeof(uid) - i * 2, "%02X", static_cast<unsigned>(current.card.uid[i]));
    }
    std::printf("SC_NFC {\"v\":1,\"uptime_ms\":%" PRIu32 ",\"active\":%s,\"enabled\":%s,"
                "\"playback_paused\":%s,\"status\":\"%s\",\"error\":%d,\"reads\":%" PRIu32
                ",\"errors\":%" PRIu32 ",\"accepted\":%" PRIu32 ",\"last_accepted_ms\":%" PRIu32
                ",\"waiting_for_removal\":%s,\"uid\":\"%s\"}\n",
                timestamp, current.active ? "true" : "false", current.config.enabled ? "true" : "false",
                current.playback_paused ? "true" : "false", status_text(current.status),
                static_cast<int>(current.error), current.reads, current.errors, current.accepted,
                current.last_accepted_ms, current.waiting_for_removal ? "true" : "false", uid);
}

void worker(void*)
{
    Reader reader;
    bool opened = false;
    std::uint32_t previous_telemetry_ms = 0;
    while (true) {
        const auto request = control();
        if (!request.enabled) {
            if (opened) {
                reader.close();
                opened = false;
            }
            emit_telemetry(previous_telemetry_ms);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (!opened) {
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (epoch != request.epoch || !latest.active || !latest.config.enabled) continue;
                latest.status = Status::Starting;
            }
            const auto error = reader.init();
            if (error != ESP_OK) {
                reader.close();
                {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    if (epoch == request.epoch && latest.active && latest.config.enabled) {
                        gate.block();
                        latest.waiting_for_removal = true;
                        latest.card = {};
                        latest.status = Status::Error;
                        latest.error = error;
                        ++latest.errors;
                    }
                }
                emit_telemetry(previous_telemetry_ms);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
                continue;
            }
            opened = true;
        }
        if (control().epoch != request.epoch) continue;
        const auto started = now_ms();
        Card card{};
        bool present = false;
        auto error = reader.poll(card, present);
        if (error == ESP_OK && present && (card.uid_length == 0 || card.uid_length > card.uid.size())) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
        const auto timestamp = now_ms();
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (epoch == request.epoch && latest.active && latest.config.enabled) {
                ++latest.reads;
                if (error != ESP_OK) ++latest.errors;
                const bool accepted = gate.observe(timestamp, present, error == ESP_OK, latest.playback_paused);
                latest.waiting_for_removal = gate.waiting_for_removal();
                latest.card = error == ESP_OK && present ? card : Card{};
                latest.error = error;
                latest.status = error != ESP_OK ? Status::Error : (present ? Status::CardPresent : Status::Ready);
                if (accepted) {
                    ++latest.accepted;
                    latest.last_accepted_ms = timestamp;
                }
            }
        }
        if (error != ESP_OK) {
            reader.close();
            opened = false;
            emit_telemetry(previous_telemetry_ms);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
            continue;
        }
        emit_telemetry(previous_telemetry_ms);
        const auto elapsed = now_ms() - started;
        const auto wait = elapsed < poll_period_ms ? poll_period_ms - elapsed : 1u;
        ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(wait)));
    }
}

}  // namespace

esp_err_t start()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    TaskHandle_t existing;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        existing = worker_task;
    }
    if (!existing) {
        TaskHandle_t created = nullptr;
        if (xTaskCreatePinnedToCore(worker, "nfc-reader", 8192, nullptr, 1, &created, 0) != pdPASS) {
            std::lock_guard<std::mutex> lock(state_mutex);
            latest.status = Status::Error;
            latest.error = ESP_ERR_NO_MEM;
            ++latest.errors;
            return ESP_ERR_NO_MEM;
        }
        std::lock_guard<std::mutex> lock(state_mutex);
        worker_task = created;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (latest.active) return ESP_OK;
        latest.active = true;
        ++epoch;
        if (latest.playback_paused || !latest.config.enabled) gate.block();
        latest.waiting_for_removal = gate.waiting_for_removal();
        latest.status = latest.config.enabled ? Status::Starting : Status::Off;
        latest.error = ESP_OK;
        latest.card = {};
    }
    notify_worker();
    return ESP_OK;
}

void stop()
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!latest.active) return;
        latest.active = false;
        block_locked();
        latest.status = Status::Off;
        latest.error = ESP_OK;
        latest.card = {};
    }
    notify_worker();
}

Config config()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return latest.config;
}

void set_config(const Config& configuration)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (latest.config.enabled == configuration.enabled) return;
        latest.config = configuration;
        block_locked();
        latest.status = latest.active && configuration.enabled ? Status::Starting : Status::Off;
        latest.error = ESP_OK;
        latest.card = {};
    }
    notify_worker();
}

Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return latest;
}

void pause_for_playback()
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (latest.playback_paused) return;
        latest.playback_paused = true;
        block_locked();
    }
    notify_worker();
}

void resume_after_playback()
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!latest.playback_paused) return;
        latest.playback_paused = false;
        block_locked();
    }
    notify_worker();
}

const char* status_text(Status status)
{
    switch (status) {
    case Status::Off: return "off";
    case Status::Starting: return "starting";
    case Status::Ready: return "ready";
    case Status::CardPresent: return "card_present";
    case Status::Error: return "error";
    }
    return "unknown";
}

}  // namespace nfc_sense
