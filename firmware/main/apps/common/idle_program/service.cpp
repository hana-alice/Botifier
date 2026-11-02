#include "service.h"
#include "policy.h"
#include "wire.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

namespace idle_program {
namespace {

constexpr char tag[] = "idle-program";
std::mutex state_mutex;
std::mutex lifecycle_mutex;
std::mutex persistence_mutex;
Policy policy;
TaskHandle_t worker_task = nullptr;

struct StoredConfig {
    std::uint32_t version, enabled, eye_interval_ms, rest_duration_ms;
    std::uint32_t idle_grace_ms, pet_gap_ms, weekly_duration_ms, weekly_interval_ms;
};

std::uint32_t now_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

std::uint32_t unix_now()
{
    const auto value = std::time(nullptr);
    return value >= 1600000000 && static_cast<std::uint64_t>(value) <= UINT32_MAX
               ? static_cast<std::uint32_t>(value) : 0u;
}

Config load_config()
{
    std::lock_guard<std::mutex> persistence(persistence_mutex);
    Config result;
    nvs_handle_t handle;
    if (nvs_open("sc_idle", NVS_READONLY, &handle) != ESP_OK) return result;
    StoredConfig stored{};
    std::size_t size = sizeof(stored);
    const auto error = nvs_get_blob(handle, "config", &stored, &size);
    nvs_close(handle);
    if (error != ESP_OK || size != sizeof(stored) || stored.version != 1 || stored.enabled > 1) return result;
    Config loaded;
    loaded.enabled = stored.enabled != 0;
    loaded.eye_interval_ms = stored.eye_interval_ms;
    loaded.rest_duration_ms = stored.rest_duration_ms;
    loaded.idle_grace_ms = stored.idle_grace_ms;
    loaded.pet_gap_ms = stored.pet_gap_ms;
    loaded.weekly_duration_ms = stored.weekly_duration_ms;
    loaded.weekly_interval_ms = stored.weekly_interval_ms;
    return valid_config(loaded) ? loaded : result;
}

bool process_frame(const char* data, std::size_t size)
{
    bool save = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        wire::Message message;
        if (!wire::parse(data, size, policy.config(), message)) return false;
        if (message.kind == wire::Kind::Weekly) {
            // Replayed or obsolete valid messages are ignored without log spam.
            policy.update_weekly(message.weekly, now_ms(), unix_now());
        } else {
            if (!policy.set_config(message.config, now_ms())) return false;
            save = message.save;
        }
    }
    if (save && save_config() != ESP_OK) ESP_LOGW(tag, "Idle configuration save failed");
    return true;
}

void emit_telemetry(std::uint32_t& previous_ms)
{
    const auto timestamp = now_ms();
    if (timestamp - previous_ms < 1000u) return;
    previous_ms = timestamp;
    const auto current = snapshot();
    std::printf("SC_IDLE_STATE {\"v\":1,\"uptime_ms\":%" PRIu32 ",\"active\":%s,\"blocked\":%s,"
                "\"program\":\"%s\",\"remaining_ms\":%" PRIu32 ",\"eye_remaining_ms\":%" PRIu32
                ",\"completed_rests\":%" PRIu32 ",\"weekly_status\":\"%s\",\"weekly_left\":%d,"
                "\"reset_at\":%" PRIu32 "}\n",
                timestamp, current.active ? "true" : "false", current.blocked ? "true" : "false",
                program_text(current.program), current.remaining_ms, current.eye_remaining_ms, current.completed_rests,
                weekly_status_text(current.weekly_status), current.weekly_left, current.weekly_reset_at);
}

void worker(void*)
{
    int secondary = -1;
    std::uint32_t opened_at = now_ms() - 1000u;
    std::uint32_t telemetry_at = 0, warned_at = 0;
    bool have_warning = false;
    wire::Lines lines;
    auto invalid_frame = [&] {
        const auto timestamp = now_ms();
        if (!have_warning || timestamp - warned_at >= 1000u) {
            ESP_LOGW(tag, "Rejected idle control frame");
            have_warning = true;
            warned_at = timestamp;
        }
    };
    while (true) {
        const auto timestamp = now_ms();
        if (secondary < 0 && timestamp - opened_at >= 1000u) {
            opened_at = timestamp;
            // Existing secondary VFS only. No driver install, console switch,
            // stream buffering changes, line-ending changes or UART ownership.
            secondary = ::open("/dev/secondary", O_RDONLY | O_NONBLOCK);
        }
        if (secondary >= 0) {
            // Bound work per tick even if a sender floods malformed input.
            for (unsigned batch = 0; batch < 4; ++batch) {
                char bytes[128];
                const auto count = ::read(secondary, bytes, sizeof(bytes));
                if (count < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        ::close(secondary);
                        secondary = -1;
                        lines = wire::Lines{};
                    }
                    break;
                }
                if (count == 0) break;
                for (ssize_t i = 0; i < count; ++i) {
                    const auto result = lines.push(bytes[i]);
                    if (result == wire::LineResult::TooLong ||
                        (result == wire::LineResult::Ready && !process_frame(lines.data(), lines.size()))) invalid_frame();
                }
                if (count < static_cast<ssize_t>(sizeof(bytes))) break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            policy.tick(now_ms(), unix_now());
        }
        emit_telemetry(telemetry_at);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

} // namespace

esp_err_t start()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    if (worker_task) return ESP_OK;
    const auto loaded = load_config();
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        policy.set_config(loaded, now_ms());
    }
    TaskHandle_t created = nullptr;
    if (xTaskCreatePinnedToCore(worker, "idle-program", 6144, nullptr, 1, &created, 0) != pdPASS) return ESP_ERR_NO_MEM;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        policy.start(now_ms());
        worker_task = created;
    }
    return ESP_OK;
}

Config config()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return policy.config();
}

esp_err_t set_config(const Config& value)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return policy.set_config(value, now_ms()) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t save_config()
{
    const auto value = config();
    const StoredConfig stored{1, value.enabled ? 1u : 0u, value.eye_interval_ms, value.rest_duration_ms,
                              value.idle_grace_ms, value.pet_gap_ms, value.weekly_duration_ms, value.weekly_interval_ms};
    std::lock_guard<std::mutex> persistence(persistence_mutex);
    nvs_handle_t handle;
    auto error = nvs_open("sc_idle", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_blob(handle, "config", &stored, sizeof(stored));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

void set_context(const Context& value)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    policy.set_context(value, now_ms());
}

void note_activity()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    policy.note_activity(now_ms());
}

Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return policy.snapshot(now_ms(), unix_now());
}

const char* program_text(Program value)
{
    switch (value) {
    case Program::Pet: return "pet";
    case Program::EyeRest: return "eye_rest";
    case Program::Weekly: return "weekly";
    case Program::RestDone: return "rest_done";
    }
    return "pet";
}

const char* weekly_status_text(WeeklyStatus value)
{
    switch (value) {
    case WeeklyStatus::Waiting: return "waiting";
    case WeeklyStatus::Fresh: return "fresh";
    case WeeklyStatus::Stale: return "stale";
    case WeeklyStatus::Unavailable: return "unavailable";
    }
    return "unavailable";
}

} // namespace idle_program
