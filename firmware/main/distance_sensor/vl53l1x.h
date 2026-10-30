// SPDX-License-Identifier: BSD-3-Clause
// Register configuration/algorithms: Copyright (c) 2018 STMicroelectronics.
// See LICENSE and README.md in this directory for attribution and limitations.
#pragma once

#include <cstddef>
#include <cstdint>
#include <driver/i2c_master.h>
#include <esp_err.h>

namespace distance_sensor {

struct Reading {
    uint16_t millimeters = 0;
    uint8_t raw_status   = 255;  // Unmodified RESULT__RANGE_STATUS register byte.
    bool valid           = false;
};

// Single caller/task only. The caller owns the synchronous I2C device and bus;
// keep both alive until stop() returns. No Arduino or external component needed.
class Vl53l1x {
public:
    // Starts Long-mode ranging: 50 ms timing budget, 100 ms intermeasurement.
    // Boot/calibration waits are bounded; reinitialization first stops this instance.
    esp_err_t initialize(i2c_master_dev_handle_t device);

    // Always resets both outputs first. No sample: ESP_OK, ready=false.
    // New sample: ESP_OK, ready=true, including invalid optical measurements.
    // I2C/driver error: error code, ready=false, default Reading.
    esp_err_t poll(Reading& reading, bool& ready);

    // Idempotent, forgets the borrowed handle even on error; never deletes it.
    // A failed stop means hardware ranging may still be active.
    esp_err_t stop();

private:
    esp_err_t read(uint16_t reg, uint8_t* data, size_t length);
    esp_err_t write8(uint16_t reg, uint8_t value);
    esp_err_t write16(uint16_t reg, uint16_t value);
    esp_err_t write32(uint16_t reg, uint32_t value);
    esp_err_t dataReady(bool& ready);
    esp_err_t waitForBoot();
    esp_err_t waitForDataReady();
    esp_err_t configure();

    i2c_master_dev_handle_t device_ = nullptr;
    bool initialized_              = false;
};

}  // namespace distance_sensor
