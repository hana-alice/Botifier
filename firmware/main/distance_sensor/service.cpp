#include "service.h"
#include "vl53l1x.h"
#include "proximity_policy.h"

#include <driver/i2c_master.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cinttypes>
#include <cstdio>
#include <atomic>

namespace distance_sensor {
namespace {

constexpr int transaction_timeout_ms = 50;
constexpr uint32_t sample_timeout_ms = 1500;
constexpr uint32_t retry_delay_ms = 5000;
TaskHandle_t sampler_task = nullptr;
uint32_t sequence = 0;
ProximityPolicy policy;
std::atomic<bool> warning_active{false};
std::atomic<uint32_t> warning_sample_ms{0};

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void emit(const char* state, esp_err_t error, const Reading* reading = nullptr)
{
    const uint32_t timestamp = now_ms();
    if (reading) {
        policy.feed(reading->valid, reading->millimeters, timestamp);
    } else {
        policy.invalidate();
    }
    const bool warning = policy.alert_active();
    warning_sample_ms.store(timestamp, std::memory_order_relaxed);
    warning_active.store(warning, std::memory_order_release);
    char distance[12] = "null";
    if (reading) {
        std::snprintf(distance, sizeof(distance), "%u", static_cast<unsigned>(reading->millimeters));
    }
    // One printf call keeps each telemetry record separate from ordinary logs.
    // The existing secondary USB Serial/JTAG console mirrors stdout to USB.
    std::printf("SC_DISTANCE {\"v\":1,\"seq\":%" PRIu32 ",\"uptime_ms\":%" PRIu32
                ",\"valid\":%s,\"distance_mm\":%s,\"raw_status\":%u,\"state\":\"%s\",\"error\":%d,\"robot_alert\":%s}\n",
                sequence++, timestamp, reading && reading->valid ? "true" : "false", distance,
                reading ? static_cast<unsigned>(reading->raw_status) : 255U, state, error, warning ? "true" : "false");
    std::fflush(stdout);
}

void report_and_wait(const char* state, esp_err_t error, uint32_t milliseconds)
{
    for (uint32_t waited = 0; waited < milliseconds; waited += 1000) {
        emit(state, error);
        vTaskDelay(pdMS_TO_TICKS(1000));
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
        result = i2c_master_probe(bus, 0x29, transaction_timeout_ms);
        if (result != ESP_OK) {
            report_and_wait("sensor_missing", result, retry_delay_ms);
            continue;
        }
        emit("initializing", ESP_OK);
        result = sensor.initialize(device);
        if (result != ESP_OK) {
            sensor.stop();
            report_and_wait("sensor_init_error", result, retry_delay_ms);
            continue;
        }

        uint32_t last_frame = now_ms();
        while (true) {
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
        }
        sensor.stop();
        report_and_wait("retrying", result == ESP_OK ? ESP_ERR_TIMEOUT : result, retry_delay_ms);
    }
}

}  // namespace

bool alert_active()
{
    if (!warning_active.load(std::memory_order_acquire)) return false;
    const uint32_t sampled_at = warning_sample_ms.load(std::memory_order_relaxed);
    return now_ms() - sampled_at <= sample_timeout_ms;
}

esp_err_t start()
{
    if (sampler_task != nullptr) return ESP_ERR_INVALID_STATE;
    const BaseType_t result = xTaskCreate(run, "distance_sensor", 4096, nullptr, 2, &sampler_task);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace distance_sensor
