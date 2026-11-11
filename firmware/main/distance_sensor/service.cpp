#include "service.h"
#include "vl53l1x.h"
#include "proximity_policy.h"

#include <driver/i2c_master.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <cinttypes>
#include <cstdio>
#include <ctime>

namespace distance_sensor {
namespace {

constexpr int transaction_timeout_ms = 50;
constexpr uint32_t sample_timeout_ms = 1500;
constexpr uint32_t retry_delay_ms = 5000;
TaskHandle_t sampler_task = nullptr;
uint32_t sequence = 0;
ProximityPolicy policy;
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
Snapshot latest_snapshot;
ProximityConfig requested_config;
SchedulePolicy schedule_policy;
std::uint32_t applied_schedule_generation = 0;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

ScheduleStatus schedule_snapshot()
{
    const auto wall = static_cast<std::int64_t>(std::time(nullptr));
    const auto uptime = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&snapshot_lock);
    const auto current = schedule_policy.update(wall, uptime);
    portEXIT_CRITICAL(&snapshot_lock);
    return current;
}

struct StoredConfig {
    uint32_t version;
    uint32_t threshold_mm;
    uint32_t hysteresis_mm;
    uint32_t near_duration_ms;
};

void load_config()
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open("sc_distance", NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return;
    if (result == ESP_OK) {
        StoredConfig stored{};
        size_t size = sizeof(stored);
        result = nvs_get_blob(handle, "config", &stored, &size);
        nvs_close(handle);
        if (result == ESP_ERR_NVS_NOT_FOUND) return;
        if (result == ESP_OK) {
            if (size != sizeof(stored) || stored.version != 1 || stored.threshold_mm > 4000 ||
                stored.hysteresis_mm > 4000) {
                result = ESP_ERR_INVALID_ARG;
            } else {
                ProximityConfig loaded{static_cast<uint16_t>(stored.threshold_mm),
                                       static_cast<uint16_t>(stored.hysteresis_mm),
                                       stored.near_duration_ms, sample_timeout_ms};
                result = set_config(loaded);
            }
        }
    }
    if (result != ESP_OK) {
        std::printf("distance settings not loaded: %s; using defaults\n", esp_err_to_name(result));
    }
}

const char* proximity_name(ProximityState state)
{
    switch (state) {
        case ProximityState::Safe: return "safe";
        case ProximityState::Near: return "near";
        case ProximityState::Alert: return "alert";
        default: return "waiting";
    }
}

void emit(const char* state, esp_err_t error, const Reading* reading = nullptr)
{
    const uint32_t timestamp = now_ms();
    const auto schedule = schedule_snapshot();
    if (!schedule.enabled()) {
        reading = nullptr;
        state = schedule.mode == ScheduleMode::WaitingClock ? "waiting_clock" : "schedule_off";
    }
    // Only this task touches the policy. UI changes are copied under the same
    // short lock as snapshots; I2C, LVGL and NVS never run inside that lock.
    policy.configure(config());
    if (applied_schedule_generation != schedule.generation) {
        policy.invalidate();
        applied_schedule_generation = schedule.generation;
    }
    if (reading) {
        policy.feed(reading->valid, reading->millimeters, timestamp);
    } else {
        policy.invalidate();
    }
    const bool warning = policy.alert_active();
    Snapshot next;
    next.valid = reading && reading->valid;
    next.raw_mm = reading ? reading->millimeters : -1;
    next.filtered_mm = policy.filtered_mm();
    next.sequence = sequence++;
    next.sampled_at_ms = timestamp;
    next.near_elapsed_ms = policy.near_elapsed_ms();
    next.proximity_state = policy.state();
    next.sensor_state = state;
    next.raw_status = reading ? reading->raw_status : 255;
    next.error = error;
    next.stale = false;
    next.config = policy.config();
    next.schedule = schedule;
    portENTER_CRITICAL(&snapshot_lock);
    latest_snapshot = next;
    portEXIT_CRITICAL(&snapshot_lock);
    char distance[12] = "null";
    if (reading) {
        std::snprintf(distance, sizeof(distance), "%u", static_cast<unsigned>(reading->millimeters));
    }
    // One printf call keeps each telemetry record separate from ordinary logs.
    // The existing secondary USB Serial/JTAG console mirrors stdout to USB.
    std::printf("SC_DISTANCE {\"v\":1,\"seq\":%" PRIu32 ",\"uptime_ms\":%" PRIu32
                ",\"valid\":%s,\"distance_mm\":%s,\"raw_status\":%u,\"state\":\"%s\",\"error\":%d,\"robot_alert\":%s"
                ",\"filtered_mm\":%d,\"near_ms\":%" PRIu32 ",\"proximity_state\":\"%s\""
                ",\"threshold_mm\":%u,\"hysteresis_mm\":%u,\"hold_ms\":%" PRIu32
                ",\"monitoring\":%s,\"schedule\":\"%s\",\"clock_available\":%s"
                ",\"local_weekday\":%d,\"local_minute\":%d,\"manual_remaining_ms\":%" PRIu32 "}\n",
                next.sequence, timestamp, next.valid ? "true" : "false", distance,
                static_cast<unsigned>(next.raw_status), state, error, warning ? "true" : "false",
                next.filtered_mm, next.near_elapsed_ms, proximity_name(next.proximity_state),
                static_cast<unsigned>(next.config.threshold_mm), static_cast<unsigned>(next.config.hysteresis_mm),
                next.config.near_duration_ms, schedule.enabled() ? "true" : "false",
                schedule_name(schedule.mode), schedule.clock_available ? "true" : "false",
                schedule.local_weekday, schedule.local_minute, schedule.manual_remaining_ms);
    std::fflush(stdout);
}

void report_and_wait(const char* state, esp_err_t error, uint32_t milliseconds)
{
    for (uint32_t waited = 0; waited < milliseconds; waited += 1000) {
        emit(state, error);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void wait_for_enable()
{
    unsigned ticks = 0;
    while (!schedule_snapshot().enabled()) {
        if (ticks++ % 10 == 0) emit("schedule_off", ESP_OK);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t create_external_bus(i2c_master_bus_handle_t& bus)
{
    i2c_master_bus_handle_t existing = nullptr;
    const esp_err_t found = i2c_master_get_bus_handle(I2C_NUM_0, &existing);
    if (found == ESP_OK) {
        // Its pins/ownership are unknown: never repurpose somebody else's bus.
        return ESP_ERR_INVALID_STATE;
    }
    if (found != ESP_ERR_INVALID_STATE && found != ESP_ERR_NOT_FOUND) {
        return found;
    }
    i2c_master_bus_config_t configuration = {};
    configuration.i2c_port = I2C_NUM_0;
    configuration.sda_io_num = GPIO_NUM_2;
    configuration.scl_io_num = GPIO_NUM_1;
    configuration.clk_source = I2C_CLK_SRC_DEFAULT;
    configuration.glitch_ignore_cnt = 7;
    configuration.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&configuration, &bus);
}

void run(void*)
{
    i2c_master_bus_handle_t bus = nullptr;
    i2c_device_config_t configuration = {};
    configuration.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    configuration.device_address = 0x29;
    configuration.scl_speed_hz = 100000;
    i2c_master_dev_handle_t device = nullptr;
    esp_err_t result = ESP_OK;
    while (device == nullptr) {
        wait_for_enable();
        result = create_external_bus(bus);
        if (result != ESP_OK) {
            report_and_wait("i2c_bus_unavailable", result, retry_delay_ms);
            continue;
        }
        result = i2c_master_bus_add_device(bus, &configuration, &device);
        if (result != ESP_OK) {
            // Only this task's newly created bus may be removed after failure.
            i2c_del_master_bus(bus);
            bus = nullptr;
            report_and_wait("i2c_device_unavailable", result, retry_delay_ms);
        }
    }

    Vl53l1x sensor;
    emit("starting", ESP_OK);
    while (true) {
        wait_for_enable();
        result = i2c_master_probe(bus, 0x29, transaction_timeout_ms);
        if (result != ESP_OK) {
            report_and_wait("sensor_missing", result, retry_delay_ms);
            continue;
        }
        emit("initializing", ESP_OK);
        const auto session_generation = schedule_snapshot().generation;
        result = sensor.initialize(device);
        if (result != ESP_OK) {
            sensor.stop();
            report_and_wait("sensor_init_error", result, retry_delay_ms);
            continue;
        }

        uint32_t last_frame = now_ms();
        auto schedule = schedule_snapshot();
        while (schedule.enabled() && schedule.generation == session_generation) {
            Reading reading;
            bool ready = false;
            result = sensor.poll(reading, ready);
            if (result != ESP_OK) {
                emit("sensor_read_error", result);
                break;
            }
            if (ready) {
                last_frame = now_ms();
                emit("ranging", ESP_OK, &reading);
            } else if (now_ms() - last_frame > sample_timeout_ms) {
                emit("sensor_timeout", ESP_ERR_TIMEOUT);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            schedule = schedule_snapshot();
        }
        sensor.stop();
        // Scheduled stop clears the median/dwell via emit before a later start.
        // It must not enter the sensor-failure backoff path.
        schedule = schedule_snapshot();
        if (!schedule.enabled() || schedule.generation != session_generation) {
            emit(schedule.enabled() ? "starting" : "schedule_off", ESP_OK);
            continue;
        }
        report_and_wait("retrying", result == ESP_OK ? ESP_ERR_TIMEOUT : result, retry_delay_ms);
    }
}

}  // namespace

Snapshot snapshot()
{
    portENTER_CRITICAL(&snapshot_lock);
    Snapshot current = latest_snapshot;
    portEXIT_CRITICAL(&snapshot_lock);
    current = current.at(now_ms());
    const auto sampled_generation = current.schedule.generation;
    current.schedule = schedule_snapshot();
    if (!current.schedule.enabled() || current.schedule.generation != sampled_generation) {
        current.valid = false;
        current.raw_mm = current.filtered_mm = -1;
        current.near_elapsed_ms = 0;
        current.proximity_state = ProximityState::Waiting;
        current.sensor_state = current.schedule.enabled() ? "starting" :
            current.schedule.mode == ScheduleMode::WaitingClock ? "waiting_clock" : "schedule_off";
    }
    return current;
}

void enable_from_head_touch()
{
    const auto wall = static_cast<std::int64_t>(std::time(nullptr));
    const auto uptime = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&snapshot_lock);
    schedule_policy.touch(wall, uptime);
    portEXIT_CRITICAL(&snapshot_lock);
}

ScheduleStatus toggle_from_nfc()
{
    const auto wall = static_cast<std::int64_t>(std::time(nullptr));
    const auto uptime = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&snapshot_lock);
    const auto current = schedule_policy.toggle(wall, uptime);
    portEXIT_CRITICAL(&snapshot_lock);
    return current;
}

ProximityConfig config()
{
    portENTER_CRITICAL(&snapshot_lock);
    const ProximityConfig current = requested_config;
    portEXIT_CRITICAL(&snapshot_lock);
    return current;
}

esp_err_t set_config(const ProximityConfig& configuration)
{
    if (!valid_config(configuration) || configuration.sample_timeout_ms != sample_timeout_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&snapshot_lock);
    requested_config = configuration;
    portEXIT_CRITICAL(&snapshot_lock);
    return ESP_OK;
}

esp_err_t save_config()
{
    const ProximityConfig current = config();
    const StoredConfig stored{1, current.threshold_mm, current.hysteresis_mm, current.near_duration_ms};
    nvs_handle_t handle;
    esp_err_t result = nvs_open("sc_distance", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    // A single versioned value prevents partially saved parameter sets.
    result = nvs_set_blob(handle, "config", &stored, sizeof(stored));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

bool alert_active()
{
    const auto current = snapshot();
    return current.valid && !current.stale && current.proximity_state == ProximityState::Alert;
}

esp_err_t start()
{
    if (sampler_task != nullptr) return ESP_ERR_INVALID_STATE;
    load_config();
    const BaseType_t result = xTaskCreate(run, "distance_sensor", 4096, nullptr, 2, &sampler_task);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace distance_sensor
