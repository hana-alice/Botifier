// SPDX-License-Identifier: BSD-3-Clause
// Register configuration/algorithms: Copyright (c) 2018 STMicroelectronics.
// Adapted from stm32duino/VL53L1X, commit 5aa46e743913186c448aaf86dd0f12071fddc0a2.
// Full redistribution terms are retained in LICENSE beside this file.
#include "vl53l1x.h"

#include <cstring>
#include <esp_check.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace distance_sensor {
namespace {

constexpr const char* kTag             = "VL53L1X";
constexpr int kI2cTimeoutMs            = 50;
constexpr int64_t kBootTimeoutUs       = 1000000;
constexpr int64_t kCalibrationTimeoutUs = 1000000;
constexpr uint32_t kIntermeasurementMs = 100;

constexpr uint16_t kVhvTimeout         = 0x0008;
constexpr uint16_t kVhvInit            = 0x000B;
constexpr uint16_t kDefaultConfigStart = 0x002D;
constexpr uint16_t kI2cPadConfig       = 0x002E;
constexpr uint16_t kGpioPadConfig      = 0x002F;
constexpr uint16_t kGpioMuxControl     = 0x0030;
constexpr uint16_t kPhasecalTimeout    = 0x004B;
constexpr uint16_t kTimeoutA           = 0x005E;
constexpr uint16_t kVcselPeriodA       = 0x0060;
constexpr uint16_t kTimeoutB           = 0x0061;
constexpr uint16_t kVcselPeriodB       = 0x0063;
constexpr uint16_t kValidPhaseHigh     = 0x0069;
constexpr uint16_t kIntermeasurement  = 0x006C;
constexpr uint16_t kWoi               = 0x0078;
constexpr uint16_t kInitialPhase      = 0x007A;
constexpr uint16_t kInterruptClear    = 0x0086;
constexpr uint16_t kModeStart         = 0x0087;
constexpr uint16_t kResultStatus      = 0x0089;
constexpr uint16_t kOscCalibration    = 0x00DE;
constexpr uint16_t kBootStatus        = 0x00E5;
constexpr uint16_t kModelId           = 0x010F;
constexpr uint16_t kExpectedModelId   = 0xEACC;

// ST's complete VL51L1X_DEFAULT_CONFIGURATION, addresses 0x002D..0x0087.
// Source/license and byte-for-byte verification are documented in README.md.
constexpr uint8_t kDefaultConfiguration[] = {
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
    0x0B, 0x00, 0x00, 0x02, 0x0A, 0x21, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xC8, 0x00, 0x00, 0x38, 0xFF, 0x01, 0x00, 0x08, 0x00, 0x00, 0x01, 0xCC, 0x0F,
    0x01, 0xF1, 0x0D, 0x01, 0x68, 0x00, 0x80, 0x08, 0xB8, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0F, 0x0D, 0x0E, 0x0E, 0x00,
    0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
};
static_assert(sizeof(kDefaultConfiguration) == 0x0087 - kDefaultConfigStart + 1);

uint16_t decode16(const uint8_t* bytes)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

void waitBetweenChecks()
{
    const TickType_t ticks = pdMS_TO_TICKS(5);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

}  // namespace

esp_err_t Vl53l1x::read(uint16_t reg, uint8_t* data, size_t length)
{
    const uint8_t address[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
    return i2c_master_transmit_receive(device_, address, sizeof(address), data, length, kI2cTimeoutMs);
}

esp_err_t Vl53l1x::write8(uint16_t reg, uint8_t value)
{
    const uint8_t bytes[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg), value};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), kI2cTimeoutMs);
}

esp_err_t Vl53l1x::write16(uint16_t reg, uint16_t value)
{
    const uint8_t bytes[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg),
                             static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), kI2cTimeoutMs);
}

esp_err_t Vl53l1x::write32(uint16_t reg, uint32_t value)
{
    const uint8_t bytes[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg),
                             static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                             static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), kI2cTimeoutMs);
}

esp_err_t Vl53l1x::dataReady(bool& ready)
{
    ready = false;
    // GPIO_HV_MUX__CTRL and GPIO__TIO_HV_STATUS are consecutive registers.
    uint8_t gpio[2] = {};
    const esp_err_t error = read(kGpioMuxControl, gpio, sizeof(gpio));
    if (error != ESP_OK) return error;

    // ST GetInterruptPolarity/CheckForDataReady: invert MUX bit 4, compare status bit 0.
    const uint8_t active_level = (gpio[0] & 0x10) == 0 ? 1 : 0;
    ready = (gpio[1] & 1) == active_level;
    return ESP_OK;
}

esp_err_t Vl53l1x::waitForBoot()
{
    const int64_t deadline = esp_timer_get_time() + kBootTimeoutUs;
    while (esp_timer_get_time() < deadline) {
        uint8_t status = 0;
        const esp_err_t error = read(kBootStatus, &status, 1);
        if (error != ESP_OK) return error;
        if ((status & 1) != 0) return ESP_OK;
        waitBetweenChecks();
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t Vl53l1x::waitForDataReady()
{
    const int64_t deadline = esp_timer_get_time() + kCalibrationTimeoutUs;
    while (esp_timer_get_time() < deadline) {
        bool ready = false;
        const esp_err_t error = dataReady(ready);
        if (error != ESP_OK) return error;
        if (ready) return ESP_OK;
        waitBetweenChecks();
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t Vl53l1x::configure()
{
    ESP_RETURN_ON_ERROR(write8(kModeStart, 0), kTag, "stop before configuration");

    uint8_t configuration[sizeof(kDefaultConfiguration) + 2] = {
        static_cast<uint8_t>(kDefaultConfigStart >> 8), static_cast<uint8_t>(kDefaultConfigStart)};
    std::memcpy(configuration + 2, kDefaultConfiguration, sizeof(kDefaultConfiguration));
    ESP_RETURN_ON_ERROR(i2c_master_transmit(device_, configuration, sizeof(configuration), kI2cTimeoutMs), kTag,
                        "default configuration");

    // The ToF4M schematic has 3.3 V AVDD and SDA/SCL/GPIO pull-ups.
    // ST's table comments require bit 0 for AVDD-level (rather than 1.8 V) pads.
    ESP_RETURN_ON_ERROR(write8(kI2cPadConfig, 1), kTag, "I2C pad voltage");
    ESP_RETURN_ON_ERROR(write8(kGpioPadConfig, 1), kTag, "GPIO pad voltage");

    // ST SensorInit's one-shot VHV calibration, with a deadline and checked I/O.
    ESP_RETURN_ON_ERROR(write8(kInterruptClear, 1), kTag, "clear before calibration");
    ESP_RETURN_ON_ERROR(write8(kModeStart, 0x40), kTag, "start calibration");
    ESP_RETURN_ON_ERROR(waitForDataReady(), kTag, "calibration data-ready timeout/read");
    ESP_RETURN_ON_ERROR(write8(kInterruptClear, 1), kTag, "clear calibration interrupt");
    ESP_RETURN_ON_ERROR(write8(kModeStart, 0), kTag, "stop calibration");
    ESP_RETURN_ON_ERROR(write8(kVhvTimeout, 0x09), kTag, "VHV loop bound");
    ESP_RETURN_ON_ERROR(write8(kVhvInit, 0), kTag, "VHV previous temperature");

    // ST SetDistanceMode(2), Long; then SetTimingBudgetInMs(50), Long branch.
    ESP_RETURN_ON_ERROR(write8(kPhasecalTimeout, 0x0A), kTag, "Long phase calibration");
    ESP_RETURN_ON_ERROR(write8(kVcselPeriodA, 0x0F), kTag, "Long VCSEL A");
    ESP_RETURN_ON_ERROR(write8(kVcselPeriodB, 0x0D), kTag, "Long VCSEL B");
    ESP_RETURN_ON_ERROR(write8(kValidPhaseHigh, 0xB8), kTag, "Long valid phase");
    ESP_RETURN_ON_ERROR(write16(kWoi, 0x0F0D), kTag, "Long WOI");
    ESP_RETURN_ON_ERROR(write16(kInitialPhase, 0x0E0E), kTag, "Long initial phase");
    ESP_RETURN_ON_ERROR(write16(kTimeoutA, 0x00AD), kTag, "50 ms timeout A");
    ESP_RETURN_ON_ERROR(write16(kTimeoutB, 0x00C6), kTag, "50 ms timeout B");

    uint8_t oscillator[2] = {};
    ESP_RETURN_ON_ERROR(read(kOscCalibration, oscillator, sizeof(oscillator)), kTag, "oscillator calibration");
    const uint16_t clock_pll = decode16(oscillator) & 0x03FF;
    if (clock_pll == 0) return ESP_ERR_INVALID_RESPONSE;
    // ST SetInterMeasurementInMs: calibrated clock * period_ms * 1.075.
    const uint32_t period = static_cast<uint32_t>(clock_pll * kIntermeasurementMs * 1.075);
    ESP_RETURN_ON_ERROR(write32(kIntermeasurement, period), kTag, "100 ms intermeasurement");
    ESP_RETURN_ON_ERROR(write8(kInterruptClear, 1), kTag, "clear before ranging");
    return write8(kModeStart, 0x40);
}

esp_err_t Vl53l1x::initialize(i2c_master_dev_handle_t device)
{
    if (device == nullptr) return ESP_ERR_INVALID_ARG;
    const esp_err_t stop_error = stop();
    if (stop_error != ESP_OK) return stop_error;
    device_ = device;

    esp_err_t error = waitForBoot();
    if (error == ESP_OK) {
        uint8_t model[2] = {};
        error = read(kModelId, model, sizeof(model));
        if (error == ESP_OK && decode16(model) != kExpectedModelId) error = ESP_ERR_NOT_SUPPORTED;
    }
    // Do not write configuration/stop registers to an unidentified device.
    if (error != ESP_OK) {
        device_ = nullptr;
        return error;
    }

    error = configure();
    if (error != ESP_OK) {
        // Preserve the original failure; this cleanup is best effort and also bounded.
        (void)stop();
        return error;
    }
    initialized_ = true;
    return ESP_OK;
}

esp_err_t Vl53l1x::poll(Reading& reading, bool& ready)
{
    reading = Reading{};
    ready   = false;
    if (!initialized_ || device_ == nullptr) return ESP_ERR_INVALID_STATE;

    bool available = false;
    esp_err_t error = dataReady(available);
    if (error != ESP_OK || !available) return error;

    // One contiguous read: status at 0x0089, big-endian distance at 0x0096/0x0097.
    // Read/acknowledge exactly one ready result, never reuse a previous Reading.
    uint8_t result[15] = {};
    error = read(kResultStatus, result, sizeof(result));
    if (error != ESP_OK) return error;
    error = write8(kInterruptClear, 1);
    if (error != ESP_OK) return error;

    reading.raw_status  = result[0];
    reading.millimeters = decode16(result + 13);
    // ST GetRangeStatus maps (raw & 0x1F) == 9 to 0 (valid). No other status
    // is accepted, including wrap/phase/signal failures and no target.
    reading.valid = (reading.raw_status & 0x1F) == 9 && reading.millimeters >= 40 && reading.millimeters <= 4000;
    ready = true;
    return ESP_OK;
}

esp_err_t Vl53l1x::stop()
{
    initialized_ = false;
    if (device_ == nullptr) return ESP_OK;
    const esp_err_t error = write8(kModeStart, 0);
    device_ = nullptr;
    return error;
}

}  // namespace distance_sensor
