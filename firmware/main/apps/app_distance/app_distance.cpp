#include "app_distance.h"
#include "pet_avatar.h"
#include "nfc_service.h"
#include "intro_player.h"

#include <assets/assets.h>
#include <apps/common/idle_program/service.h>
#include <apps/common/idle_program/view.h>
#include <distance_sensor/service.h>
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <lvgl_image.h>
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
    enum class Mode { Live, Tune, Nfc, Idle };

    struct Row {
        std::unique_ptr<Label> label;
        std::unique_ptr<Label> value;
        std::unique_ptr<Button> minus;
        std::unique_ptr<Button> plus;
    };

    // Parents precede their child wrappers so destruction releases children first.
    std::unique_ptr<Container> live_controls;
    std::unique_ptr<Container> tune_page;
    std::unique_ptr<Container> intro_page;
    lv_obj_t* intro_image = nullptr;
    std::unique_ptr<Button> intro_home;
    std::unique_ptr<Label> intro_status;
    std::shared_ptr<LvglAllocatedImage> intro_frame;
    std::unique_ptr<Button> live_home;
    std::unique_ptr<Button> tune_button;
    std::unique_ptr<Label> live_distance;
    std::unique_ptr<Label> live_state;
    std::unique_ptr<Label> live_nfc_status;
    std::unique_ptr<Label> live_eye_status;
    std::unique_ptr<Button> tune_home;
    std::unique_ptr<Button> live_button;
    std::unique_ptr<Button> mode_button;
    std::unique_ptr<Label> telemetry;
    std::array<Row, 3> rows;
    std::unique_ptr<Button> save_button;
    std::unique_ptr<Label> save_status;
    std::unique_ptr<Label> apply_status;
    std::unique_ptr<Label> nfc_hint;
    std::unique_ptr<Label> nfc_counts;
    std::unique_ptr<Label> nfc_uid;

    Mode mode = Mode::Tune;
    Mode requested_mode = Mode::Live;
    DistancePetAvatar* live_avatar = nullptr;
    bool save_requested = false;
    bool save_idle_requested = false;
    std::uint32_t edit_revision = 0;
    std::uint32_t idle_edit_revision = 0;
    bool intro_visible = false;

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
        live_nfc_status = make_label(*live_controls, 8, 8, 304, &lv_font_montserrat_14, kMuted);
        live_nfc_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
        live_nfc_status->setText("NFC STARTING");
        live_eye_status = make_label(*live_controls, 8, 28, 304, &lv_font_montserrat_14, kMuted);
        live_eye_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
        live_eye_status->setText("EYE OFF");

        tune_page = make_page();
        tune_home = make_button(*tune_page, 8, 4, 80, "HOME");
        tune_home->onClick().connect([&app]() { app.close(); });
        live_button = make_button(*tune_page, 232, 4, 80, "LIVE");
        live_button->onClick().connect([this]() { requested_mode = Mode::Live; });
        mode_button = make_button(*tune_page, 96, 4, 128, "NFC");
        mode_button->onClick().connect([this]() {
            requested_mode = mode == Mode::Tune ? Mode::Nfc : mode == Mode::Nfc ? Mode::Idle : Mode::Tune;
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
        nfc_hint = make_label(*tune_page, 8, 118, 304, &lv_font_montserrat_14, kMuted);
        nfc_hint->setTextAlign(LV_TEXT_ALIGN_CENTER);
        nfc_hint->setText("Tap NFC card to play\nMove away to re-arm");
        nfc_counts = make_label(*tune_page, 8, 198, 304, &lv_font_montserrat_14, kMuted);
        nfc_uid = make_label(*tune_page, 8, 219, 304, &lv_font_montserrat_14, kMuted);
        nfc_hint->addFlag(LV_OBJ_FLAG_HIDDEN);
        nfc_counts->addFlag(LV_OBJ_FLAG_HIDDEN);
        nfc_uid->addFlag(LV_OBJ_FLAG_HIDDEN);
        save_button->onClick().connect([this]() {
            save_requested = true;
            save_idle_requested = mode == Mode::Idle;
            set_enabled(*save_button, false);
            set_save_status("SAVING...", kNear);
        });
        intro_page = make_page();
        intro_page->setBgColor(lv_color_hex(0x000000));
        intro_image = lv_image_create(intro_page->get());
        lv_obj_center(intro_image);
        lv_obj_remove_flag(intro_image, LV_OBJ_FLAG_CLICKABLE);
        intro_home = make_button(*intro_page, 12, 190, 92, "HOME");
        intro_home->onClick().connect([&app]() { app.close(); });
        intro_status = make_label(*intro_page, 8, 100, 304, &lv_font_montserrat_16);
        intro_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
        intro_status->setText("KURO GAMES");
        intro_page->addFlag(LV_OBJ_FLAG_HIDDEN);
        apply_mode();
    }

    ~View()
    {
        // The LVGL object borrows its source descriptor. Delete it before
        // releasing the final shared image reference.
        lv_obj_delete(intro_image);
        intro_image = nullptr;
        intro_frame.reset();
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
        if (mode == Mode::Nfc) nfc_hint->removeFlag(LV_OBJ_FLAG_HIDDEN);
        else nfc_hint->addFlag(LV_OBJ_FLAG_HIDDEN);
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
            const bool nfc = mode == Mode::Nfc;
            const bool idle = mode == Mode::Idle;
            mode_button->label().setText(nfc ? "PROGRAMS" : idle ? "DISTANCE" : "NFC");
            const char* names[] = {nfc ? "READER" : idle ? "PROGRAMS" : "TRIP cm",
                                   idle ? "EYE min" : "CLEAR +cm", idle ? "REST sec" : "WAIT sec"};
            for (std::size_t i = 0; i < rows.size(); ++i) rows[i].label->setText(names[i]);
            for (std::size_t i = 1; i < rows.size(); ++i) {
                for (auto* object : {rows[i].label->get(), rows[i].value->get(),
                                     rows[i].minus->get(), rows[i].plus->get()}) {
                    if (nfc) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
                    else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
                }
            }
            for (auto* object : {save_button->get(), save_status->get(), apply_status->get()}) {
                if (nfc) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
            for (auto* object : {nfc_counts->get(), nfc_uid->get()}) {
                if (nfc) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
            if (!nfc) set_save_status("SAVE to keep", kMuted);
        }
    }

    void update_pet(std::uint32_t now, const distance_sensor::Snapshot& snapshot)
    {
        if (!live_avatar) return;
        auto signal = distance_pet::Signal::Unavailable;
        if (!snapshot.stale && snapshot.valid && snapshot.filtered_mm >= 0) {
            signal = snapshot.proximity_state == distance_sensor::ProximityState::Alert ?
                     distance_pet::Signal::Alert : distance_pet::Signal::Ready;
        }
        for (auto* label : {live_nfc_status.get(), live_eye_status.get()}) {
            if (signal == distance_pet::Signal::Alert) label->addFlag(LV_OBJ_FLAG_HIDDEN);
            else label->removeFlag(LV_OBJ_FLAG_HIDDEN);
        }
        live_avatar->tick(now, signal);
    }

    void show_intro(const intro_player::Snapshot& playback)
    {
        intro_visible = playback.busy;
        if (intro_visible) live_eye_status->addFlag(LV_OBJ_FLAG_HIDDEN);
        if (!intro_visible) {
            lv_image_set_src(intro_image, nullptr);
            intro_frame.reset();
            intro_page->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (playback.frame && playback.frame != intro_frame) {
            lv_image_set_src(intro_image, playback.frame->image_dsc());
            intro_frame = playback.frame;
            lv_obj_center(intro_image);
        }
        if (intro_frame) intro_status->addFlag(LV_OBJ_FLAG_HIDDEN);
        else intro_status->removeFlag(LV_OBJ_FLAG_HIDDEN);
        intro_page->removeFlag(LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(intro_page->get());
    }

    void raise_controls()
    {
        if (intro_visible) {
            lv_obj_move_foreground(intro_page->get());
            return;
        }
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

    void show_reader_status(const distance_sensor::Snapshot& distance,
                            const nfc_sense::Snapshot& scanned)
    {
        // Leave the global TOO CLOSE title unobstructed.
        if (distance.valid && !distance.stale &&
            distance.proximity_state == distance_sensor::ProximityState::Alert) {
            live_nfc_status->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }
        live_nfc_status->removeFlag(LV_OBJ_FLAG_HIDDEN);
        const char* text = "NFC TAP CARD";
        auto color = kSafe;
        if (!scanned.active || !scanned.config.enabled) {
            text = "NFC OFF";
            color = kMuted;
        } else if (scanned.status == nfc_sense::Status::Error) {
            text = "NFC READER ERROR";
            color = kAlert;
        } else if (scanned.status == nfc_sense::Status::Starting) {
            text = "NFC STARTING";
            color = kNear;
        } else if (scanned.waiting_for_removal) {
            text = "NFC MOVE CARD AWAY";
            color = kNear;
        }
        live_nfc_status->setText(text);
        live_nfc_status->setTextColor(lv_color_hex(color));
    }

    void show_eye_status(const idle_program::Snapshot& idle, const distance_sensor::Snapshot& distance,
                         bool playback_busy)
    {
        if (playback_busy || (distance.valid && !distance.stale &&
            distance.proximity_state == distance_sensor::ProximityState::Alert)) {
            live_eye_status->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }
        live_eye_status->removeFlag(LV_OBJ_FLAG_HIDDEN);
        char text[32];
        if (!idle.active) {
            std::snprintf(text, sizeof(text), "EYE --");
        } else if (!idle.config.enabled) {
            std::snprintf(text, sizeof(text), "EYE OFF");
        } else if (idle.eye_remaining_ms == 0) {
            std::snprintf(text, sizeof(text), "EYE DUE");
        } else {
            const auto seconds = idle.eye_remaining_ms / 1000 + (idle.eye_remaining_ms % 1000 != 0);
            std::snprintf(text, sizeof(text), "EYE %02lu:%02lu", static_cast<unsigned long>(seconds / 60),
                          static_cast<unsigned long>(seconds % 60));
        }
        live_eye_status->setText(text);
        live_eye_status->setTextColor(lv_color_hex(idle.active && idle.config.enabled &&
                                                  idle.eye_remaining_ms == 0 ? kNear : kMuted));
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
        if (mode == Mode::Idle) {
            auto next = idle_program::config();
            if (row == 0) next.enabled = direction > 0;
            else if (row == 1) next.eye_interval_ms = static_cast<std::uint32_t>(
                std::clamp(static_cast<int>(next.eye_interval_ms / 60000) + direction, 1, 120)) * 60000u;
            else next.rest_duration_ms = static_cast<std::uint32_t>(
                std::clamp(static_cast<int>(next.rest_duration_ms / 1000) + direction, 1, 120)) * 1000u;
            const auto error = idle_program::set_config(next);
            if (error == ESP_OK) {
                ++idle_edit_revision;
                show_idle(next);
                set_save_status("UNSAVED", kNear);
            } else {
                char text[40];
                std::snprintf(text, sizeof(text), "SET ERR 0x%X", static_cast<unsigned>(error));
                set_save_status(text, kAlert);
            }
            return;
        }
        if (mode == Mode::Nfc) {
            if (row != 0) return;
            auto next = nfc_sense::config();
            next.enabled = direction > 0;
            nfc_sense::set_config(next);
            show_nfc(nfc_sense::snapshot());
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

    void show_idle(const idle_program::Config& config)
    {
        char text[24];
        telemetry->setText("IDLE ROTATION");
        telemetry->setTextColor(lv_color_hex(kForeground));
        rows[0].value->setText(config.enabled ? "ON" : "OFF");
        std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(config.eye_interval_ms / 60000));
        rows[1].value->setText(text);
        std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(config.rest_duration_ms / 1000));
        rows[2].value->setText(text);
        set_enabled(*rows[0].minus, config.enabled);
        set_enabled(*rows[0].plus, !config.enabled);
        set_enabled(*rows[1].minus, config.eye_interval_ms > 60000);
        set_enabled(*rows[1].plus, config.eye_interval_ms < 120u * 60000u);
        set_enabled(*rows[2].minus, config.rest_duration_ms > 1000);
        set_enabled(*rows[2].plus, config.rest_duration_ms < 120000);
        apply_status->setText("AUTO - WHEN IDLE");
    }

    void show_nfc(const nfc_sense::Snapshot& scanned)
    {
        char text[96];
        const auto& config = scanned.config;
        std::snprintf(text, sizeof(text), "READER %s", nfc_sense::status_text(scanned.status));
        telemetry->setText(text);
        telemetry->setTextColor(lv_color_hex(scanned.status == nfc_sense::Status::Error ? kAlert : kForeground));
        rows[0].value->setText(config.enabled ? "ON" : "OFF");
        set_enabled(*rows[0].minus, config.enabled);
        set_enabled(*rows[0].plus, !config.enabled);
        std::snprintf(text, sizeof(text), "READS%lu%s TRIG%lu%s",
                      static_cast<unsigned long>(std::min<std::uint32_t>(scanned.reads, 9999)), scanned.reads > 9999 ? "+" : "",
                      static_cast<unsigned long>(std::min<std::uint32_t>(scanned.accepted, 9999)), scanned.accepted > 9999 ? "+" : "");
        nfc_counts->setText(text);
        std::snprintf(text, sizeof(text), "UID --");
        const auto uid_length = std::min(scanned.card.uid_length, scanned.card.uid.size());
        if (uid_length > 0) {
            std::snprintf(text, sizeof(text), "UID ");
            for (std::size_t i = 0; i < uid_length; ++i) {
                std::snprintf(text + 4 + i * 2, sizeof(text) - 4 - i * 2, "%02X",
                              static_cast<unsigned>(scanned.card.uid[i]));
            }
        }
        nfc_uid->setText(text);
    }

    void update(const distance_sensor::Snapshot& snapshot, const distance_sensor::ProximityConfig& requested,
                const nfc_sense::Snapshot& scanned)
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
        if (mode == Mode::Nfc) {
            show_nfc(scanned);
            return;
        }
        if (mode == Mode::Idle) {
            show_idle(idle_program::config());
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

    void finish_save(esp_err_t error, std::uint32_t saved_revision, bool idle)
    {
        set_enabled(*save_button, true);
        if ((mode == Mode::Idle) != idle) return;
        if (error == ESP_OK) {
            const bool newer_edits = saved_revision != (idle ? idle_edit_revision : edit_revision);
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
    nfc_sense::start(); // An unavailable reader must not prevent ranging or UI.
    const auto playback = intro_player::snapshot();
    const auto current = distance_sensor::snapshot();
    const auto scanned = nfc_sense::snapshot();
    _last_nfc_event = scanned.accepted;
    _intro_completed_count = playback.completed_count;
    LvglLockGuard lock;
    _view.reset();
    _view = std::make_unique<View>(*this);
    _view->update(current, distance_sensor::config(), scanned);
    _view->show_reader_status(current, scanned);
    _view->show_eye_status(idle_program::snapshot(), current, playback.busy);
    _last_refresh_ms = GetHAL().millis();
}

void AppDistance::onRunning()
{
    auto playback = intro_player::snapshot();
    const auto current = distance_sensor::snapshot();
    auto scanned = nfc_sense::snapshot();
    const auto now = GetHAL().millis();
    if (playback.completed_count != _intro_completed_count) {
        _intro_completed_count = playback.completed_count;
        _last_nfc_event = playback.sensing_baseline;
    }
    const bool new_event = scanned.accepted != _last_nfc_event;
    _last_nfc_event = scanned.accepted; // Busy events are consumed, never queued.
    const bool nfc_activity = !playback.busy && new_event && now - scanned.last_accepted_ms <= 500;
    if (nfc_activity) {
        idle_program::note_activity();
        if (intro_player::start() == ESP_OK) playback = intro_player::snapshot();
    }
    const bool refresh = now - _last_refresh_ms >= kRefreshMs;
    if (refresh) {
        _last_refresh_ms = now;
    }

    bool save = false;
    bool save_idle = false;
    std::uint32_t saved_revision = 0;
    {
        LvglLockGuard lock;
        if (!_view) {
            return;
        }
        const auto previous_mode = _view->mode;
        _view->apply_mode();
        _view->update_pet(now, current);
        GetStackChan().update();
        _view->show_intro(playback);
        _view->raise_controls();
        idle_program::set_context({
            _view->mode == View::Mode::Live,
            playback.busy || _view->mode != View::Mode::Live,
            current.valid && !current.stale && current.proximity_state == distance_sensor::ProximityState::Alert});
        if (nfc_activity || playback.busy || _view->mode != View::Mode::Live ||
            (current.valid && !current.stale && current.proximity_state == distance_sensor::ProximityState::Alert)) {
            idle_program::clear_locked();
        }
        if (refresh || previous_mode != _view->mode) {
            _view->update(current, distance_sensor::config(), scanned);
            _view->show_reader_status(current, scanned);
            _view->show_eye_status(idle_program::snapshot(), current, playback.busy);
        }
        save = _view->save_requested;
        save_idle = _view->save_idle_requested;
        _view->save_requested = false;
        saved_revision = save_idle ? _view->idle_edit_revision : _view->edit_revision;
    }

    if (save) {
        // NVS work runs on the app task, outside both the event callback and
        // the LVGL lock. Parameter button edits never write flash implicitly.
        const auto error = save_idle ? idle_program::save_config() : distance_sensor::save_config();
        LvglLockGuard lock;
        if (_view) {
            _view->finish_save(error, saved_revision, save_idle);
        }
    }
}

void AppDistance::onClose()
{
    idle_program::set_context({false, false, false});
    // Never hold the display lock while waiting for input or playback workers.
    nfc_sense::stop();
    intro_player::stop();
    if (!_view) {
        return;
    }
    LvglLockGuard lock;
    idle_program::clear_locked();
    _view.reset();
}
