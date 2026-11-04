#include "app_distance.h"
#include "pet_avatar.h"
#include "nfc_service.h"
#include "intro_player.h"
#include "rest_head_motion.h"
#include "touch_reaction.h"
#include "alert_head_motion.h"
#include "edge_swipe.h"
#include "content_timing.h"

#include <assets/assets.h>
#include <apps/common/codex_weekly/service.h>
#include <apps/common/idle_program/service.h>
#include <apps/common/idle_program/view.h>
#include <distance_sensor/service.h>
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <lvgl_image.h>
#include <esp_log.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cinttypes>
#include <cstring>

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

// Called only by the app task after releasing the LVGL lock. These are content
// update-call timings (rendering may throttle internally); they do not measure
// actual draws, LCD scanout or LVGL's display flush rate.
void emit_content_timing(const distance_pet::ContentTimingBatch& batch)
{
    for (std::uint32_t i = 0; i < batch.count; ++i) {
        const auto& r = batch.reports[i];
        std::printf("SC_FRAME {\"v\":1,\"mode\":0,\"uptime_ms\":%" PRIu32 ",\"generation\":%" PRIu32
                    ",\"window_start_ms\":%" PRIu32
                    ",\"first_uptime_ms\":%" PRIu32 ",\"last_uptime_ms\":%" PRIu32
                    ",\"content_update\":%" PRIu32 ",\"span_ms\":%" PRIu32 ",\"gaps\":%" PRIu32
                    ",\"gap_sum_ms\":%" PRIu32 ",\"gap_max_ms\":%" PRIu32
                    ",\"gap_ge50\":%" PRIu32 ",\"gap_ge100\":%" PRIu32 ",\"tail\":%" PRIu32
                    ",\"content_sum_ms\":%" PRIu32 ",\"content_max_ms\":%" PRIu32
                    ",\"lock_wait_sum_ms\":%" PRIu32 ",\"lock_wait_max_ms\":%" PRIu32
                    ",\"head_before_sum_ms\":%" PRIu32 ",\"head_before_max_ms\":%" PRIu32
                    ",\"head_after_sum_ms\":%" PRIu32 ",\"head_after_max_ms\":%" PRIu32
                    ",\"head_sum_ms\":%" PRIu32 ",\"head_max_ms\":%" PRIu32
                    ",\"stack_sum_ms\":%" PRIu32 ",\"stack_max_ms\":%" PRIu32
                    ",\"render_sum_ms\":%" PRIu32 ",\"render_max_ms\":%" PRIu32 "}\n",
                    static_cast<std::uint32_t>(GetHAL().millis()), r.generation, r.window_start_ms,
                    r.first_uptime_ms, r.last_uptime_ms, r.content_update, r.span_ms, r.gaps,
                    r.gap_sum_ms, r.gap_max_ms, r.gap_ge50, r.gap_ge100, r.tail,
                    r.content.sum_ms, r.content.max_ms, r.lock_wait.sum_ms, r.lock_wait.max_ms,
                    r.head_before.sum_ms, r.head_before.max_ms, r.head_after.sum_ms, r.head_after.max_ms,
                    r.head.sum_ms, r.head.max_ms, r.stack.sum_ms, r.stack.max_ms, r.render.sum_ms, r.render.max_ms);
    }
}

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

const char* codex_state_label(codex_weekly::State state)
{
    using State = codex_weekly::State;
    switch (state) {
        case State::Starting: return "STARTING";
        case State::WaitingWifi: return "WAITING FOR WI-FI";
        case State::WaitingClock: return "WAITING FOR CLOCK";
        case State::SignedOut: return "SIGNED OUT";
        case State::StartingLogin: return "STARTING LOGIN";
        case State::AwaitingLogin: return "ENTER THIS CODE IN YOUR BROWSER";
        case State::Refreshing: return "REFRESHING LOGIN";
        case State::Fetching: return "CHECKING WEEKLY QUOTA";
        case State::Ready: return "SIGNED IN";
        case State::Unavailable: return "WEEKLY QUOTA UNAVAILABLE";
        case State::LoginExpired: return "LOGIN CODE EXPIRED";
        case State::Error: return "ERROR";
    }
    return "UNAVAILABLE";
}

const char* codex_error_label(codex_weekly::Error error)
{
    using Error = codex_weekly::Error;
    switch (error) {
        case Error::Network: return "ERROR: NETWORK";
        case Error::Clock: return "ERROR: CLOCK";
        case Error::Tls: return "ERROR: TLS";
        case Error::Http: return "ERROR: HTTP";
        case Error::RateLimited: return "ERROR: RATE LIMITED";
        case Error::InvalidResponse: return "ERROR: INVALID RESPONSE";
        case Error::AuthExpired: return "ERROR: LOGIN EXPIRED";
        case Error::LoginDisabled: return "ERROR: LOGIN DISABLED";
        case Error::Storage: return "ERROR: STORAGE";
        case Error::Internal: return "ERROR: INTERNAL";
        default: return "ERROR";
    }
}

}  // namespace

struct AppDistance::View {
    enum class Mode { Live, Tune, Nfc, Idle, Codex };

    struct Row {
        std::unique_ptr<Label> label;
        std::unique_ptr<Label> value;
        std::unique_ptr<Button> minus;
        std::unique_ptr<Button> plus;
    };

    // Parents precede their child wrappers so destruction releases children first.
    std::unique_ptr<Container> tune_page;
    std::unique_ptr<Container> codex_page;
    std::unique_ptr<Container> intro_page;
    std::unique_ptr<Container> drawer_root;
    std::unique_ptr<Container> drawer_panel;
    lv_obj_t* intro_image = nullptr;
    std::unique_ptr<Label> intro_status;
    std::shared_ptr<LvglAllocatedImage> intro_frame;
    std::unique_ptr<Button> menu_home;
    std::unique_ptr<Button> menu_live;
    std::unique_ptr<Button> menu_distance;
    std::unique_ptr<Button> menu_nfc;
    std::unique_ptr<Button> menu_programs;
    std::unique_ptr<Button> menu_close;
    std::unique_ptr<Button> menu_codex;
    std::unique_ptr<Label> menu_title;
    std::unique_ptr<Label> live_distance;
    std::unique_ptr<Label> live_state;
    std::unique_ptr<Label> menu_battery;
    std::unique_ptr<Label> live_nfc_status;
    std::unique_ptr<Label> live_eye_status;
    std::unique_ptr<Label> telemetry;
    std::array<Row, 3> rows;
    std::unique_ptr<Button> save_button;
    std::unique_ptr<Label> save_status;
    std::unique_ptr<Label> apply_status;
    std::unique_ptr<Label> nfc_hint;
    std::unique_ptr<Label> nfc_counts;
    std::unique_ptr<Label> nfc_uid;
    std::unique_ptr<Label> codex_title;
    std::unique_ptr<Label> codex_status;
    std::unique_ptr<Label> codex_timing;
    std::unique_ptr<Label> codex_code;
    std::unique_ptr<Label> codex_url;
    std::unique_ptr<Label> codex_ip;
    std::unique_ptr<Label> codex_key;
    std::unique_ptr<Button> codex_sign_in;
    std::unique_ptr<Button> codex_refresh;
    std::unique_ptr<Button> codex_forget;

    Mode mode = Mode::Tune;
    Mode requested_mode = Mode::Live;
    DistancePetAvatar* live_avatar = nullptr;
    bool save_requested = false;
    bool save_idle_requested = false;
    std::uint32_t edit_revision = 0;
    std::uint32_t idle_edit_revision = 0;
    bool intro_visible = false;
    distance_pet::RestHeadMotion rest_head;
    distance_pet::TouchReaction touch_reaction;
    distance_pet::AlertHeadMotion alert_head;
    static constexpr std::uint32_t kTouchEnabled = 1u;
    static constexpr std::uint32_t kTouchPressed = 2u;
    std::atomic<std::uint32_t> head_touch_events{0};
    std::atomic<std::uint32_t> head_touch_at{0};
    int head_touch_connection = -1;
    bool previous_interactions_suspended = false;
    int touch_logged_phase = -1;
    std::uint32_t touch_logged_generation = 0;
    distance_pet::Mood touch_mood = distance_pet::Mood::Idle;
    bool touch_face_active = false;
    distance_pet::ContentTimingWindow content_timings;
    bool rest_head_active = false;
    std::uint32_t rest_head_intro_generation = 0;
    bool drawer_open = false;
    bool close_requested = false;
    bool stop_playback_requested = false;
    bool consume_touch = false;
    lv_indev_t* touch_input = nullptr;
    distance_ui::EdgeSwipe edge_swipe;
    int head_log_phase = -2;
    int head_log_beat = -2;
    int head_log_flags = -1;

    explicit View(AppDistance& app)
    {
        (void)app;
        auto& stackchan = GetStackChan();
        previous_interactions_suspended = stackchan.interactionsSuspended();
        stackchan.setInteractionsSuspended(true);
        // Cancel legacy pet/IMU restores while their old avatar is still alive.
        // Their callbacks keep running but only this app consumes interactions.
        stackchan.update();
        drawer_root = std::make_unique<Container>(lv_layer_sys());
        drawer_root->setPos(0, 0);
        drawer_root->setSize(320, 240);
        drawer_root->setPaddingAll(0);
        drawer_root->setBorderWidth(0);
        drawer_root->setRadius(0);
        drawer_root->setBgColor(lv_color_black());
        drawer_root->setBgOpa(160);
        drawer_root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        drawer_root->addFlag(LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(drawer_root->get(), [](lv_event_t* event) {
            auto* self = static_cast<View*>(lv_event_get_user_data(event));
            if (lv_event_get_target(event) == self->drawer_root->get()) self->close_drawer();
        }, LV_EVENT_CLICKED, this);
        drawer_panel = std::make_unique<Container>(*drawer_root);
        drawer_panel->setPos(6, 10);
        drawer_panel->setSize(308, 220);
        drawer_panel->setPaddingAll(0);
        drawer_panel->setRadius(12);
        drawer_panel->setBorderWidth(1);
        drawer_panel->setBgColor(lv_color_hex(0x14202B));
        drawer_panel->setBgOpa(LV_OPA_COVER);
        drawer_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        menu_title = make_label(*drawer_panel, 12, 8, 190, &MontserratSemiBold26);
        menu_title->setText("BOTIFIER");
        menu_codex = make_button(*drawer_panel, 208, 6, 92, "CODEX");
        menu_codex->setSize(92, 30);
        menu_codex->label().setTextFont(&lv_font_montserrat_14);
        menu_codex->onClick().connect([this]() { request_mode(Mode::Codex); });
        live_distance = make_label(*drawer_panel, 12, 42, 120, &lv_font_montserrat_16);
        live_state = make_label(*drawer_panel, 142, 42, 64, &lv_font_montserrat_14);
        menu_battery = make_label(*drawer_panel, 208, 42, 88, &lv_font_montserrat_14);
        menu_battery->setText("BAT --");
        menu_battery->setTextAlign(LV_TEXT_ALIGN_RIGHT);
        live_nfc_status = make_label(*drawer_panel, 12, 64, 280, &lv_font_montserrat_14, kMuted);
        live_nfc_status->setText("NFC STARTING");
        live_eye_status = make_label(*drawer_panel, 12, 84, 280, &lv_font_montserrat_14, kMuted);
        live_eye_status->setText("EYE OFF");
        menu_home = make_button(*drawer_panel, 8, 112, 92, "HOME");
        menu_live = make_button(*drawer_panel, 108, 112, 92, "LIVE");
        menu_close = make_button(*drawer_panel, 208, 112, 92, "CLOSE");
        menu_distance = make_button(*drawer_panel, 8, 164, 92, "DISTANCE");
        menu_nfc = make_button(*drawer_panel, 108, 164, 92, "NFC");
        menu_programs = make_button(*drawer_panel, 208, 164, 92, "PROGRAMS");
        for (auto* button : {menu_home.get(), menu_live.get(), menu_close.get(), menu_distance.get(),
                             menu_nfc.get(), menu_programs.get()}) button->label().setTextFont(&lv_font_montserrat_14);
        menu_home->onClick().connect([this]() { close_requested = true; });
        menu_live->onClick().connect([this]() { request_mode(Mode::Live); });
        menu_distance->onClick().connect([this]() { request_mode(Mode::Tune); });
        menu_nfc->onClick().connect([this]() { request_mode(Mode::Nfc); });
        menu_programs->onClick().connect([this]() { request_mode(Mode::Idle); });
        menu_close->onClick().connect([this]() { close_drawer(); });
        close_drawer();

        tune_page = make_page();
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

        codex_page = make_page();
        codex_title = make_label(*codex_page, 8, 8, 304, &MontserratSemiBold26);
        codex_title->setText("CODEX");
        codex_status = make_label(*codex_page, 8, 40, 304, &lv_font_montserrat_14);
        codex_timing = make_label(*codex_page, 8, 58, 304, &lv_font_montserrat_14, kMuted);
        codex_code = make_label(*codex_page, 8, 75, 304, &MontserratSemiBold26, kSafe);
        codex_code->setTextAlign(LV_TEXT_ALIGN_CENTER);
        codex_url = make_label(*codex_page, 8, 110, 304, &lv_font_montserrat_14, kMuted);
        codex_ip = make_label(*codex_page, 8, 128, 304, &lv_font_montserrat_14, kMuted);
        codex_key = make_label(*codex_page, 8, 148, 304, &lv_font_montserrat_14, kMuted);
        codex_sign_in = make_button(*codex_page, 8, 196, 96, "SIGN IN");
        codex_refresh = make_button(*codex_page, 112, 196, 96, "REFRESH");
        codex_forget = make_button(*codex_page, 216, 196, 96, "FORGET");
        for (auto* button : {codex_sign_in.get(), codex_refresh.get(), codex_forget.get()}) {
            button->setSize(96, 36);
            button->label().setTextFont(&lv_font_montserrat_14);
        }
        // These APIs only enqueue worker actions; no TLS or flash work runs here.
        codex_sign_in->onClick().connect([]() { codex_weekly::begin_login(); });
        codex_refresh->onClick().connect([]() { codex_weekly::request_refresh(); });
        codex_forget->onClick().connect([]() { codex_weekly::logout(); });
        codex_page->addFlag(LV_OBJ_FLAG_HIDDEN);
        intro_page = make_page();
        intro_page->setBgColor(lv_color_hex(0x000000));
        intro_image = lv_image_create(intro_page->get());
        lv_obj_center(intro_image);
        lv_obj_remove_flag(intro_image, LV_OBJ_FLAG_CLICKABLE);
        intro_status = make_label(*intro_page, 8, 100, 304, &lv_font_montserrat_16);
        intro_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
        intro_status->setText("KURO GAMES");
        intro_page->addFlag(LV_OBJ_FLAG_HIDDEN);
        apply_mode();
        head_touch_connection = GetHAL().onHeadPetGesture.connect([this](HeadPetGesture gesture) {
            // HAL emits Press once per contact. A following Swipe is part of
            // the same touch; it must not restart the reaction.
            if (gesture != HeadPetGesture::Press) return;
            auto events = head_touch_events.load();
            if (!(events & kTouchEnabled)) return;
            const auto epoch = events & ~3u;
            head_touch_at.store(GetHAL().millis());
            while ((events & kTouchEnabled) && (events & ~3u) == epoch) {
                if (head_touch_events.compare_exchange_weak(events, events | kTouchPressed)) return;
            }
        });
        touch_input = GetHAL().lvTouchpad;
        if (touch_input) lv_indev_add_event_cb(touch_input, touch_event, LV_EVENT_ALL, this);
    }

    ~View()
    {
        consume_head_touch(false);
        if (head_touch_connection >= 0) GetHAL().onHeadPetGesture.disconnect(head_touch_connection);
        if (touch_input) lv_indev_remove_event_cb_with_user_data(touch_input, touch_event, this);
        lv_anim_delete(drawer_panel->get(), nullptr);
        // The LVGL object borrows its source descriptor. Delete it before
        // releasing the final shared image reference.
        lv_obj_delete(intro_image);
        intro_image = nullptr;
        intro_frame.reset();
        stop_live();
        GetStackChan().setInteractionsSuspended(previous_interactions_suspended);
    }

    static void touch_event(lv_event_t* event)
    {
        auto* self = static_cast<View*>(lv_event_get_user_data(event));
        const auto code = lv_event_get_code(event);
        if (code == LV_EVENT_PRESSED) {
            self->consume_touch = false;
            lv_point_t point{};
            lv_indev_get_point(self->touch_input, &point);
            self->edge_swipe.press(point.x, point.y, GetHAL().millis());
        } else if (code == LV_EVENT_RELEASED) {
            lv_point_t point{};
            lv_indev_get_point(self->touch_input, &point);
            const auto edge = self->edge_swipe.release(point.x, point.y, GetHAL().millis());
            if (edge != distance_ui::SwipeEdge::None) {
                self->consume_touch = true;
                // The consumed release never reaches the original object.
                // Cancel its press without emitting a click or toggling it.
                if (auto* pressed = lv_indev_get_active_obj()) {
                    lv_obj_send_event(pressed, LV_EVENT_PRESS_LOST, self->touch_input);
                }
                self->open_drawer(edge);
            }
            if (self->consume_touch) lv_indev_stop_processing(self->touch_input);
        } else if ((code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) && self->consume_touch) {
            // RELEASED and the subsequent click are separate indev events.
            lv_indev_stop_processing(self->touch_input);
        } else if (code == LV_EVENT_INDEV_RESET) {
            self->edge_swipe.reset();
        }
    }

    void open_drawer(distance_ui::SwipeEdge edge)
    {
        drawer_open = true;
        drawer_root->removeFlag(LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(drawer_root->get());
        lv_anim_delete(drawer_panel->get(), nullptr);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, drawer_panel->get());
        lv_anim_set_exec_cb(&animation, [](void* object, std::int32_t y) {
            lv_obj_set_y(static_cast<lv_obj_t*>(object), y);
        });
        lv_anim_set_values(&animation, edge == distance_ui::SwipeEdge::Top ? -220 : 240, 10);
        lv_anim_set_duration(&animation, 160);
        lv_anim_start(&animation);
    }

    void close_drawer()
    {
        drawer_open = false;
        lv_anim_delete(drawer_panel->get(), nullptr);
        drawer_root->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void request_mode(Mode next)
    {
        requested_mode = next;
        stop_playback_requested = next != Mode::Live;
        close_drawer();
    }

    void stop_live()
    {
        auto& stackchan = GetStackChan();
        consume_head_touch(false);
        alert_head.cancel(stackchan.motion());
        touch_reaction.cancel(stackchan.motion());
        touch_face_active = false;
        rest_head.cancel(stackchan.motion());
        rest_head_active = false;
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
        if (mode == Mode::Codex) codex_page->removeFlag(LV_OBJ_FLAG_HIDDEN);
        else codex_page->addFlag(LV_OBJ_FLAG_HIDDEN);
        if (mode == Mode::Nfc) nfc_hint->removeFlag(LV_OBJ_FLAG_HIDDEN);
        else nfc_hint->addFlag(LV_OBJ_FLAG_HIDDEN);
        if (mode == Mode::Codex) {
            tune_page->addFlag(LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(codex_page->get());
        } else if (mode == Mode::Live) {
            auto avatar = std::make_unique<DistancePetAvatar>();
            avatar->init(lv_screen_active());
            live_avatar = avatar.get();
            GetStackChan().attachAvatar(std::move(avatar));
            tune_page->addFlag(LV_OBJ_FLAG_HIDDEN);
            raise_controls();
        } else {
            tune_page->removeFlag(LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(tune_page->get());
            const bool nfc = mode == Mode::Nfc;
            const bool idle = mode == Mode::Idle;
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

    void update_pet(std::uint32_t now, const distance_sensor::Snapshot& snapshot, bool distance_alert)
    {
        if (!live_avatar) return;
        auto signal = distance_pet::Signal::Unavailable;
        if (!snapshot.stale && snapshot.valid && snapshot.filtered_mm >= 0) {
            signal = snapshot.proximity_state == distance_sensor::ProximityState::Alert ?
                     distance_pet::Signal::Alert : distance_pet::Signal::Ready;
        }
        if (distance_alert) signal = distance_pet::Signal::Alert;
        live_avatar->setInteractionForeground(touch_reaction.active());
        if (touch_reaction.active()) {
            // Interaction stays visible over distance reminders; the sensor
            // keeps sampling and its current warning returns on completion.
            if (!touch_face_active || touch_mood != touch_reaction.mood()) {
                touch_mood = touch_reaction.mood();
                live_avatar->setMood(touch_mood, now);
            }
            touch_face_active = true;
            live_avatar->tick(now, distance_pet::Signal::Ready, false, touch_reaction.eye_cue(now), true);
        } else {
            if (touch_face_active) live_avatar->setMood(distance_pet::Mood::Idle, now);
            touch_face_active = false;
            live_avatar->tick(now, signal);
        }
    }

    std::uint32_t consume_head_touch(bool enabled)
    {
        auto events = head_touch_events.load();
        for (;;) {
            const auto epoch = events & ~3u;
            // Advance on disable so even a callback paused across a complete
            // disable/resume cycle cannot publish an old touch into this one.
            const auto next = enabled ? epoch | kTouchEnabled :
                (events & kTouchEnabled) ? epoch + 4u : epoch;
            if (head_touch_events.compare_exchange_weak(events, next)) return events;
        }
    }

    void update_head_touch(std::uint32_t now, bool foreground_busy,
                           const idle_program::Snapshot& idle)
    {
        const bool eye_owned = idle.active && !idle.blocked && idle.program == idle_program::Program::EyeRest;
        const bool eligible = mode == Mode::Live && requested_mode == Mode::Live &&
                              !drawer_open && !foreground_busy && !eye_owned;
        // Disable producers and drop pending input in one atomic operation;
        // an in-flight callback cannot enqueue behind a higher-priority owner.
        const auto events = consume_head_touch(eligible);
        const bool pressed = (events & kTouchPressed) != 0;
        const auto pressed_at = head_touch_at.load();
        // Input may arrive after the app's earlier sensor snapshot. Sample the
        // clock after the mailbox, so a fresh event cannot appear backdated.
        now = GetHAL().millis();
        auto& motion = GetStackChan().motion();
        const bool fresh_press = pressed && now - pressed_at <= 500u;
        const bool interrupted_alert = fresh_press && alert_head.owns_lock() && motion.isModifyLocked();
        if (!eligible || fresh_press || touch_reaction.active()) alert_head.cancel(motion);
        if (!eligible) {
            touch_reaction.cancel(motion);
        } else {
            if (fresh_press && touch_reaction.trigger(motion, now, interrupted_alert)) {
                // Clears a normal information card, but never claims the
                // protected NFC/EyeRest foreground-animation slot.
                idle_program::note_activity();
            }
            touch_reaction.tick(motion, now);
        }
        const auto phase = static_cast<int>(touch_reaction.phase());
        if (phase != touch_logged_phase || touch_reaction.generation() != touch_logged_generation) {
            touch_logged_phase = phase;
            touch_logged_generation = touch_reaction.generation();
            std::printf("SC_PET {\"v\":1,\"uptime_ms\":%" PRIu32 ",\"generation\":%" PRIu32
                        ",\"phase\":%d,\"active\":%s,\"owns_lock\":%s}\n",
                        now, touch_logged_generation, phase, touch_reaction.active() ? "true" : "false",
                        touch_reaction.owns_lock() ? "true" : "false");
        }
    }

    void update_alert_head(std::uint32_t now, bool foreground_busy,
                           const idle_program::Snapshot& idle, bool distance_alert)
    {
        const bool eye_owned = idle.active && !idle.blocked && idle.program == idle_program::Program::EyeRest;
        const bool enabled = mode == Mode::Live && requested_mode == Mode::Live &&
                             !drawer_open && !foreground_busy && !eye_owned &&
                             !touch_reaction.active() && distance_alert;
        alert_head.tick(GetStackChan().motion(), now, enabled);
    }

    void show_intro(const intro_player::Snapshot& playback)
    {
        intro_visible = playback.busy;
        intro_status->setText(playback.clip == intro_player::ClipId::Shorekeeper ? "EYE BREAK" : "KURO GAMES");
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

    void update_rest_head(const idle_program::Snapshot& idle)
    {
        const bool eligible = mode == Mode::Live && idle.active && !idle.blocked &&
                              idle.program == idle_program::Program::EyeRest;
        if (!eligible) {
            if (rest_head_active) {
                rest_head.cancel(GetStackChan().motion());
                log_head(idle, true);
            }
            rest_head_active = false;
            return;
        }
        if (rest_head_active && idle.eye_phase == idle_program::EyePhase::Intro &&
            rest_head_intro_generation != idle.generation) {
            // OFF/ON can replace a session between app frames. A new intro
            // must not inherit the old controller's beat, lock or centre.
            rest_head.cancel(GetStackChan().motion());
            rest_head_active = false;
        }
        if (!rest_head_active) {
            rest_head = distance_pet::RestHeadMotion{};
            rest_head_active = true;
            rest_head_intro_generation = idle.generation;
            head_log_phase = head_log_beat = -2;
            head_log_flags = -1;
        }
        if (idle.eye_phase == idle_program::EyePhase::Intro) {
            // Reserve against ordinary idle motion while the video plays; any
            // already-moving idle target can finish before the rhythm starts.
            rest_head.reserve(GetStackChan().motion());
            log_head(idle);
            return;
        }
        rest_head.tick(GetStackChan().motion(), idle.eye_phase == idle_program::EyePhase::Motion ?
                       idle.phase_elapsed_ms : idle_program::kMotionDurationMs);
        log_head(idle);
    }

    void log_head(const idle_program::Snapshot& idle, bool exit = false)
    {
        const auto phase = exit ? -1 : static_cast<int>(idle.eye_phase);
        const auto beat = rest_head.last_beat();
        const int flags = (rest_head.started() ? 1 : 0) | (rest_head.skipped() ? 2 : 0) |
                          (rest_head.owns_lock() ? 4 : 0) | (rest_head.ownership_lost() ? 8 : 0) |
                          (rest_head.limits_unavailable() ? 16 : 0);
        if (phase == head_log_phase && beat == head_log_beat && flags == head_log_flags) return;
        head_log_phase = phase;
        head_log_beat = beat;
        head_log_flags = flags;
        // These snapshots perform no bus reads; their raw fields and counters
        // identify the most recent actual driver transactions, including -1.
        auto& motion = GetStackChan().motion();
        const auto yaw = motion.yawServo().diagnostics();
        const auto pitch = motion.pitchServo().diagnostics();
        std::printf("SC_HEAD {\"v\":1,\"uptime_ms\":%" PRIu32 ",\"generation\":%" PRIu32
                    ",\"phase\":\"%s\",\"elapsed_ms\":%" PRIu32 ",\"beat\":%d,\"flags\":%d,"
                    "\"target_yaw\":%d,\"target_pitch\":%d,\"cached_feedback\":true,"
                    "\"yaw\":{\"supported\":%s,\"position\":%d,\"moving\":%d,\"torque_before\":%d,\"torque\":%d,"
                    "\"position_reads\":%" PRIu32 ",\"position_writes\":%" PRIu32 ",\"write_ack\":%d,"
                    "\"write_error\":%d,\"write_state\":%d,\"write_duration_ms\":%u,\"torque_ack\":%d,"
                    "\"torque_write_error\":%d,\"torque_write_state\":%d,\"torque_read_error\":%d},"
                    "\"pitch\":{\"supported\":%s,\"position\":%d,\"moving\":%d,\"torque_before\":%d,\"torque\":%d,"
                    "\"position_reads\":%" PRIu32 ",\"position_writes\":%" PRIu32 ",\"write_ack\":%d,"
                    "\"write_error\":%d,\"write_state\":%d,\"write_duration_ms\":%u,\"torque_ack\":%d,"
                    "\"torque_write_error\":%d,\"torque_write_state\":%d,\"torque_read_error\":%d}}\n",
                    static_cast<std::uint32_t>(GetHAL().millis()), idle.generation,
                    exit ? "exit" : idle_program::eye_phase_text(idle.eye_phase), idle.phase_elapsed_ms, beat, flags,
                    rest_head.target_yaw(), rest_head.target_pitch(),
                    yaw.supported ? "true" : "false", yaw.raw_position, yaw.raw_moving, yaw.torque_before, yaw.raw_torque,
                    yaw.position_reads, yaw.position_writes, yaw.write_ack, yaw.write_error, yaw.write_state,
                    static_cast<unsigned>(yaw.last_write_duration_ms),
                    yaw.torque_ack, yaw.torque_write_error, yaw.torque_write_state, yaw.torque_read_error,
                    pitch.supported ? "true" : "false", pitch.raw_position, pitch.raw_moving, pitch.torque_before, pitch.raw_torque,
                    pitch.position_reads, pitch.position_writes, pitch.write_ack, pitch.write_error, pitch.write_state,
                    static_cast<unsigned>(pitch.last_write_duration_ms),
                    pitch.torque_ack, pitch.torque_write_error, pitch.torque_write_state, pitch.torque_read_error);
    }

    void raise_controls()
    {
        if (intro_visible) {
            lv_obj_move_foreground(intro_page->get());
        }
        // The hidden sys-layer drawer is independent of the full-screen media
        // and top-layer programmes; opening it never changes playback context.
        if (drawer_open) lv_obj_move_foreground(drawer_root->get());
    }

    void set_save_status(const char* text, std::uint32_t color)
    {
        save_status->setText(text);
        save_status->setTextColor(lv_color_hex(color));
    }

    void show_reader_status(const distance_sensor::Snapshot&,
                            const nfc_sense::Snapshot& scanned)
    {
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

    void show_eye_status(const idle_program::Snapshot& idle, const distance_sensor::Snapshot&,
                         bool)
    {
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

    void show_codex(const codex_weekly::Snapshot& snapshot)
    {
        if (mode != Mode::Codex) return;
        using State = codex_weekly::State;
        char text[96];
        const bool login = snapshot.state == State::StartingLogin || snapshot.state == State::AwaitingLogin;
        const bool ready = snapshot.state == State::Ready;
        const bool error = snapshot.state == State::Error;
        codex_status->setText(error ? codex_error_label(snapshot.error) : codex_state_label(snapshot.state));
        codex_status->setTextColor(lv_color_hex(error || snapshot.state == State::LoginExpired ? kAlert : kForeground));
        const auto milliseconds = login ? snapshot.login_expires_in_ms : snapshot.next_poll_in_ms;
        const auto seconds = milliseconds / 1000 + (milliseconds % 1000 != 0);
        if (snapshot.state == State::AwaitingLogin) {
            std::snprintf(text, sizeof(text), "CODE EXPIRES %" PRIu32 ":%02" PRIu32, seconds / 60, seconds % 60);
        } else if (snapshot.state == State::StartingLogin) {
            std::snprintf(text, sizeof(text), "GETTING A LOGIN CODE...");
        } else if (ready) {
            std::snprintf(text, sizeof(text), "NEXT CHECK %" PRIu32 "m %02" PRIu32 "s", seconds / 60, seconds % 60);
        } else {
            std::snprintf(text, sizeof(text), "%s", snapshot.signed_in ? "ACCOUNT SAVED ON THIS ROBOT" : "SIGN IN ON THIS ROBOT");
        }
        codex_timing->setText(text);
        codex_code->setTextFont(&MontserratSemiBold26);
        if (login) {
            char code[33]{};
            std::snprintf(code, sizeof(code), "%.32s", snapshot.user_code.data());
            lv_point_t size{};
            lv_text_get_size(&size, code, &MontserratSemiBold26, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
            if (size.x > 304) {
                codex_code->setTextFont(&lv_font_montserrat_14);
                if (std::strlen(code) > 16) std::snprintf(text, sizeof(text), "%.16s\n%.16s", code, code + 16);
                else std::snprintf(text, sizeof(text), "%s", code);
            } else {
                std::snprintf(text, sizeof(text), "%s", code[0] ? code : "--");
            }
        } else if (ready && snapshot.remaining_percent >= 0 && snapshot.remaining_percent <= 100) {
            std::snprintf(text, sizeof(text), "%d%% LEFT", snapshot.remaining_percent);
        } else {
            std::snprintf(text, sizeof(text), "--");
        }
        codex_code->setText(text);
        codex_code->setTextColor(lv_color_hex(ready && snapshot.remaining_percent <= 20 ? kNear : kSafe));
        codex_url->setText(ready ? "AUTO REFRESH OVER WI-FI" : "auth.openai.com/codex/device");
        std::snprintf(text, sizeof(text), "IP %.15s:8765", snapshot.ip_address[0] ? snapshot.ip_address.data() : "--");
        codex_ip->setText(text);
        if (snapshot.debug_key[0]) {
            std::snprintf(text, sizeof(text), "KEY %.16s\n    %.16s", snapshot.debug_key.data(), snapshot.debug_key.data() + 16);
        } else {
            std::snprintf(text, sizeof(text), "KEY --");
        }
        codex_key->setText(text);
        set_enabled(*codex_sign_in, !snapshot.signed_in && !snapshot.login_pending && !login);
    }

    void update(const distance_sensor::Snapshot& snapshot, const distance_sensor::ProximityConfig& requested,
                const nfc_sense::Snapshot& scanned)
    {
        char text[96];
        char centimeters[24];
        char raw_centimeters[24];
        int battery = -1;
        bool charging = false, discharging = false;
        if (GetHAL().getBatteryStatus(battery, charging, discharging)) {
            std::snprintf(text, sizeof(text), charging ? "%d%% CHG" : "BAT %d%%", battery);
            menu_battery->setText(text);
            menu_battery->setTextColor(lv_color_hex(charging ? kSafe : battery <= 15 ? kAlert : kForeground));
        } else {
            menu_battery->setText("BAT --");
            menu_battery->setTextColor(lv_color_hex(kMuted));
        }
        const bool usable = snapshot.valid && !snapshot.stale;
        const char* quality = snapshot.stale ? "STALE" : snapshot.valid ? "VALID" : "INVALID";
        const auto color = state_color(snapshot.proximity_state);
        format_cm(centimeters, sizeof(centimeters), usable ? snapshot.filtered_mm : -1);
        std::snprintf(text, sizeof(text), "%s cm", centimeters);
        live_distance->setText(text);
        live_state->setText(usable ? state_text(snapshot.proximity_state) : quality);
        live_state->setTextColor(lv_color_hex(usable ? color : kNear));
        if (mode == Mode::Codex) return;
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
    setAppInfo().name = "BOTIFIER";
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
    _eye_intro_started = false;
    nfc_sense::start(); // An unavailable reader must not prevent ranging or UI.
    const auto playback = intro_player::snapshot();
    const auto current = distance_sensor::snapshot();
    const auto scanned = nfc_sense::snapshot();
    const auto codex = codex_weekly::snapshot();
    _last_nfc_event = scanned.accepted;
    _intro_completed_count = playback.completed_count;
    LvglLockGuard lock;
    _view.reset();
    _view = std::make_unique<View>(*this);
    _view->update(current, distance_sensor::config(), scanned);
    _view->show_reader_status(current, scanned);
    _view->show_eye_status(idle_program::snapshot(), current, playback.busy);
    _view->show_codex(codex);
    _last_refresh_ms = GetHAL().millis();
}

void AppDistance::onRunning()
{
    distance_pet::ContentFrameTiming frame_timing;
    auto lock_requested_ms = GetHAL().millis();
    bool close_requested = false;
    bool stop_playback_requested = false;
    {
        LvglLockGuard lock;
        frame_timing.lock_wait_ms += GetHAL().millis() - lock_requested_ms;
        close_requested = _view && _view->close_requested;
        stop_playback_requested = _view && _view->stop_playback_requested;
        if (_view) _view->stop_playback_requested = false;
    }
    if (close_requested) {
        // The input callback only requests this action. Worker cancellation and
        // app teardown run outside LVGL's event-dispatch lock.
        if (_view) emit_content_timing(_view->content_timings.finish());
        close();
        return;
    }
    auto playback = intro_player::snapshot();
    const auto current = distance_sensor::snapshot();
    auto scanned = nfc_sense::snapshot();
    const auto codex = codex_weekly::snapshot();
    auto now = GetHAL().millis();
    if (playback.completed_count != _intro_completed_count) {
        if (_eye_intro_started && !playback.busy && playback.clip == intro_player::ClipId::Shorekeeper &&
            playback.completed_count != _eye_intro_completion_baseline) {
            // The codec worker has actually finished; its opening/decode time
            // never consumes the later head sequence or REST timer.
            idle_program::finish_eye_intro(_eye_intro_generation);
            _eye_intro_started = false;
        }
        _intro_completed_count = playback.completed_count;
        _last_nfc_event = playback.sensing_baseline;
    }
    auto pending_eye = idle_program::snapshot();
    const bool eye_intro_owned = pending_eye.active && !pending_eye.blocked &&
        pending_eye.program == idle_program::Program::EyeRest &&
        pending_eye.eye_phase == idle_program::EyePhase::Intro;
    bool live_requested = false;
    lock_requested_ms = GetHAL().millis();
    {
        LvglLockGuard lock;
        frame_timing.lock_wait_ms += GetHAL().millis() - lock_requested_ms;
        live_requested = _view && _view->mode == View::Mode::Live &&
                         _view->requested_mode == View::Mode::Live;
    }
    if ((playback.busy && stop_playback_requested) || (_eye_intro_started &&
        (!eye_intro_owned || !live_requested || pending_eye.generation != _eye_intro_generation))) {
        // Explicit exit/OFF must not leave a self-owned clip blocking the app.
        intro_player::stop();
        playback = intro_player::snapshot();
        _intro_completed_count = playback.completed_count;
        _last_nfc_event = playback.sensing_baseline;
        scanned = nfc_sense::snapshot();
        now = GetHAL().millis();
        _eye_intro_started = false;
    }
    const bool new_event = scanned.accepted != _last_nfc_event;
    _last_nfc_event = scanned.accepted; // Busy events are consumed, never queued.
    // The programme service claims this slot atomically against its EyeRest
    // worker. A tap during that animation is consumed above, never queued.
    const bool nfc_activity = !playback.busy && new_event && now - scanned.last_accepted_ms <= 500 &&
                              idle_program::try_begin_foreground_animation();
    if (nfc_activity) {
        if (intro_player::start() == ESP_OK) playback = intro_player::snapshot();
    }
    if (!playback.busy && eye_intro_owned && live_requested && !_eye_intro_started) {
        _eye_intro_generation = pending_eye.generation;
        _eye_intro_completion_baseline = playback.completed_count;
        const auto error = intro_player::start(intro_player::ClipId::Shorekeeper);
        if (error == ESP_OK) {
            _eye_intro_started = true;
            playback = intro_player::snapshot();
        } else {
            ESP_LOGW("botifier", "eye intro unavailable error=%d", static_cast<int>(error));
            // Fail forward once; never spin/retry a missing clip indefinitely.
            idle_program::finish_eye_intro(_eye_intro_generation);
        }
    }
    const bool refresh = now - _last_refresh_ms >= kRefreshMs;
    if (refresh) {
        _last_refresh_ms = now;
    }

    bool save = false;
    bool save_idle = false;
    std::uint32_t saved_revision = 0;
    bool motion_content_updated = false;
    std::uint32_t content_generation = 0;
    lock_requested_ms = GetHAL().millis();
    {
        LvglLockGuard lock;
        frame_timing.start_ms = GetHAL().millis();
        frame_timing.lock_wait_ms += frame_timing.start_ms - lock_requested_ms;
        if (!_view) {
            return;
        }
        const auto previous_mode = _view->mode;
        _view->apply_mode();
        // Release a low-priority pet movement before the eye controller or
        // ordinary modifiers get their turn to acquire the motion lock.
        _view->update_head_touch(now, playback.busy || nfc_activity, idle_program::snapshot());
        auto stage_started_ms = GetHAL().millis();
        _view->update_rest_head(idle_program::snapshot());
        frame_timing.head_before_ms = GetHAL().millis() - stage_started_ms;
        stage_started_ms = GetHAL().millis();
        GetStackChan().update();
        frame_timing.stack_ms = GetHAL().millis() - stage_started_ms;
        // The sampler can enter Alert after the snapshot above. Match the
        // global warning before allowing a top-layer information card.
        const bool distance_alert = distance_sensor::alert_active() ||
            (current.valid && !current.stale && current.proximity_state == distance_sensor::ProximityState::Alert);
        _view->show_intro(playback);
        _view->raise_controls();
        idle_program::set_context({
            _view->mode == View::Mode::Live,
            (playback.busy && playback.clip != intro_player::ClipId::Shorekeeper) ||
                _view->mode != View::Mode::Live,
            distance_alert});
        const auto idle = idle_program::snapshot();
        // The programme worker may acquire the foreground during
        // StackChan.update. Recheck before issuing any later head/face command.
        _view->update_head_touch(now, playback.busy || nfc_activity, idle);
        stage_started_ms = GetHAL().millis();
        _view->update_rest_head(idle);
        frame_timing.head_after_ms = GetHAL().millis() - stage_started_ms;
        _view->update_alert_head(GetHAL().millis(), playback.busy || nfc_activity, idle, distance_alert);
        _view->update_pet(GetHAL().millis(), current, distance_alert);
        const bool rest_animation = idle.active && !idle.blocked && idle.program == idle_program::Program::EyeRest;
        if (nfc_activity || playback.busy || _view->mode != View::Mode::Live ||
            _view->touch_reaction.active() || (distance_alert && !rest_animation)) {
            idle_program::clear_locked();
        } else {
            // EyeRest and NFC are exclusive top-priority animations. TOO CLOSE
            // replaces ordinary cards, but waits for either animation to finish.
            stage_started_ms = GetHAL().millis();
            idle_program::render_locked(idle);
            frame_timing.render_ms = GetHAL().millis() - stage_started_ms;
            motion_content_updated = idle.active && !idle.blocked &&
                idle.program == idle_program::Program::EyeRest && idle.eye_phase == idle_program::EyePhase::Motion;
            content_generation = idle.generation;
        }
        if (refresh || previous_mode != _view->mode) {
            _view->update(current, distance_sensor::config(), scanned);
            _view->show_reader_status(current, scanned);
            _view->show_eye_status(idle_program::snapshot(), current, playback.busy);
            _view->show_codex(codex);
        }
        save = _view->save_requested;
        save_idle = _view->save_idle_requested;
        _view->save_requested = false;
        saved_revision = save_idle ? _view->idle_edit_revision : _view->edit_revision;
        frame_timing.end_ms = GetHAL().millis();
    }

    // Timing reports are emitted outside the display lock.
    emit_content_timing(_view->content_timings.add(motion_content_updated, content_generation, frame_timing));

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
    if (_view) emit_content_timing(_view->content_timings.finish());
    _eye_intro_started = false;
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
