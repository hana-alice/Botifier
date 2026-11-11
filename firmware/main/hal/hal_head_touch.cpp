/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "drivers/Si12T/Si12T.h"
#include "board/hal_bridge.h"
#include "head_touch_filter.h"
#include <distance_sensor/service.h>
#include <mooncake_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cinttypes>
#include <cstdio>

static const std::string_view _tag = "HAL-HeadTouch";

// 触摸状态
enum class TouchState { IDLE, TOUCHED, SWIPING };

// 配置参数
struct TouchConfig {
    uint8_t touch_threshold = 1;
    int16_t swipe_threshold = 40;  // 使用百分比，范围-100到100
};

// 触摸数据
struct TouchData {
    uint8_t intensity[3];
    uint32_t timestamp;

    // 计算位置（返回-100到100的整数）
    int16_t get_position() const
    {
        uint16_t total = intensity[0] + intensity[1] + intensity[2];
        if (total == 0) return 0;

        int32_t weighted = intensity[0] * (-100) + intensity[1] * 0 + intensity[2] * 100;
        return static_cast<int16_t>(weighted / total);
    }

    uint8_t get_max_intensity() const
    {
        uint8_t max_val = intensity[0];
        if (intensity[1] > max_val) max_val = intensity[1];
        if (intensity[2] > max_val) max_val = intensity[2];
        return max_val;
    }

    bool is_touched() const
    {
        return get_max_intensity() >= 1;
    }
};

// 手势识别器类
class GestureRecognizer {
public:
    GestureRecognizer() : current_state(TouchState::IDLE), initial_position(0)
    {
    }

    // 更新状态机，返回识别到的手势
    HeadPetGesture update(const TouchData& data)
    {
        HeadPetGesture gesture = HeadPetGesture::None;

        switch (current_state) {
            case TouchState::IDLE:
                if (data.is_touched()) {
                    current_state    = TouchState::TOUCHED;
                    initial_position = data.get_position();
                    gesture          = HeadPetGesture::Press;
                    // mclog::tagInfo(_tag, "Touch detected at position: {}", initial_position);
                }
                break;

            case TouchState::TOUCHED:
                if (!data.is_touched()) {
                    current_state = TouchState::IDLE;
                    gesture       = HeadPetGesture::Release;
                } else {
                    // Check for swipe
                    int16_t current_pos = data.get_position();
                    int16_t delta       = current_pos - initial_position;

                    if (delta > config.swipe_threshold) {
                        current_state = TouchState::SWIPING;
                        gesture       = HeadPetGesture::SwipeForward;
                        // mclog::tagInfo(_tag, "Swipe forward detected, delta: {}", delta);
                    } else if (delta < -config.swipe_threshold) {
                        current_state = TouchState::SWIPING;
                        gesture       = HeadPetGesture::SwipeBackward;
                        // mclog::tagInfo(_tag, "Swipe backward detected, delta: {}", delta);
                    }
                }
                break;

            case TouchState::SWIPING:
                if (!data.is_touched()) {
                    current_state = TouchState::IDLE;
                    gesture       = HeadPetGesture::Release;
                }
                break;
        }

        return gesture;
    }

    void set_config(const TouchConfig& cfg)
    {
        config = cfg;
    }

private:
    TouchConfig config;
    TouchState current_state;
    int16_t initial_position;
};

static void _head_touch_update_task(void* param)
{
    mclog::tagInfo(_tag, "start update task");

    si12t_handle_t si12t = (si12t_handle_t)param;
    uint8_t touch_result = 0;
    uint8_t filtered_result = 0;
    TouchData data{};
    head_touch::Filter input;
    uint32_t raw_edges = 0, presses = 0, read_errors = 0, last_log = 0;
    uint8_t previous_raw = 0;

    GestureRecognizer recognizer;
    HeadPetGesture gesture;

    // VM is enabled before this task starts. Allow the power rail to settle
    // before resetting the touch reference; do not block the UI's boot path.
    vTaskDelay(pdMS_TO_TICKS(1500));
    // Only TS1-TS3 are head electrodes on the StackChan touch board. In the
    // device A/B capture, LOW3 repeatedly asserted without contact; LOW5 was
    // quiet and switching back to LOW3 reproduced the assertions. Keep sleep
    // enabled and verify this board configuration through register readback.
    const auto setup_error = si12t_setup_with_options(si12t, SI12T_TYPE_LOW,
        SI12T_SENSITIVITY_LEVEL_5, 0x0007, true);
    if (setup_error != ESP_OK) {
        mclog::tagError(_tag, "head touch setup failed: {}", static_cast<int>(setup_error));
        si12t_delete(si12t);
        vTaskDelete(nullptr);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    while (1) {
        // Read data
        const auto error = si12t_read_touch_result(si12t, &touch_result);
        const auto now = GetHAL().millis();
        if (error == ESP_OK) {
            const auto raw = static_cast<uint8_t>(touch_result & 0x3f);
            if ((raw != 0) != (previous_raw != 0)) ++raw_edges;
            previous_raw = raw;
        } else ++read_errors;

        // Update and fire event
        if (input.sample(touch_result, error == ESP_OK, now, filtered_result)) {
            si12t_parse_touch_result_to(filtered_result, data.intensity);
            data.timestamp = now;
            gesture = recognizer.update(data);
            if (gesture != HeadPetGesture::None) {
                if (gesture == HeadPetGesture::Press) {
                    ++presses;
                    distance_sensor::enable_from_head_touch();
                }
                GetHAL().onHeadPetGesture.emit(gesture);
            }
        }
        // Numeric input diagnostics only; bounded to one line per two seconds.
        if (now - last_log >= 2000u) {
            last_log = now;
            std::printf("SC_TOUCH {\"v\":1,\"uptime_ms\":%" PRIu32 ",\"raw\":%u,\"filtered\":%u,"
                        "\"raw_edges\":%" PRIu32 ",\"presses\":%" PRIu32 ",\"read_errors\":%" PRIu32
                        ",\"isolated\":%s,\"suppressed\":%" PRIu32 "}\n",
                        now, static_cast<unsigned>(previous_raw), static_cast<unsigned>(filtered_result),
                        raw_edges, presses, read_errors, input.isolated() ? "true" : "false", input.suppressed_presses());
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Hal::head_touch_init()
{
    mclog::tagInfo(_tag, "init");

    auto i2c_bus = hal_bridge::board_get_i2c_bus();

    si12t_config_t si12t_cfg = {
        .i2c_bus  = i2c_bus,
        .dev_addr = SI12T_GND_ADDRESS,
    };
    static si12t_handle_t si12t;
    const auto error = si12t_init(&si12t_cfg, &si12t);
    if (error != ESP_OK) {
        mclog::tagError(_tag, "head touch setup failed: {}", static_cast<int>(error));
        return;
    }

    // Keep the periodic touch worker's stack internal during local OTA flash
    // writes. Wi-Fi upload exposed a headtouch stack-canary failure with the
    // same 6 KiB stack in PSRAM; the gesture logic and polling stay unchanged.
    xTaskCreatePinnedToCoreWithCaps(_head_touch_update_task, "headtouch", 1024 * 6, si12t, 2, NULL, 1,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
