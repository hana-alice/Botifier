#ifdef NDEBUG
#undef NDEBUG
#endif
#include <hal/drivers/Si12T/Si12T.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

struct FakeSi12tDevice { bool live = false; };

namespace {
unsigned assertions = 0;
void check(bool condition, const char* message)
{
    ++assertions;
    if (!condition) throw std::runtime_error(message);
    assert(condition);
}
namespace fake {
FakeSi12tDevice device;
std::array<std::uint8_t, 256> registers{};
std::array<unsigned, 256> reads{};
std::vector<std::pair<std::uint8_t, std::uint8_t>> writes;
bool fail_add = false;
int fail_write_reg = -1, fail_read_reg = -1, mismatch_reg = -1;
void reset()
{
    check(!device.live, "previous test leaked its device handle");
    registers = {}; reads = {}; writes.clear();
    fail_add = false; fail_write_reg = fail_read_reg = mismatch_reg = -1;
}
}
struct Device {
    si12t_handle_t handle = nullptr;
    Device()
    {
        fake::reset();
        si12t_config_t config{&fake::device, SI12T_GND_ADDRESS};
        check(si12t_init(&config, &handle) == ESP_OK && handle, "valid device initialization failed");
    }
    ~Device() { if (handle) si12t_delete(handle); }
    esp_err_t setup() { return si12t_setup(handle, SI12T_TYPE_LOW, SI12T_SENSITIVITY_LEVEL_3); }
};

void setup_preserves_required_bits_and_calibrates_after_configuration()
{
    Device device;
    check(device.setup() == ESP_OK, "normal setup failed");
    check(fake::registers[SI12T_CH_HOLD2_ADDR] == 0x30, "CH_HOLD2 did not preserve required bits 4 and 5");
    check(fake::registers[SI12T_CTRL1_ADDR] == 0x22 && fake::registers[SI12T_CTRL2_ADDR] == 0x07,
          "setup changed the existing response, sleep or reset configuration");
    for (auto reg : {SI12T_SENSITIVITY1_ADDR, SI12T_SENSITIVITY2_ADDR, SI12T_SENSITIVITY3_ADDR,
                     SI12T_SENSITIVITY4_ADDR, SI12T_SENSITIVITY5_ADDR, SI12T_SENSITIVITY6_ADDR})
        check(fake::registers[reg] == 0x33, "setup changed the original sensitivity");

    const std::array<std::pair<std::uint8_t, std::uint8_t>, 4> pulse{{
        {SI12T_REF_RST1_ADDR, 0xff}, {SI12T_REF_RST2_ADDR, 0x0f},
        {SI12T_REF_RST1_ADDR, 0}, {SI12T_REF_RST2_ADDR, 0}}};
    check(fake::writes.size() >= pulse.size(), "setup omitted the reference reset pulse");
    const auto first_pulse = fake::writes.size() - pulse.size();
    for (std::size_t i = 0; i < pulse.size(); ++i)
        check(fake::writes[first_pulse + i] == pulse[i], "setup did not finish with the reference reset pulse");
    bool configured_before_pulse = false;
    for (std::size_t i = 0; i < first_pulse; ++i)
        if (fake::writes[i].first == SI12T_SENSITIVITY6_ADDR) configured_before_pulse = true;
    check(configured_before_pulse, "reference reset preceded final sensitivity configuration");
    for (auto reg : {SI12T_SENSITIVITY1_ADDR, SI12T_SENSITIVITY2_ADDR, SI12T_SENSITIVITY3_ADDR,
                     SI12T_SENSITIVITY4_ADDR, SI12T_SENSITIVITY5_ADDR, SI12T_SENSITIVITY6_ADDR, SI12T_CTRL1_ADDR, SI12T_CTRL2_ADDR,
                     SI12T_CH_HOLD1_ADDR, SI12T_CH_HOLD2_ADDR, SI12T_REF_RST1_ADDR, SI12T_REF_RST2_ADDR,
                     SI12T_CAL_HOLD1_ADDR, SI12T_CAL_HOLD2_ADDR})
        check(fake::reads[reg] != 0, "setup did not read back a configured register");
}

void three_channels_and_sleep_options_preserve_independent_settings()
{
    for (const bool sleep : {false, true}) {
        for (int level = 3; level <= 7; ++level) {
            Device device;
            check(si12t_setup_with_options(device.handle, SI12T_TYPE_LOW,
                      static_cast<si12t_sensitivity_level_t>(level), 0x007, sleep) == ESP_OK,
                  "three-channel setup rejected a valid sleep/sensitivity combination");
            check(fake::registers[SI12T_CH_HOLD1_ADDR] == 0xf8 &&
                  fake::registers[SI12T_CH_HOLD2_ADDR] == 0x3f,
                  "three-channel mask did not hold TS4 through TS12");
            check(fake::registers[SI12T_CTRL1_ADDR] == 0x22 &&
                  fake::registers[SI12T_CTRL2_ADDR] == (sleep ? 0x07 : 0x03),
                  "sleep option changed the response configuration or was not applied");
            check(fake::writes[0] == std::pair<std::uint8_t, std::uint8_t>{SI12T_CTRL2_ADDR, sleep ? 0x0f : 0x0b} &&
                  fake::writes[1] == std::pair<std::uint8_t, std::uint8_t>{SI12T_CTRL2_ADDR, sleep ? 0x07 : 0x03},
                  "digital reset did not retain the requested sleep option");
            for (int reg = SI12T_SENSITIVITY1_ADDR; reg <= SI12T_SENSITIVITY6_ADDR; ++reg)
                check(fake::registers[reg] == level * 0x11 && fake::reads[reg] != 0,
                      "configured sensitivity was not written and verified for all twelve channels");
            const std::array<std::pair<std::uint8_t, std::uint8_t>, 4> pulse{{
                {SI12T_REF_RST1_ADDR, 0x07}, {SI12T_REF_RST2_ADDR, 0x00},
                {SI12T_REF_RST1_ADDR, 0x00}, {SI12T_REF_RST2_ADDR, 0x00}}};
            const auto first_pulse = fake::writes.size() - pulse.size();
            for (std::size_t i = 0; i < pulse.size(); ++i)
                check(fake::writes[first_pulse + i] == pulse[i],
                      "configured reference pulse changed inactive channels or preceded final configuration");
            check(fake::writes[first_pulse - 1].first == SI12T_SENSITIVITY6_ADDR,
                  "reference reset preceded the final sensitivity register");
        }
    }
}

void channel_mask_maps_each_ts_pin_without_changing_required_bits()
{
    const std::array<std::pair<std::uint8_t, std::uint8_t>, 12> holds{{
        {0xfe, 0x3f}, {0xfd, 0x3f}, {0xfb, 0x3f}, {0xf7, 0x3f},
        {0xef, 0x3f}, {0xdf, 0x3f}, {0xbf, 0x3f}, {0x7f, 0x3f},
        {0xff, 0x3e}, {0xff, 0x3d}, {0xff, 0x3b}, {0xff, 0x37}}};
    for (unsigned channel = 0; channel < holds.size(); ++channel) {
        Device device;
        check(si12t_setup_with_options(device.handle, SI12T_TYPE_HIGH,
                  SI12T_SENSITIVITY_LEVEL_3, static_cast<std::uint16_t>(1u << channel), true) == ESP_OK,
              "valid single-channel mask was rejected");
        check(fake::registers[SI12T_CH_HOLD1_ADDR] == holds[channel].first &&
              fake::registers[SI12T_CH_HOLD2_ADDR] == holds[channel].second,
              "active mask selected the wrong TS pin or lost mandatory bits");
        check(fake::registers[SI12T_SENSITIVITY6_ADDR] == 0xbb,
              "high sensitivity was not preserved for the final channel pair");
    }
}

void invalid_setup_options_do_not_touch_hardware()
{
    Device device;
    for (const auto mask : {0x0000u, 0x1000u, 0x1007u, 0xffffu}) {
        check(si12t_setup_with_options(device.handle, SI12T_TYPE_LOW, SI12T_SENSITIVITY_LEVEL_3,
                  static_cast<std::uint16_t>(mask), false) == ESP_ERR_INVALID_ARG,
              "empty or out-of-range active-channel mask was accepted");
    }
    for (const auto level : {-1, 8, 255}) {
        check(si12t_setup_with_options(device.handle, SI12T_TYPE_LOW,
                  static_cast<si12t_sensitivity_level_t>(level), 0x007, true) == ESP_ERR_INVALID_ARG,
              "invalid setup sensitivity level was accepted");
    }
    for (const auto type : {-1, 2, 255}) {
        check(si12t_setup_with_options(device.handle, static_cast<si12t_type_t>(type),
                  SI12T_SENSITIVITY_LEVEL_3, 0x007, true) == ESP_ERR_INVALID_ARG,
              "invalid setup sensitivity type was accepted");
    }
    check(si12t_setup_with_options(nullptr, SI12T_TYPE_LOW, SI12T_SENSITIVITY_LEVEL_3,
              0x007, false) == ESP_ERR_INVALID_ARG, "configured setup accepted a null handle");
    check(fake::writes.empty(), "invalid options reset or reconfigured the sensor before rejection");
    for (const auto reads : fake::reads) check(reads == 0, "invalid options accessed sensor registers");
}

void configured_setup_checks_final_channel_sleep_and_sensitivity_readback()
{
    for (const auto reg : {SI12T_CH_HOLD1_ADDR, SI12T_CH_HOLD2_ADDR,
                          SI12T_CTRL2_ADDR, SI12T_SENSITIVITY6_ADDR}) {
        Device device;
        fake::mismatch_reg = reg;
        check(si12t_setup_with_options(device.handle, SI12T_TYPE_LOW,
                  SI12T_SENSITIVITY_LEVEL_7, 0x007, false) == ESP_ERR_INVALID_STATE,
              "configured setup accepted a mismatching channel, sleep or final sensitivity register");
    }
    {
        Device device;
        fake::fail_write_reg = SI12T_SENSITIVITY6_ADDR;
        check(si12t_setup_with_options(device.handle, SI12T_TYPE_LOW,
                  SI12T_SENSITIVITY_LEVEL_3, 0x007, false) == ESP_ERR_TIMEOUT,
              "configured setup ignored the final sensitivity write failure");
        check(fake::registers[SI12T_REF_RST1_ADDR] == 0,
              "failed sensitivity setup nevertheless forced a new reference");
    }
    {
        Device device;
        fake::fail_read_reg = SI12T_SENSITIVITY6_ADDR;
        check(si12t_setup_with_options(device.handle, SI12T_TYPE_LOW,
                  SI12T_SENSITIVITY_LEVEL_3, 0x007, false) == ESP_ERR_TIMEOUT,
              "configured setup ignored the final sensitivity read failure");
    }
}

void initialization_and_null_inputs_fail_safely()
{
    fake::reset();
    si12t_handle_t handle = nullptr;
    si12t_config_t config{&fake::device, SI12T_GND_ADDRESS};
    check(si12t_init(nullptr, &handle) == ESP_ERR_INVALID_ARG, "null config accepted");
    check(si12t_init(&config, nullptr) == ESP_ERR_INVALID_ARG, "null output handle accepted");
    const si12t_config_t no_bus{nullptr, SI12T_GND_ADDRESS};
    check(si12t_init(&no_bus, &handle) == ESP_ERR_INVALID_ARG, "null bus accepted");
    fake::fail_add = true;
    check(si12t_init(&config, &handle) == ESP_ERR_TIMEOUT && !handle && !fake::device.live,
          "I2C initialization failure published a usable device");
    check(si12t_setup(nullptr, SI12T_TYPE_LOW, SI12T_SENSITIVITY_LEVEL_3) == ESP_ERR_INVALID_ARG,
          "null setup handle accepted");
    std::uint8_t output = 0;
    check(si12t_read_touch_result(nullptr, &output) == ESP_ERR_INVALID_ARG, "null read handle accepted");
    check(si12t_delete(nullptr) == ESP_ERR_INVALID_ARG, "null delete handle accepted");
}

void setup_propagates_write_and_read_errors()
{
    for (auto reg : {SI12T_CTRL2_ADDR, SI12T_CH_HOLD2_ADDR, SI12T_CTRL1_ADDR,
                     SI12T_SENSITIVITY3_ADDR, SI12T_SENSITIVITY6_ADDR, SI12T_REF_RST1_ADDR}) {
        Device device;
        fake::fail_write_reg = reg;
        check(device.setup() == ESP_ERR_TIMEOUT, "setup hid a configuration write failure");
    }
    Device device;
    fake::fail_read_reg = SI12T_SENSITIVITY3_ADDR;
    check(device.setup() == ESP_ERR_TIMEOUT, "setup hid a verification read failure");
}

void every_readback_mismatch_rejects_setup()
{
    for (auto reg : {SI12T_SENSITIVITY1_ADDR, SI12T_SENSITIVITY2_ADDR, SI12T_SENSITIVITY3_ADDR,
                     SI12T_SENSITIVITY4_ADDR, SI12T_SENSITIVITY5_ADDR, SI12T_SENSITIVITY6_ADDR, SI12T_CTRL1_ADDR, SI12T_CTRL2_ADDR,
                     SI12T_CH_HOLD1_ADDR, SI12T_CH_HOLD2_ADDR, SI12T_REF_RST1_ADDR, SI12T_REF_RST2_ADDR,
                     SI12T_CAL_HOLD1_ADDR, SI12T_CAL_HOLD2_ADDR}) {
        Device device;
        fake::mismatch_reg = reg;
        check(device.setup() == ESP_ERR_INVALID_STATE, "setup accepted a mismatching sensor register");
    }
}

void output_read_uses_the_real_register_and_preserves_errors()
{
    Device device;
    std::uint8_t output = 0;
    fake::registers[SI12T_OUTPUT1_ADDR] = 0x28;
    check(si12t_read_touch_result(device.handle, &output) == ESP_OK && output == 0x28,
          "touch output did not come from OUTPUT1");
    check(si12t_read_touch_result(device.handle, nullptr) == ESP_ERR_INVALID_ARG,
          "null read destination accepted");
    fake::fail_read_reg = SI12T_OUTPUT1_ADDR;
    check(si12t_read_touch_result(device.handle, &output) == ESP_ERR_TIMEOUT,
          "touch output read failure was reported as valid input");
}
} // namespace

extern "C" {
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t* config,
                                   i2c_master_dev_handle_t* output)
{
    check(bus && config && output, "invalid I2C add arguments");
    check(config->dev_addr_length == I2C_ADDR_BIT_LEN_7 && config->device_address == SI12T_GND_ADDRESS &&
          config->scl_speed_hz == 100000, "unexpected sensor bus configuration");
    if (fake::fail_add) return ESP_ERR_TIMEOUT;
    fake::device.live = true; *output = &fake::device;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    check(device == &fake::device && device->live, "invalid sensor device removal");
    device->live = false;
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const std::uint8_t* data,
                             std::size_t size, int timeout)
{
    check(device == &fake::device && device->live && data && size == 2 && timeout == 100,
          "invalid sensor write transaction");
    fake::writes.emplace_back(data[0], data[1]);
    if (data[0] == fake::fail_write_reg) return ESP_ERR_TIMEOUT;
    fake::registers[data[0]] = data[1];
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const std::uint8_t* address,
                                     std::size_t address_size, std::uint8_t* data, std::size_t size, int timeout)
{
    check(device == &fake::device && device->live && address && address_size == 1 && data && size == 1 && timeout == 100,
          "invalid sensor read transaction");
    ++fake::reads[*address];
    if (*address == fake::fail_read_reg) return ESP_ERR_TIMEOUT;
    *data = fake::registers[*address] ^ (*address == fake::mismatch_reg ? 1 : 0);
    return ESP_OK;
}
}

int main()
{
    try {
        setup_preserves_required_bits_and_calibrates_after_configuration();
        three_channels_and_sleep_options_preserve_independent_settings();
        channel_mask_maps_each_ts_pin_without_changing_required_bits();
        invalid_setup_options_do_not_touch_hardware();
        configured_setup_checks_final_channel_sleep_and_sensitivity_readback();
        initialization_and_null_inputs_fail_safely();
        setup_propagates_write_and_read_errors();
        every_readback_mismatch_rejects_setup();
        output_read_uses_the_real_register_and_preserves_errors();
        std::cout << "si12t_driver_test: 9 scenarios, " << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "si12t_driver_test: " << error.what() << '\n';
        return 1;
    }
}
