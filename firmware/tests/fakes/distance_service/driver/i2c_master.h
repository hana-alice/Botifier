#pragma once

#include <cstdint>
#include <esp_err.h>

struct FakeDistanceBus;
struct FakeDistanceDevice;
using i2c_master_bus_handle_t = FakeDistanceBus*;
using i2c_master_dev_handle_t = FakeDistanceDevice*;

constexpr int I2C_NUM_0 = 0;
constexpr int GPIO_NUM_1 = 1;
constexpr int GPIO_NUM_2 = 2;
constexpr int I2C_CLK_SRC_DEFAULT = 0;
constexpr int I2C_ADDR_BIT_LEN_7 = 7;

struct i2c_master_bus_config_t {
    int i2c_port = 0;
    int sda_io_num = 0;
    int scl_io_num = 0;
    int clk_source = 0;
    unsigned glitch_ignore_cnt = 0;
    struct { bool enable_internal_pullup = false; } flags;
};

struct i2c_device_config_t {
    int dev_addr_length = 0;
    std::uint16_t device_address = 0;
    std::uint32_t scl_speed_hz = 0;
};

esp_err_t i2c_master_get_bus_handle(int port, i2c_master_bus_handle_t* result);
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* configuration, i2c_master_bus_handle_t* result);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t* configuration,
                                  i2c_master_dev_handle_t* result);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, std::uint16_t address, int timeout_ms);
