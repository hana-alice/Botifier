#include "Si12T.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "Si12T";

#define I2C_TIMEOUT_MS 100

/**
 * @brief Si12T设备结构体
 */
struct si12t_dev_t {
    i2c_master_dev_handle_t i2c_dev; /*!< I2C设备句柄 */
    uint8_t dev_addr;                /*!< 设备地址 */
};

uint8_t si12t_point_type[3] = {SI12T_OUTPUT_NONE, SI12T_OUTPUT_NONE, SI12T_OUTPUT_NONE};

/**
 * @brief 写寄存器
 */
static esp_err_t si12t_i2c_write_reg(si12t_handle_t handle, uint8_t reg_addr, uint8_t value)
{
    uint8_t write_buf[2] = {reg_addr, value};
    return i2c_master_transmit(handle->i2c_dev, write_buf, sizeof(write_buf), I2C_TIMEOUT_MS);
}

/**
 * @brief 读寄存器
 */
static esp_err_t si12t_i2c_read_reg(si12t_handle_t handle, uint8_t reg_addr, uint8_t *value)
{
    esp_err_t ret = i2c_master_transmit_receive(handle->i2c_dev, &reg_addr, 1, value, 1, I2C_TIMEOUT_MS);
    ESP_LOGD(TAG, "Read reg 0x%02x, value: 0x%02x", reg_addr, *value);
    return ret;
}

/**
 * @brief 设置所有灵敏度寄存器
 */
static esp_err_t si12t_set_sens(si12t_handle_t handle, uint8_t value)
{
    // Datasheet 12.2.1: SEN1 through SEN6 cover TS1 through TS12.
    for (uint8_t reg = SI12T_SENSITIVITY1_ADDR; reg <= SI12T_SENSITIVITY6_ADDR; ++reg) {
        const auto ret = si12t_i2c_write_reg(handle, reg, value);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

static esp_err_t si12t_configure_channels(si12t_handle_t handle, uint16_t active_channels)
{
    // Datasheet 12.2.5: one holds both sensing and calibration for that channel.
    const uint16_t held = static_cast<uint16_t>(~active_channels & 0x0fffu);
    const uint8_t regs[] = {SI12T_REF_RST1_ADDR, SI12T_REF_RST2_ADDR,
        SI12T_CH_HOLD1_ADDR, SI12T_CH_HOLD2_ADDR, SI12T_CAL_HOLD1_ADDR, SI12T_CAL_HOLD2_ADDR};
    const uint8_t values[] = {0x00, 0x00, static_cast<uint8_t>(held),
        static_cast<uint8_t>(0x30u | (held >> 8)), 0x00, 0x00};
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        const auto ret = si12t_i2c_write_reg(handle, regs[i], values[i]);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

static esp_err_t si12t_configure_ctrl2(si12t_handle_t handle, bool sleep_enabled)
{
    // CTRL bit 3 asserts digital reset; bit 2 allows sleep, bits 1:0 stay one.
    const uint8_t ctrl = sleep_enabled ? 0x07 : 0x03;
    auto ret = si12t_i2c_write_reg(handle, SI12T_CTRL2_ADDR, static_cast<uint8_t>(ctrl | 0x08u));
    if (ret != ESP_OK) return ret;
    return si12t_i2c_write_reg(handle, SI12T_CTRL2_ADDR, ctrl);
}

esp_err_t si12t_init(const si12t_config_t *config, si12t_handle_t *handle)
{
    if (config == NULL || handle == NULL || config->i2c_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    si12t_handle_t dev = (si12t_handle_t)calloc(1, sizeof(struct si12t_dev_t));
    if (dev == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for Si12T device");
        return ESP_ERR_NO_MEM;
    }

    dev->dev_addr = config->dev_addr ? config->dev_addr : SI12T_GND_ADDRESS;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = dev->dev_addr,
        .scl_speed_hz    = 100000,  // 100kHz
    };

    esp_err_t ret = i2c_master_bus_add_device(config->i2c_bus, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        free(dev);
        return ret;
    }

    *handle = dev;
    ESP_LOGI(TAG, "Si12T initialized, version: %s", SI12T_VERSION);
    return ESP_OK;
}

esp_err_t si12t_setup(si12t_handle_t handle, si12t_type_t sens_type, si12t_sensitivity_level_t sens_level)
{
    return si12t_setup_with_options(handle, sens_type, sens_level, 0x0fff, true);
}

esp_err_t si12t_setup_with_options(si12t_handle_t handle, si12t_type_t sens_type,
                                   si12t_sensitivity_level_t sens_level,
                                   uint16_t active_channels, bool sleep_enabled)
{
    // Reject invalid options before a reset or any other write reaches hardware.
    if (handle == NULL || active_channels == 0 || (active_channels & 0xf000u) != 0 ||
        (sens_type != SI12T_TYPE_LOW && sens_type != SI12T_TYPE_HIGH) ||
        sens_level < SI12T_SENSITIVITY_LEVEL_0 || sens_level >= SI12T_SENSITIVITY_LEVEL_INVALID) {
        return ESP_ERR_INVALID_ARG;
    }

    // Reset first; establish the reference only after the final configuration.
    esp_err_t ret = si12t_configure_ctrl2(handle, sleep_enabled);
    if (ret != ESP_OK) return ret;
    ret = si12t_configure_channels(handle, active_channels);
    if (ret != ESP_OK) return ret;
    ret = si12t_set_ctrl1(handle);
    if (ret != ESP_OK) return ret;
    ret = si12t_set_sensitivity(handle, sens_type, sens_level);
    if (ret != ESP_OK) return ret;

    // Datasheet 12.2.4: pulse reference reset only for channels in operation.
    const uint8_t reset_regs[] = {SI12T_REF_RST1_ADDR, SI12T_REF_RST2_ADDR,
                                  SI12T_REF_RST1_ADDR, SI12T_REF_RST2_ADDR};
    const uint8_t reset_values[] = {static_cast<uint8_t>(active_channels),
        static_cast<uint8_t>(active_channels >> 8), 0x00, 0x00};
    for (unsigned i = 0; i < sizeof(reset_regs); ++i) {
        ret = si12t_i2c_write_reg(handle, reset_regs[i], reset_values[i]);
        if (ret != ESP_OK) return ret;
    }

    // Read the actual registers, rather than treating a successful write as
    // proof that the sensor accepted its configuration.
    const uint8_t sensitivity = static_cast<uint8_t>(sens_level * 0x11u +
                                    (sens_type == SI12T_TYPE_HIGH ? 0x88u : 0u));
    const uint16_t held = static_cast<uint16_t>(~active_channels & 0x0fffu);
    const uint8_t regs[] = {SI12T_SENSITIVITY1_ADDR, SI12T_SENSITIVITY2_ADDR,
        SI12T_SENSITIVITY3_ADDR, SI12T_SENSITIVITY4_ADDR, SI12T_SENSITIVITY5_ADDR, SI12T_SENSITIVITY6_ADDR,
        SI12T_CTRL1_ADDR, SI12T_CTRL2_ADDR, SI12T_CH_HOLD1_ADDR, SI12T_CH_HOLD2_ADDR,
        SI12T_REF_RST1_ADDR, SI12T_REF_RST2_ADDR, SI12T_CAL_HOLD1_ADDR, SI12T_CAL_HOLD2_ADDR};
    const uint8_t expected[] = {sensitivity, sensitivity, sensitivity, sensitivity, sensitivity, sensitivity,
        0x22, static_cast<uint8_t>(sleep_enabled ? 0x07 : 0x03), static_cast<uint8_t>(held),
        static_cast<uint8_t>(0x30u | (held >> 8)), 0x00, 0x00, 0x00, 0x00};
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        uint8_t actual = 0;
        ret = si12t_i2c_read_reg(handle, regs[i], &actual);
        if (ret != ESP_OK) return ret;
        ESP_LOGI(TAG, "Config reg=0x%02x actual=0x%02x expected=0x%02x", regs[i], actual, expected[i]);
        if (actual != expected[i]) return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Si12T setup and reference reset verified");
    return ESP_OK;
}

esp_err_t si12t_delete(si12t_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
    return ret;
}

esp_err_t si12t_get_sensitivity(si12t_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data  = 0;
    esp_err_t ret = ESP_OK;

    ret |= si12t_i2c_read_reg(handle, SI12T_SENSITIVITY1_ADDR, &data);
    ret |= si12t_i2c_read_reg(handle, SI12T_SENSITIVITY2_ADDR, &data);
    ret |= si12t_i2c_read_reg(handle, SI12T_SENSITIVITY3_ADDR, &data);
    ret |= si12t_i2c_read_reg(handle, SI12T_SENSITIVITY4_ADDR, &data);
    ret |= si12t_i2c_read_reg(handle, SI12T_SENSITIVITY5_ADDR, &data);
    ret |= si12t_i2c_read_reg(handle, SI12T_SENSITIVITY6_ADDR, &data);

    return ret;
}

esp_err_t si12t_set_sensitivity(si12t_handle_t handle, si12t_type_t sens_type, si12t_sensitivity_level_t sens_level)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sens_type != SI12T_TYPE_LOW && sens_type != SI12T_TYPE_HIGH) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t value = 0x00;

    if (sens_type == SI12T_TYPE_HIGH) {
        switch (sens_level) {
            case SI12T_SENSITIVITY_LEVEL_0:
                value = 0x88;
                break;
            case SI12T_SENSITIVITY_LEVEL_1:
                value = 0x99;
                break;
            case SI12T_SENSITIVITY_LEVEL_2:
                value = 0xAA;
                break;
            case SI12T_SENSITIVITY_LEVEL_3:
                value = 0xBB;
                break;
            case SI12T_SENSITIVITY_LEVEL_4:
                value = 0xCC;
                break;
            case SI12T_SENSITIVITY_LEVEL_5:
                value = 0xDD;
                break;
            case SI12T_SENSITIVITY_LEVEL_6:
                value = 0xEE;
                break;
            case SI12T_SENSITIVITY_LEVEL_7:
                value = 0xFF;
                break;
            default:
                return ESP_ERR_INVALID_ARG;
        }
    } else {
        switch (sens_level) {
            case SI12T_SENSITIVITY_LEVEL_0:
                value = 0x00;
                break;
            case SI12T_SENSITIVITY_LEVEL_1:
                value = 0x11;
                break;
            case SI12T_SENSITIVITY_LEVEL_2:
                value = 0x22;
                break;
            case SI12T_SENSITIVITY_LEVEL_3:
                value = 0x33;
                break;
            case SI12T_SENSITIVITY_LEVEL_4:
                value = 0x44;
                break;
            case SI12T_SENSITIVITY_LEVEL_5:
                value = 0x55;
                break;
            case SI12T_SENSITIVITY_LEVEL_6:
                value = 0x66;
                break;
            case SI12T_SENSITIVITY_LEVEL_7:
                value = 0x77;
                break;
            default:
                return ESP_ERR_INVALID_ARG;
        }
    }

    ESP_LOGD(TAG, "Set sensitivity value: 0x%02x", value);
    return si12t_set_sens(handle, value);
}

esp_err_t si12t_set_ctrl1(si12t_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t test;
    // sends register data, Auto Mode, FTC=01, Interrupt(Middle,High), Response 4 (2+2)
    esp_err_t ret = si12t_i2c_write_reg(handle, SI12T_CTRL1_ADDR, 0x22);
    si12t_i2c_read_reg(handle, SI12T_CTRL1_ADDR, &test);
    return ret;
}

esp_err_t si12t_set_ctrl2(si12t_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    return si12t_configure_ctrl2(handle, true);
}

esp_err_t si12t_sleep_enable(si12t_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return si12t_i2c_write_reg(handle, SI12T_CTRL2_ADDR, 0x07);  // S/W Reset Enable, Sleep Mode Enable
}

esp_err_t si12t_sleep_disable(si12t_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return si12t_i2c_write_reg(handle, SI12T_CTRL2_ADDR, 0x03);  // S/W Reset Disable, Sleep Mode Disable
}

esp_err_t si12t_enable_channel(si12t_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    return si12t_configure_channels(handle, 0x0fff);
}

esp_err_t si12t_read_touch_result(si12t_handle_t handle, uint8_t *touch_result)
{
    if (handle == NULL || touch_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return si12t_i2c_read_reg(handle, SI12T_OUTPUT1_ADDR, touch_result);
}

void si12t_parse_touch_result(uint8_t touch_result)
{
    int index = 0;
    for (int j = 0; j < 6; j += 2) {
        si12t_point_type[index] = (touch_result >> j) & 0x03;
        index++;
    }
}

void si12t_parse_touch_result_to(uint8_t touch_result, uint8_t *parsed_result)
{
    int index = 0;
    for (int j = 0; j < 6; j += 2) {
        parsed_result[index] = (touch_result >> j) & 0x03;
        index++;
    }
}
