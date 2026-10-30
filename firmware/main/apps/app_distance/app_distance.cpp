#include "app_distance.h"

#include <assets/assets.h>
#include <distance_sensor/service.h>
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <algorithm>
#include <cstdio>

using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {

constexpr std::uint32_t kBackground = 0x16131C;
constexpr std::uint32_t kForeground = 0xF7F2FF;
constexpr std::uint32_t kMuted = 0xB8B1C3;
constexpr std::uint32_t kSafe = 0x74E4B5;
constexpr std::uint32_t kNear = 0xFFD37A;
constexpr std::uint32_t kAlert = 0xFF7979;
constexpr std::uint32_t kRefreshMs = 100;

std::unique_ptr<Label> make_label(Container& parent, int x, int y, int width,
                                 const lv_font_t* font, std::uint32_t color = kForeground)
{
    auto label = std::make_unique<Label>(parent);
    label->setPos(x, y);
    label->setWidth(width);
    label->setTextFont(font);
    label->setTextColor(lv_color_hex(color));
    label->setLongMode(LV_LABEL_LONG_MODE_CLIP);
    return label;
}

void format_cm(char* text, std::size_t size, int millimeters)
{
    if (millimeters < 0) {
        std::snprintf(text, size, "--");
        return;
    }
    std::snprintf(text, size, "%d.%d", millimeters / 10, millimeters % 10);
}

}  // namespace

struct AppDistance::View {
    // The parent is declared first so wrappers release their children first.
    std::unique_ptr<Container> page;
    std::unique_ptr<Button> home;
    std::unique_ptr<Label> title;
    std::unique_ptr<Label> filtered_caption;
    std::unique_ptr<Label> filtered;
    std::unique_ptr<Label> unit;
    std::unique_ptr<Label> state;
    std::unique_ptr<Label> raw;
    std::unique_ptr<Label> quality;
    std::unique_ptr<Label> hold;
    std::unique_ptr<Label> limits;
    std::unique_ptr<Label> sample;
    std::unique_ptr<Label> sensor;
    std::unique_ptr<Label> status;

    explicit View(AppDistance& app)
    {
        page = std::make_unique<Container>(lv_screen_active());
        page->setSize(320, 240);
        page->align(LV_ALIGN_CENTER, 0, 0);
        page->setPaddingAll(0);
        page->setBorderWidth(0);
        page->setRadius(0);
        page->setBgColor(lv_color_hex(kBackground));
        page->setBgOpa(LV_OPA_COVER);
        page->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        title = make_label(*page, 12, 13, 200, &lv_font_montserrat_20);
        title->setText("DISTANCE");

        home = std::make_unique<Button>(*page);
        home->setPos(226, 4);
        home->setSize(84, 40);
        home->setRadius(6);
        home->setBgColor(lv_color_hex(0x513775));
        home->setBorderWidth(0);
        home->label().setTextFont(&lv_font_montserrat_16);
        home->label().setTextColor(lv_color_hex(kForeground));
        home->label().setText("HOME");
        home->onClick().connect([&app]() { app.close(); });

        filtered_caption = make_label(*page, 12, 47, 150, &lv_font_montserrat_14, kMuted);
        filtered_caption->setText("FILTERED");
        filtered = make_label(*page, 12, 67, 100, &MontserratSemiBold26);
        filtered->setTransformPivot(0, 0);
        // Reuse the existing font: LVGL transform units use 256 for 1x.
        lv_obj_set_style_transform_scale_x(filtered->get(), 384, LV_PART_MAIN);
        lv_obj_set_style_transform_scale_y(filtered->get(), 384, LV_PART_MAIN);
        unit = make_label(*page, 170, 82, 40, &lv_font_montserrat_20, kMuted);
        unit->setText("cm");
        state = make_label(*page, 211, 70, 99, &lv_font_montserrat_20);
        state->setTextAlign(LV_TEXT_ALIGN_RIGHT);

        raw = make_label(*page, 12, 114, 190, &lv_font_montserrat_16);
        quality = make_label(*page, 210, 115, 100, &lv_font_montserrat_14);
        quality->setTextAlign(LV_TEXT_ALIGN_RIGHT);
        hold = make_label(*page, 12, 138, 298, &lv_font_montserrat_16);
        limits = make_label(*page, 12, 163, 298, &lv_font_montserrat_14, kMuted);
        sample = make_label(*page, 12, 183, 298, &lv_font_montserrat_14, kMuted);
        sensor = make_label(*page, 12, 202, 298, &lv_font_montserrat_14, kMuted);
        status = make_label(*page, 12, 221, 298, &lv_font_montserrat_14, kMuted);
    }

    void update(const distance_sensor::Snapshot& snapshot)
    {
        char text[128];
        char raw_cm[24];
        char trigger_cm[24];
        char clear_cm[24];
        const bool usable = snapshot.valid && !snapshot.stale;
        format_cm(text, sizeof(text), usable ? snapshot.filtered_mm : -1);
        filtered->setText(text);
        filtered->setTextColor(lv_color_hex(usable && snapshot.filtered_mm >= 0 ? kForeground : kMuted));

        const char* state_text = "WAITING";
        std::uint32_t state_color = kMuted;
        switch (snapshot.proximity_state) {
            case distance_sensor::ProximityState::Waiting:
                break;
            case distance_sensor::ProximityState::Safe:
                state_text = "SAFE";
                state_color = kSafe;
                break;
            case distance_sensor::ProximityState::Near:
                state_text = "NEAR";
                state_color = kNear;
                break;
            case distance_sensor::ProximityState::Alert:
                state_text = "ALERT";
                state_color = kAlert;
                break;
        }
        state->setText(state_text);
        state->setTextColor(lv_color_hex(state_color));

        // Keep an invalid optical reading visible for diagnosis; snapshot()
        // itself clears old raw values when the sampler becomes stale.
        format_cm(raw_cm, sizeof(raw_cm), snapshot.raw_mm);
        std::snprintf(text, sizeof(text), "RAW %s cm", raw_cm);
        raw->setText(text);
        quality->setText(snapshot.stale ? "STALE" : snapshot.valid ? "VALID" : "INVALID");
        quality->setTextColor(lv_color_hex(usable ? kSafe : kNear));

        const auto elapsed = std::min(snapshot.near_elapsed_ms, snapshot.config.near_duration_ms);
        std::snprintf(text, sizeof(text), "HOLD %lu / %lu ms", static_cast<unsigned long>(elapsed),
                      static_cast<unsigned long>(snapshot.config.near_duration_ms));
        hold->setText(text);
        hold->setTextColor(lv_color_hex(state_color));

        format_cm(trigger_cm, sizeof(trigger_cm), snapshot.config.threshold_mm);
        format_cm(clear_cm, sizeof(clear_cm),
                  static_cast<int>(snapshot.config.threshold_mm) + snapshot.config.hysteresis_mm);
        std::snprintf(text, sizeof(text), "TRIP <%s cm | CLEAR >=%s cm", trigger_cm, clear_cm);
        limits->setText(text);
        std::snprintf(text, sizeof(text), "AGE %lu ms  SEQ %lu", static_cast<unsigned long>(snapshot.age_ms),
                      static_cast<unsigned long>(snapshot.sequence));
        sample->setText(text);
        std::snprintf(text, sizeof(text), "SENSOR: %s", snapshot.sensor_state ? snapshot.sensor_state : "unknown");
        sensor->setText(text);
        std::snprintf(text, sizeof(text), "STATUS %d  ERR 0x%X", snapshot.raw_status,
                      static_cast<unsigned int>(snapshot.error));
        status->setText(text);
        status->setTextColor(lv_color_hex(snapshot.error == ESP_OK ? kMuted : kAlert));
    }
};

AppDistance::AppDistance()
{
    setAppInfo().name = "DISTANCE";
    static auto icon = assets::get_image("icon_sentinel.bin");
    setAppInfo().icon = static_cast<void*>(&icon);
    static std::uint32_t theme_color = 0xB77BFF;
    setAppInfo().userData = static_cast<void*>(&theme_color);
}

AppDistance::~AppDistance()
{
    onClose();
}

void AppDistance::onOpen()
{
    // Copy the sampler state before taking the UI lock; no I2C access here.
    const auto current = distance_sensor::snapshot();
    LvglLockGuard lock;
    GetStackChan().resetAvatar();
    _view = std::make_unique<View>(*this);
    _view->update(current);
    _last_refresh_ms = GetHAL().millis();
}

void AppDistance::onRunning()
{
    const auto now = GetHAL().millis();
    const bool refresh = now - _last_refresh_ms >= kRefreshMs;
    distance_sensor::Snapshot current;
    if (refresh) {
        _last_refresh_ms = now;
        current = distance_sensor::snapshot();
    }
    LvglLockGuard lock;
    // Preserve the normal app update cadence for motion, neon and modifiers;
    // only refreshing the diagnostic labels is limited to 10 Hz.
    GetStackChan().update();
    if (refresh && _view) {
        _view->update(current);
    }
}

void AppDistance::onClose()
{
    if (!_view) {
        return;
    }
    LvglLockGuard lock;
    _view.reset();
}
