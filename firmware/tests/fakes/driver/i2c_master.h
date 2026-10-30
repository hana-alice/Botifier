#pragma once

#include <cstddef>
#include <cstdint>
#include <esp_err.h>

struct FakeI2cDevice;
using i2c_master_dev_handle_t = FakeI2cDevice*;

extern "C" {
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t* data, size_t size, int timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t* address, size_t address_size,
                                      uint8_t* data, size_t size, int timeout_ms);
}
