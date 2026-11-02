#include "view.h"
#include "../../app_distance/pet_avatar.h"

#include <assets/assets.h>
#include <hal/hal.h>
#include <lvgl.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

namespace idle_program {
namespace {

constexpr std::uint32_t kText = 0xF7F2FF;
constexpr std::uint32_t kMuted = 0xB8B1C3;
constexpr std::uint32_t kMint = 0x74E4B5;
constexpr std::uint32_t kAmber = 0xFFD37A;
constexpr std::uint32_t kRose = 0xFF91AE;

lv_obj_t* plain(lv_obj_t* parent, int width, int height)
{
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return object;
}

lv_obj_t* label(lv_obj_t* parent, int x, int y, int width, const lv_font_t* font, std::uint32_t color)
{
    auto* object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

void show(lv_obj_t* object, bool visible)
{
    if (visible) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

distance_pet::Mood mood_for(const Snapshot& value)
{
    if (value.program == Program::RestDone) return distance_pet::Mood::Happy;
    if (value.program == Program::EyeRest) return distance_pet::Mood::Affection;
    if (value.weekly_status == WeeklyStatus::Unavailable || value.weekly_left < 0) return distance_pet::Mood::Sad;
    return value.weekly_left <= 20 ? distance_pet::Mood::Sad :
           value.weekly_left >= 60 ? distance_pet::Mood::Happy : distance_pet::Mood::Curious;
}

void format_time(char* text, std::size_t size, std::uint32_t timestamp_seconds, const char* prefix)
{
    if (!timestamp_seconds) {
        std::snprintf(text, size, "%s --", prefix);
        return;
    }
    const auto timestamp = static_cast<std::time_t>(timestamp_seconds) + 8 * 60 * 60;
    std::tm reset{};
#ifdef _WIN32
    const bool valid = gmtime_s(&reset, &timestamp) == 0;
#else
    const bool valid = gmtime_r(&timestamp, &reset) != nullptr;
#endif
    if (valid) {
        std::snprintf(text, size, "%s %02d-%02d %02d:%02d UTC+8", prefix, reset.tm_mon + 1, reset.tm_mday,
                      reset.tm_hour, reset.tm_min);
    } else {
        std::snprintf(text, size, "%s --", prefix);
    }
}

struct View {
    lv_obj_t* root = nullptr;
    lv_obj_t* face_host = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* number = nullptr;
    lv_obj_t* unit = nullptr;
    lv_obj_t* detail = nullptr;
    lv_obj_t* progress = nullptr;
    std::unique_ptr<DistancePetAvatar> pet;
    Program previous_program = Program::Pet;
    WeeklyStatus previous_status = WeeklyStatus::Waiting;
    distance_pet::Mood previous_mood = distance_pet::Mood::Idle;
    std::uint32_t previous_generation = 0;
    std::uint32_t last_render_ms = 0;
    bool rendered = false;

    View()
    {
        root = plain(lv_layer_top(), 320, 184);
        lv_obj_set_pos(root, 0, 0);
        lv_obj_set_style_bg_color(root, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        face_host = plain(root, 320, 240);
        lv_obj_set_pos(face_host, 180, 39);
        lv_obj_set_style_transform_pivot_x(face_host, 0, 0);
        lv_obj_set_style_transform_pivot_y(face_host, 0, 0);
        lv_obj_set_style_transform_scale_x(face_host, 104, 0);
        lv_obj_set_style_transform_scale_y(face_host, 104, 0);
        pet = std::make_unique<DistancePetAvatar>();
        pet->init(face_host);
        title = label(root, 16, 8, 288, &BUILTIN_TEXT_FONT, kText);
        number = label(root, 16, 49, 82, &MontserratSemiBold26, kText);
        lv_obj_set_style_transform_pivot_x(number, 0, 0);
        lv_obj_set_style_transform_pivot_y(number, 0, 0);
        lv_obj_set_style_transform_scale_x(number, 512, 0);
        lv_obj_set_style_transform_scale_y(number, 512, 0);
        unit = label(root, 16, 111, 288, &lv_font_montserrat_14, kMuted);
        detail = label(root, 16, 153, 288, &BUILTIN_TEXT_FONT, kMuted);
        progress = lv_bar_create(root);
        lv_obj_remove_flag(progress, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_size(progress, 288, 6);
        lv_obj_set_pos(progress, 16, 140);
        lv_obj_set_style_bg_color(progress, lv_color_hex(0x242330), LV_PART_MAIN);
        lv_obj_set_style_bg_color(progress, lv_color_hex(kMint), LV_PART_INDICATOR);
        lv_obj_set_style_radius(progress, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(progress, 3, LV_PART_INDICATOR);
        lv_bar_set_range(progress, 0, 1000);
    }

    ~View()
    {
        pet.reset();
        lv_obj_delete(root);
    }

    void render(const Snapshot& value)
    {
        const auto now = GetHAL().millis();
        if (rendered && value.program == previous_program && value.generation == previous_generation &&
            now - last_render_ms < 33) return;
        rendered = true;
        last_render_ms = now;
        const auto mood = mood_for(value);
        if (value.program != previous_program || value.weekly_status != previous_status ||
            value.generation != previous_generation || mood != previous_mood) {
            pet->setMood(mood, now);
            previous_program = value.program;
            previous_status = value.weekly_status;
            previous_generation = value.generation;
            previous_mood = mood;
        }
        pet->tick(now, distance_pet::Signal::Ready);
        show(number, value.program != Program::RestDone);
        show(unit, value.program != Program::RestDone);
        char text[64];
        if (value.program == Program::EyeRest) {
            lv_label_set_text(title, "EYE REST");
            lv_obj_set_style_text_font(title, &BUILTIN_TEXT_FONT, 0);
            std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value.remaining_ms / 1000 +
                          (value.remaining_ms % 1000 != 0)));
            lv_label_set_text(number, text);
            lv_obj_set_style_text_color(number, lv_color_hex(kMint), 0);
            lv_label_set_text(unit, "SECONDS LEFT");
            lv_obj_set_pos(unit, 16, 111);
            lv_obj_set_style_text_color(unit, lv_color_hex(kMuted), 0);
            lv_obj_set_style_text_font(detail, &BUILTIN_TEXT_FONT, 0);
            lv_label_set_text(detail, "Look about 6m away");
            lv_obj_set_pos(detail, 16, 153);
            lv_obj_set_pos(progress, 16, 140);
            const auto duration = std::max<std::uint32_t>(1, value.config.rest_duration_ms);
            lv_bar_set_value(progress, static_cast<int>(std::min<std::uint64_t>(1000,
                             static_cast<std::uint64_t>(value.remaining_ms) * 1000 / duration)), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(progress, lv_color_hex(kMint), LV_PART_INDICATOR);
        } else if (value.program == Program::Weekly) {
            lv_label_set_text(title, "CODEX / WEEKLY LEFT");
            lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
            const bool available = value.weekly_left >= 0 && value.weekly_left <= 100 &&
                                   value.weekly_status == WeeklyStatus::Fresh;
            const auto color = value.weekly_status == WeeklyStatus::Stale ? kAmber : !available ? kMuted :
                               value.weekly_left <= 20 ? kRose : kMint;
            if (available) std::snprintf(text, sizeof(text), "%d%%", value.weekly_left);
            else std::snprintf(text, sizeof(text), "--");
            lv_label_set_text(number, text);
            lv_obj_set_style_text_color(number, lv_color_hex(color), 0);
            if (value.weekly_status == WeeklyStatus::Waiting) {
                std::snprintf(text, sizeof(text), "WAITING FOR CODEX");
            } else if (value.weekly_status == WeeklyStatus::Stale) {
                format_time(text, sizeof(text), value.weekly_observed_at, "STALE");
            } else if (!available) {
                std::snprintf(text, sizeof(text), "UNAVAILABLE");
            } else {
                format_time(text, sizeof(text), value.weekly_observed_at,
                            value.weekly_status == WeeklyStatus::Fresh ? "FRESH" : "STALE");
            }
            lv_label_set_text(unit, text);
            lv_obj_set_pos(unit, 16, 128);
            lv_obj_set_style_text_color(unit, lv_color_hex(color), 0);
            format_time(text, sizeof(text), value.weekly_reset_at, "RESET");
            lv_label_set_text(detail, text);
            lv_obj_set_style_text_font(detail, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(detail, 16, 153);
            lv_obj_set_pos(progress, 16, 177);
            lv_bar_set_value(progress, available ? value.weekly_left * 10 : 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(progress, lv_color_hex(color), LV_PART_INDICATOR);
        } else {
            lv_label_set_text(title, "REST COMPLETE");
            lv_obj_set_style_text_font(title, &BUILTIN_TEXT_FONT, 0);
            lv_obj_set_style_text_font(detail, &BUILTIN_TEXT_FONT, 0);
            lv_label_set_text(detail, "Nice! Back to your day");
            lv_obj_set_pos(detail, 16, 153);
            lv_obj_set_pos(progress, 16, 140);
            lv_bar_set_value(progress, 1000, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(progress, lv_color_hex(kMint), LV_PART_INDICATOR);
        }
    }
};

std::unique_ptr<View> view;

} // namespace

void clear_locked()
{
    view.reset();
}

void render_locked(const Snapshot& value)
{
    if (!value.active || value.blocked || !value.config.enabled || value.program == Program::Pet) {
        clear_locked();
        return;
    }
    if (!view) view = std::make_unique<View>();
    if (lv_obj_get_child(lv_layer_top(), -1) != view->root) lv_obj_move_foreground(view->root);
    view->render(value);
}

} // namespace idle_program
