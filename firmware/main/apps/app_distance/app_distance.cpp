#include "app_distance.h"
#include "pet_avatar.h"
#include "snap_service.h"

#include <assets/assets.h>
#include <distance_sensor/service.h>
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <algorithm>
#include <array>
#include <cstdio>

using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {

constexpr std::uint32_t kBackground = 0x16131C;
constexpr std::uint32_t kForeground = 0xF7F2FF;
constexpr std::uint32_t kMuted = 0xB8B1C3;
constexpr std::uint32_t kButton = 0x513775;
constexpr std::uint32_t kSafe = 0x74E4B5;
constexpr std::uint32_t kNear = 0xFFD37A;
constexpr std::uint32_t kAlert = 0xFF7979;
constexpr std::uint32_t kRefreshMs = 100;

std::unique_ptr<Container> make_page(bool transparent = false)
{
    auto page = std::make_unique<Container>(lv_screen_active());
    page->setSize(320, 240);
    page->align(LV_ALIGN_CENTER, 0, 0);
    page->setPaddingAll(0);
    page->setBorderWidth(0);
    page->setRadius(0);
    page->setBgColor(lv_color_hex(kBackground));
    page->setBgOpa(transparent ? LV_OPA_TRANSP : LV_OPA_COVER);
    page->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    if (transparent) {
        page->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    }
    return page;
}

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

std::unique_ptr<Button> make_button(Container& parent, int x, int y, int width, const char* text)
{
    auto button = std::make_unique<Button>(parent);
    button->setPos(x, y);
    button->setSize(width, 40);
    button->setRadius(6);
    button->setBgColor(lv_color_hex(kButton));
    button->setBorderWidth(0);
    button->label().setTextFont(&lv_font_montserrat_16);
    button->label().setTextColor(lv_color_hex(kForeground));
    button->label().setText(text);
    return button;
}

void set_enabled(Button& button, bool enabled)
{
    if (enabled) {
        button.removeState(LV_STATE_DISABLED);
    } else {
        button.addState(LV_STATE_DISABLED);
    }
}

void format_cm(char* text, std::size_t size, int millimeters)
{
    if (millimeters < 0) {
        std::snprintf(text, size, "--");
    } else {
        std::snprintf(text, size, "%d.%d", millimeters / 10, millimeters % 10);
    }
}

bool same_config(const distance_sensor::ProximityConfig& a, const distance_sensor::ProximityConfig& b)
{
    return a.threshold_mm == b.threshold_mm && a.hysteresis_mm == b.hysteresis_mm &&
           a.near_duration_ms == b.near_duration_ms && a.sample_timeout_ms == b.sample_timeout_ms;
}

const char* state_text(distance_sensor::ProximityState state)
{
    switch (state) {
        case distance_sensor::ProximityState::Safe: return "SAFE";
        case distance_sensor::ProximityState::Near: return "NEAR";
        case distance_sensor::ProximityState::Alert: return "ALERT";
        default: return "WAITING";
    }
}

std::uint32_t state_color(distance_sensor::ProximityState state)
{
    switch (state) {
        case distance_sensor::ProximityState::Safe: return kSafe;
        case distance_sensor::ProximityState::Near: return kNear;
        case distance_sensor::ProximityState::Alert: return kAlert;
        default: return kMuted;
    }
}

}  // namespace

struct AppDistance::View {
    enum class Mode { Live, Tune, Snap };

    struct Row {
        std::unique_ptr<Label> label;
        std::unique_ptr<Label> value;
        std::unique_ptr<Button> minus;
        std::unique_ptr<Button> plus;
    };

    // Parents precede their child wrappers so destruction releases children first.
    std::unique_ptr<Container> live_controls;
    std::unique_ptr<Container> tune_page;
    std::unique_ptr<Button> live_home;
    std::unique_ptr<Button> tune_button;
    std::unique_ptr<Label> live_distance;
    std::unique_ptr<Label> live_state;
    std::unique_ptr<Button> tune_home;
    std::unique_ptr<Button> live_button;
    std::unique_ptr<Button> mode_button;
    std::unique_ptr<Label> telemetry;
    std::array<Row, 3> rows;
    std::unique_ptr<Button> save_button;
    std::unique_ptr<Label> save_status;
    std::unique_ptr<Label> apply_status;
    std::unique_ptr<Label> snap_levels;
    std::unique_ptr<Label> snap_result;

    Mode mode = Mode::Tune;
    Mode requested_mode = Mode::Live;
    DistancePetAvatar* live_avatar = nullptr;
    bool save_requested = false;
    std::uint32_t edit_revision = 0;
    std::uint32_t last_snap_event = 0;

    explicit View(AppDistance& app)
    {
        live_controls = make_page(true);
        live_home = make_button(*live_controls, 12, 190, 92, "HOME");
        live_home->onClick().connect([&app]() { app.close(); });
        tune_button = make_button(*live_controls, 216, 190, 92, "TUNE");
        tune_button->onClick().connect([this]() { requested_mode = Mode::Tune; });
        live_distance = make_label(*live_controls, 108, 193, 104, &lv_font_montserrat_16);
        live_state = make_label(*live_controls, 108, 214, 104, &lv_font_montserrat_14);
        live_distance->setTextAlign(LV_TEXT_ALIGN_CENTER);
        live_state->setTextAlign(LV_TEXT_ALIGN_CENTER);

        tune_page = make_page();
        tune_home = make_button(*tune_page, 8, 4, 80, "HOME");
        tune_home->onClick().connect([&app]() { app.close(); });
        live_button = make_button(*tune_page, 232, 4, 80, "LIVE");
        live_button->onClick().connect([this]() { requested_mode = Mode::Live; });
        mode_button = make_button(*tune_page, 96, 4, 128, "SNAP");
        mode_button->onClick().connect([this]() {
            requested_mode = mode == Mode::Snap ? Mode::Tune : Mode::Snap;
        });
        telemetry = make_label(*tune_page, 8, 47, 304, &lv_font_montserrat_14);
        telemetry->setTextAlign(LV_TEXT_ALIGN_CENTER);

        const char* names[] = {"TRIP cm", "CLEAR +cm", "WAIT sec"};
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const int y = 70 + static_cast<int>(i) * 42;
            auto& row = rows[i];
            row.label = make_label(*tune_page, 12, y + 12, 96, &lv_font_montserrat_14, kMuted);
            row.label->setText(names[i]);
            row.minus = make_button(*tune_page, 112, y, 44, "-");
            row.value = make_label(*tune_page, 160, y + 7, 96, &MontserratSemiBold26);
            row.value->setTextAlign(LV_TEXT_ALIGN_CENTER);
            row.plus = make_button(*tune_page, 264, y, 44, "+");
            // LVGL already holds its lock while dispatching these callbacks.
            row.minus->onClick().connect([this, i]() { adjust(i, -1); });
            row.plus->onClick().connect([this, i]() { adjust(i, 1); });
        }

        save_button = make_button(*tune_page, 8, 196, 80, "SAVE");
        save_status = make_label(*tune_page, 98, 198, 214, &lv_font_montserrat_14, kMuted);
        save_status->setText("SAVE to keep");
        apply_status = make_label(*tune_page, 98, 219, 214, &lv_font_montserrat_14, kMuted);
        snap_levels = make_label(*tune_page, 8, 198, 304, &lv_font_montserrat_14, kMuted);
        snap_result = make_label(*tune_page, 8, 219, 304, &lv_font_montserrat_14, kMuted);
        snap_levels->addFlag(LV_OBJ_FLAG_HIDDEN);
        snap_result->addFlag(LV_OBJ_FLAG_HIDDEN);
        save_button->onClick().connect([this]() {
            save_requested = true;
            set_enabled(*save_button, false);
            set_save_status("SAVING...", kNear);
        });
        last_snap_event = snap_sense::snapshot().accepted;
        apply_mode();
    }

    ~View()
    {
        stop_live();
    }

    void stop_live()
    {
        auto& stackchan = GetStackChan();
        if (live_avatar && stackchan.hasAvatar() && &stackchan.avatar() == live_avatar) {
            stackchan.resetAvatar();
        }
        live_avatar = nullptr;
    }

    void apply_mode()
    {
        if (mode == requested_mode) {
            return;
        }
        stop_live();
        mode = requested_mode;
        if (mode == Mode::Live) {
            auto avatar = std::make_unique<DistancePetAvatar>();
            avatar->init(lv_screen_active());
            live_avatar = avatar.get();
            GetStackChan().attachAvatar(std::move(avatar));
            tune_page->addFlag(LV_OBJ_FLAG_HIDDEN);
            live_controls->removeFlag(LV_OBJ_FLAG_HIDDEN);
            raise_controls();
        } else {
            live_controls->addFlag(LV_OBJ_FLAG_HIDDEN);
            tune_page->removeFlag(LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(tune_page->get());
            const bool snap = mode == Mode::Snap;
            mode_button->label().setText(snap ? "DISTANCE" : "SNAP");
            const char* names[] = {snap ? "SENS 1..5" : "TRIP cm",
                                   snap ? "CAM assist" : "CLEAR +cm",
                                   snap ? "LISTEN" : "WAIT sec"};
            for (std::size_t i = 0; i < rows.size(); ++i) rows[i].label->setText(names[i]);
            for (auto* object : {save_button->get(), save_status->get(), apply_status->get()}) {
                if (snap) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
            for (auto* object : {snap_levels->get(), snap_result->get()}) {
                if (snap) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    void update_pet(std::uint32_t now, const distance_sensor::Snapshot& snapshot,
                    const snap_sense::Snapshot& heard)
    {
        const bool new_event = heard.accepted != last_snap_event;
        last_snap_event = heard.accepted;
        if (!live_avatar) return;
        auto signal = distance_pet::Signal::Unavailable;
        if (!snapshot.stale && snapshot.valid && snapshot.filtered_mm >= 0) {
            signal = snapshot.proximity_state == distance_sensor::ProximityState::Alert ?
                     distance_pet::Signal::Alert : distance_pet::Signal::Ready;
        }
        const bool react = new_event && signal == distance_pet::Signal::Ready &&
                           now - heard.last_accepted_ms <= 500;
        live_avatar->tick(now, signal, react);
    }

    void raise_controls()
    {
        // A newly created global warning is a later screen child. Keep the
        // two bottom controls above it without changing the warning itself.
        if (mode == Mode::Live && lv_obj_get_child(lv_screen_active(), -1) != live_controls->get()) {
            lv_obj_move_foreground(live_controls->get());
        }
    }

    void set_save_status(const char* text, std::uint32_t color)
    {
        save_status->setText(text);
        save_status->setTextColor(lv_color_hex(color));
    }

    void show_config(const distance_sensor::ProximityConfig& configuration)
    {
        char text[24];
        format_cm(text, sizeof(text), configuration.threshold_mm);
        rows[0].value->setText(text);
        format_cm(text, sizeof(text), configuration.hysteresis_mm);
        rows[1].value->setText(text);
        std::snprintf(text, sizeof(text), "%lu.%lu", static_cast<unsigned long>(configuration.near_duration_ms / 1000),
                      static_cast<unsigned long>((configuration.near_duration_ms % 1000) / 100));
        rows[2].value->setText(text);
        set_enabled(*rows[0].minus, configuration.threshold_mm > 40);
        set_enabled(*rows[0].plus, configuration.threshold_mm < 4000);
        set_enabled(*rows[1].minus, configuration.hysteresis_mm > 0);
        set_enabled(*rows[1].plus, configuration.hysteresis_mm < 4000 - configuration.threshold_mm);
        set_enabled(*rows[2].minus, configuration.near_duration_ms > 0);
        set_enabled(*rows[2].plus, configuration.near_duration_ms < 10000);
    }

    void adjust(std::size_t row, int direction)
    {
        if (mode == Mode::Snap) {
            auto next = snap_sense::config();
            if (row == 0) next.sensitivity = static_cast<unsigned>(
                std::clamp(static_cast<int>(next.sensitivity) + direction, 1, 5));
            else if (row == 1) next.camera_required = direction > 0;
            else next.listening = direction > 0;
            snap_sense::set_config(next);
            show_snap(snap_sense::snapshot());
            return;
        }
        const auto previous = distance_sensor::config();
        auto next = previous;
        if (row == 0) {
            next.threshold_mm = static_cast<std::uint16_t>(
                std::clamp(static_cast<int>(next.threshold_mm) + direction * 10, 40, 4000));
            next.hysteresis_mm = static_cast<std::uint16_t>(std::min<int>(next.hysteresis_mm, 4000 - next.threshold_mm));
        } else if (row == 1) {
            next.hysteresis_mm = static_cast<std::uint16_t>(
                std::clamp(static_cast<int>(next.hysteresis_mm) + direction * 10, 0, 4000 - next.threshold_mm));
        } else {
            next.near_duration_ms = static_cast<std::uint32_t>(
                std::clamp(static_cast<int>(next.near_duration_ms) + direction * 100, 0, 10000));
        }
        if (same_config(previous, next)) {
            return;
        }
        const auto error = distance_sensor::set_config(next);
        if (error == ESP_OK) {
            ++edit_revision;
            show_config(next);
            set_save_status("UNSAVED", kNear);
            apply_status->setText("APPLYING NEXT SAMPLE");
        } else {
            char text[40];
            std::snprintf(text, sizeof(text), "SET ERR 0x%X", static_cast<unsigned int>(error));
            set_save_status(text, kAlert);
        }
    }

    void show_snap(const snap_sense::Snapshot& heard)
    {
        char text[96];
        const auto& config = heard.config;
        std::snprintf(text, sizeof(text), "MIC %s  CAM %s", snap_sense::status_text(heard.status),
                      snap_sense::camera_status_text(heard.camera_status));
        telemetry->setText(text);
        telemetry->setTextColor(lv_color_hex(heard.status == snap_sense::Status::AudioError ? kAlert : kForeground));
        std::snprintf(text, sizeof(text), "%u", config.sensitivity);
        rows[0].value->setText(text);
        rows[1].value->setText(config.camera_required ? "ON" : "OFF");
        rows[2].value->setText(config.listening ? "ON" : "OFF");
        set_enabled(*rows[0].minus, config.sensitivity > 1);
        set_enabled(*rows[0].plus, config.sensitivity < 5);
        set_enabled(*rows[1].minus, config.camera_required);
        set_enabled(*rows[1].plus, !config.camera_required);
        set_enabled(*rows[2].minus, config.listening);
        set_enabled(*rows[2].plus, !config.listening);
        std::snprintf(text, sizeof(text), "P%lu R%lu N%lu", static_cast<unsigned long>(heard.audio.peak),
                      static_cast<unsigned long>(heard.audio.rms), static_cast<unsigned long>(heard.audio.noise));
        snap_levels->setText(text);
        std::snprintf(text, sizeof(text), "OK%lu%s NO%lu%s M%u %s",
                      static_cast<unsigned long>(std::min<std::uint32_t>(heard.accepted, 9999)), heard.accepted > 9999 ? "+" : "",
                      static_cast<unsigned long>(std::min<std::uint32_t>(heard.rejected, 9999)), heard.rejected > 9999 ? "+" : "",
                      heard.visual.changed_cells,
                      snap_sense::decision_text(heard.last_decision));
        snap_result->setText(text);
    }

    void update(const distance_sensor::Snapshot& snapshot, const distance_sensor::ProximityConfig& requested,
                const snap_sense::Snapshot& heard)
    {
        char text[96];
        char centimeters[24];
        char raw_centimeters[24];
        const bool usable = snapshot.valid && !snapshot.stale;
        const char* quality = snapshot.stale ? "STALE" : snapshot.valid ? "VALID" : "INVALID";
        const auto color = state_color(snapshot.proximity_state);
        format_cm(centimeters, sizeof(centimeters), usable ? snapshot.filtered_mm : -1);
        std::snprintf(text, sizeof(text), "%s cm", centimeters);
        live_distance->setText(text);
        live_state->setText(usable ? state_text(snapshot.proximity_state) : quality);
        live_state->setTextColor(lv_color_hex(usable ? color : kNear));
        if (mode == Mode::Snap) {
            show_snap(heard);
            return;
        }
        format_cm(raw_centimeters, sizeof(raw_centimeters), snapshot.stale ? -1 : snapshot.raw_mm);
        std::snprintf(text, sizeof(text), "F%s R%scm %s %s", centimeters, raw_centimeters,
                      state_text(snapshot.proximity_state), quality);
        telemetry->setText(text);
        telemetry->setTextColor(lv_color_hex(color));
        show_config(requested);
        if (!same_config(snapshot.config, requested)) {
            apply_status->setText("APPLYING NEXT SAMPLE");
        } else {
            format_cm(centimeters, sizeof(centimeters),
                      static_cast<int>(requested.threshold_mm) + requested.hysteresis_mm);
            std::snprintf(text, sizeof(text), "CLEAR >=%s cm", centimeters);
            apply_status->setText(text);
        }
    }

    void finish_save(esp_err_t error, std::uint32_t saved_revision)
    {
        set_enabled(*save_button, true);
        if (error == ESP_OK) {
            const bool newer_edits = saved_revision != edit_revision;
            set_save_status(newer_edits ? "SAVED; NEWER EDITS" : "SAVED", newer_edits ? kNear : kSafe);
        } else {
            char text[40];
            std::snprintf(text, sizeof(text), "SAVE ERR 0x%X", static_cast<unsigned int>(error));
            set_save_status(text, kAlert);
        }
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
    snap_sense::start(); // An unavailable microphone must not prevent ranging or UI.
    const auto current = distance_sensor::snapshot();
    const auto heard = snap_sense::snapshot();
    LvglLockGuard lock;
    _view.reset();
    _view = std::make_unique<View>(*this);
    _view->update(current, distance_sensor::config(), heard);
    _last_refresh_ms = GetHAL().millis();
}

void AppDistance::onRunning()
{
    const auto current = distance_sensor::snapshot();
    const auto heard = snap_sense::snapshot();
    const auto now = GetHAL().millis();
    const bool refresh = now - _last_refresh_ms >= kRefreshMs;
    if (refresh) {
        _last_refresh_ms = now;
    }

    bool save = false;
    std::uint32_t saved_revision = 0;
    {
        LvglLockGuard lock;
        if (!_view) {
            return;
        }
        const auto previous_mode = _view->mode;
        _view->apply_mode();
        _view->update_pet(now, current, heard);
        GetStackChan().update();
        _view->raise_controls();
        if (refresh || previous_mode != _view->mode) {
            _view->update(current, distance_sensor::config(), heard);
        }
        save = _view->save_requested;
        _view->save_requested = false;
        saved_revision = _view->edit_revision;
    }

    if (save) {
        // NVS work runs on the app task, outside both the event callback and
        // the LVGL lock. Parameter button edits never write flash implicitly.
        const auto error = distance_sensor::save_config();
        LvglLockGuard lock;
        if (_view) {
            _view->finish_save(error, saved_revision);
        }
    }
}

void AppDistance::onClose()
{
    // Input handoff can wait for I2S. Never hold the display lock while stopping.
    snap_sense::stop();
    if (!_view) {
        return;
    }
    LvglLockGuard lock;
    _view.reset();
}
