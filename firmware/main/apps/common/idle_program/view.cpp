#include "view.h"
#include "eye_sequence.h"
#include "../../app_distance/pet_avatar.h"

#include <assets/assets.h>
#include <hal/hal.h>
#include <lvgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <memory>

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

namespace idle_program {
namespace {

constexpr std::uint32_t kText = 0xF7F2FF;
constexpr std::uint32_t kMuted = 0xADC4D2;
constexpr std::uint32_t kMint = 0x74E4B5;
constexpr std::uint32_t kAmber = 0xFFD37A;
constexpr std::uint32_t kRose = 0xFF91AE;
constexpr auto kNoBeat = std::numeric_limits<std::uint32_t>::max();
constexpr std::array<distance_pet::Mood, 4> kGroupMoods{
    distance_pet::Mood::Curious, distance_pet::Mood::Happy,
    distance_pet::Mood::Surprise, distance_pet::Mood::Affection};

lv_obj_t* plain(lv_obj_t* parent, int width, int height)
{
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return object;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, std::uint32_t color)
{
    auto* object = lv_label_create(parent);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

void place(lv_obj_t* object, int x, int y, int width, int scale = 256)
{
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_obj_set_style_transform_scale_x(object, scale, 0);
    lv_obj_set_style_transform_scale_y(object, scale, 0);
}

void show(lv_obj_t* object, bool visible)
{
    if (visible) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void format_time(char* text, std::size_t size, std::uint32_t timestamp_seconds, const char* prefix)
{
    if (!timestamp_seconds) {
        std::snprintf(text, size, "%s --", prefix);
        return;
    }
    const auto timestamp = static_cast<std::time_t>(timestamp_seconds) + 8 * 60 * 60;
    std::tm result{};
#ifdef _WIN32
    const bool valid = gmtime_s(&result, &timestamp) == 0;
#else
    const bool valid = gmtime_r(&timestamp, &result) != nullptr;
#endif
    if (valid) {
        std::snprintf(text, size, "%s %02d-%02d %02d:%02d UTC+8", prefix, result.tm_mon + 1, result.tm_mday,
                      result.tm_hour, result.tm_min);
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
    lv_obj_t* clock_ring = nullptr;
    lv_obj_t* clock_hands = nullptr;
    lv_obj_t* codex_mark = nullptr;
    std::array<lv_obj_t*, 10> quota_cells{};
    std::array<lv_obj_t*, 10> quota_fill{};
    std::array<lv_obj_t*, kMotionBeats.size()> beat_dots{};
    std::unique_ptr<DistancePetAvatar> pet;
    Program previous_program = Program::Pet;
    EyePhase previous_phase = EyePhase::Intro;
    WeeklyStatus previous_status = WeeklyStatus::Waiting;
    distance_pet::Mood previous_mood = distance_pet::Mood::Idle;
    std::uint32_t previous_generation = 0;
    std::uint32_t previous_beat = kNoBeat;
    std::uint32_t last_render_ms = 0;
    bool rendered = false;

    View()
    {
        root = plain(lv_layer_top(), 320, 240);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        face_host = plain(root, 320, 240);
        lv_obj_set_style_transform_pivot_x(face_host, 0, 0);
        lv_obj_set_style_transform_pivot_y(face_host, 0, 0);
        pet = std::make_unique<DistancePetAvatar>();
        pet->init(face_host);

        clock_ring = lv_arc_create(root);
        lv_obj_remove_flag(clock_ring, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_remove_style(clock_ring, nullptr, LV_PART_KNOB);
        lv_obj_set_size(clock_ring, 58, 58);
        lv_obj_set_pos(clock_ring, 20, 83);
        lv_arc_set_bg_angles(clock_ring, 0, 360);
        lv_arc_set_rotation(clock_ring, 270);
        lv_arc_set_range(clock_ring, 0, 1000);
        lv_obj_set_style_arc_width(clock_ring, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(clock_ring, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(clock_ring, lv_color_hex(0x5D3B16), LV_PART_MAIN);
        lv_obj_set_style_arc_color(clock_ring, lv_color_hex(kAmber), LV_PART_INDICATOR);
        clock_hands = lv_line_create(root);
        lv_obj_remove_style_all(clock_hands);
        lv_obj_remove_flag(clock_hands, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_pos(clock_hands, 20, 83);
        lv_obj_set_size(clock_hands, 58, 58);
        static constexpr std::array<lv_point_precise_t, 3> hands{{{29, 12}, {29, 29}, {41, 36}}};
        lv_line_set_points(clock_hands, hands.data(), hands.size());
        lv_obj_set_style_line_color(clock_hands, lv_color_hex(kAmber), 0);
        lv_obj_set_style_line_width(clock_hands, 2, 0);
        lv_obj_set_style_line_rounded(clock_hands, true, 0);

        for (std::size_t i = 0; i < quota_cells.size(); ++i) {
            quota_cells[i] = plain(root, 26, 14);
            lv_obj_set_pos(quota_cells[i], 16 + static_cast<int>(i) * 29, 151);
            lv_obj_set_style_border_width(quota_cells[i], 1, 0);
            lv_obj_set_style_border_color(quota_cells[i], lv_color_hex(0x355365), 0);
            lv_obj_set_style_radius(quota_cells[i], 3, 0);
            quota_fill[i] = plain(quota_cells[i], 24, 10);
            lv_obj_set_pos(quota_fill[i], 1, 2);
            lv_obj_set_style_bg_opa(quota_fill[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(quota_fill[i], 1, 0);
        }
        for (std::size_t i = 0; i < beat_dots.size(); ++i) {
            beat_dots[i] = plain(root, 8, 6);
            lv_obj_set_pos(beat_dots[i], 44 + static_cast<int>(i) * 15, 203);
            lv_obj_set_style_radius(beat_dots[i], 3, 0);
            lv_obj_set_style_bg_opa(beat_dots[i], LV_OPA_COVER, 0);
        }
        codex_mark = label(root, &MontserratSemiBold26, kMint);
        lv_label_set_text(codex_mark, ">_");
        place(codex_mark, 16, 19, 46);
        title = label(root, &BUILTIN_TEXT_FONT, kText);
        number = label(root, &MontserratSemiBold26, kText);
        lv_obj_set_style_transform_pivot_x(number, 0, 0);
        lv_obj_set_style_transform_pivot_y(number, 0, 0);
        unit = label(root, &lv_font_montserrat_14, kMuted);
        detail = label(root, &lv_font_montserrat_14, kMuted);
        progress = lv_bar_create(root);
        lv_obj_remove_flag(progress, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_size(progress, 288, 6);
        lv_obj_set_pos(progress, 16, 230);
        lv_obj_set_style_bg_color(progress, lv_color_hex(0x343B78), LV_PART_MAIN);
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
        const bool eye = value.program == Program::EyeRest;
        const bool motion = eye && value.eye_phase == EyePhase::Motion;
        const bool rest = eye && value.eye_phase == EyePhase::Rest;
        const bool countdown = value.program == Program::EyeCountdown;
        const bool weekly = value.program == Program::Weekly;
        const auto beat = motion ? motion_cue_index(value.phase_elapsed_ms) : kNoBeat;
        if (rendered && value.program == previous_program && value.generation == previous_generation &&
            value.eye_phase == previous_phase && beat == previous_beat && now - last_render_ms < 33) return;
        rendered = true;
        last_render_ms = now;
        const auto mood = motion ? kGroupMoods[std::min<std::uint32_t>(3, beat / 4)] : value.program == Program::RestDone ? distance_pet::Mood::Happy :
                          eye ? distance_pet::Mood::Affection : countdown ? distance_pet::Mood::Curious :
                          value.weekly_status != WeeklyStatus::Fresh || value.weekly_left < 0 ? distance_pet::Mood::Sad :
                          value.weekly_left <= 20 ? distance_pet::Mood::Sad :
                          value.weekly_left >= 60 ? distance_pet::Mood::Happy : distance_pet::Mood::Curious;
        if (value.program != previous_program || value.eye_phase != previous_phase || value.weekly_status != previous_status ||
            value.generation != previous_generation || mood != previous_mood) {
            pet->setMood(mood, now);
        }
        previous_program = value.program;
        previous_phase = value.eye_phase;
        previous_status = value.weekly_status;
        previous_generation = value.generation;
        previous_mood = mood;
        previous_beat = beat;
        distance_pet::EyeCue eye_cue;
        if (motion) {
            const auto gaze = motion_gaze_offset(value.phase_elapsed_ms);
            eye_cue = {gaze.yaw_delta * 0.15f, gaze.pitch_delta * 0.2f,
                       motion_blink_open(value.phase_elapsed_ms)};
        }
        pet->tick(now, distance_pet::Signal::Ready, false, eye_cue, motion);
        show(face_host, weekly || motion || value.program == Program::RestDone);
        show(clock_ring, countdown);
        show(clock_hands, countdown);
        show(codex_mark, weekly);
        for (auto* cell : quota_cells) show(cell, weekly);
        for (auto* dot : beat_dots) show(dot, motion);
        show(number, countdown || weekly || rest);
        show(unit, !motion && value.program != Program::RestDone);
        show(progress, rest || motion || value.program == Program::RestDone);
        lv_obj_set_style_bg_color(root, lv_color_hex(countdown ? 0x291B0D : weekly ? 0x0B1F2A : rest ? 0x172D96 : 0), 0);
        lv_obj_set_style_bg_grad_color(root, lv_color_hex(0x6E35CB), 0);
        lv_obj_set_style_bg_grad_dir(root, rest ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_text_font(title, &BUILTIN_TEXT_FONT, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(countdown ? kAmber : kText), 0);
        lv_obj_set_style_text_align(title, weekly ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(number, weekly ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(unit, weekly ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(detail, weekly ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(detail, lv_color_hex(kMuted), 0);
        place(title, weekly ? 66 : 16, 20, weekly ? 238 : 288);
        place(number, 78, 78, 82, 512);
        place(unit, 16, 151, 288);
        place(detail, 16, 205, 288);
        place(face_host, weekly ? 200 : 30, weekly ? 60 : 12, 320, weekly ? 80 : 208);
        char text[64];
        if (countdown) {
            lv_label_set_text(title, "NEXT EYE BREAK");
            const auto seconds = value.eye_remaining_ms / 1000 + (value.eye_remaining_ms % 1000 != 0);
            std::snprintf(text, sizeof(text), "%02lu:%02lu", static_cast<unsigned long>(seconds / 60),
                          static_cast<unsigned long>(seconds % 60));
            lv_label_set_text(number, text);
            place(number, 78, 88, 110, 384);
            lv_obj_set_style_text_color(number, lv_color_hex(kAmber), 0);
            lv_label_set_text(unit, "UNTIL REST");
            lv_obj_set_style_text_color(unit, lv_color_hex(0xE6C8A0), 0);
            std::snprintf(text, sizeof(text), "REST %lus  /  LOOK 6m AWAY", static_cast<unsigned long>(
                          value.config.rest_duration_ms / 1000 + (value.config.rest_duration_ms % 1000 != 0)));
            lv_label_set_text(detail, text);
            const auto duration = std::max<std::uint32_t>(1, value.config.eye_interval_ms);
            lv_arc_set_value(clock_ring, static_cast<int>(std::min<std::uint64_t>(1000,
                             static_cast<std::uint64_t>(value.eye_remaining_ms) * 1000 / duration)));
        } else if (weekly) {
            lv_label_set_text(title, "CODEX WEEKLY");
            place(number, 16, 73, 82, 512);
            const bool available = value.weekly_left >= 0 && value.weekly_left <= 100 && value.weekly_status == WeeklyStatus::Fresh;
            const auto color = value.weekly_status == WeeklyStatus::Stale ? kAmber : !available ? kMuted :
                               value.weekly_left <= 20 ? kRose : kMint;
            if (available) std::snprintf(text, sizeof(text), "%d%%", value.weekly_left);
            else std::snprintf(text, sizeof(text), "--");
            lv_label_set_text(number, text);
            lv_obj_set_style_text_color(number, lv_color_hex(color), 0);
            if (value.weekly_status == WeeklyStatus::Waiting) std::snprintf(text, sizeof(text), "WAITING FOR CODEX");
            else if (value.weekly_status == WeeklyStatus::Stale) format_time(text, sizeof(text), value.weekly_observed_at, "STALE");
            else if (!available) std::snprintf(text, sizeof(text), "UNAVAILABLE");
            else format_time(text, sizeof(text), value.weekly_observed_at, "FRESH");
            lv_label_set_text(unit, text);
            lv_obj_set_style_text_color(unit, lv_color_hex(color), 0);
            place(unit, 16, 185, 288);
            format_time(text, sizeof(text), value.weekly_reset_at, "RESET");
            lv_label_set_text(detail, text);
            place(detail, 16, 210, 288);
            for (std::size_t i = 0; i < quota_fill.size(); ++i) {
                const int fill = available ? std::clamp(value.weekly_left - static_cast<int>(i) * 10, 0, 10) : 0;
                show(quota_fill[i], fill > 0);
                lv_obj_set_width(quota_fill[i], std::max(1, fill * 24 / 10));
                lv_obj_set_style_bg_color(quota_fill[i], lv_color_hex(color), 0);
            }
        } else if (motion) {
            lv_label_set_text(title, "FOLLOW THE BEAT");
            lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
            place(title, 16, 8, 288);
            const auto target = motion_visual_offset(value.phase_elapsed_ms);
            place(face_host, 15 + static_cast<int>(std::lround(target.yaw_delta / 10)),
                  10 + static_cast<int>(std::lround(target.pitch_delta / 10)), 320, 232);
            const auto group = motion_group(value.phase_elapsed_ms);
            const auto step = motion_step(value.phase_elapsed_ms);
            const char* direction = group == 1 ? "LEFT" : group == 2 ? "RIGHT" : group == 3 ? "UP" : "DOWN";
            if (motion_is_pause(value.phase_elapsed_ms))
                std::snprintf(text, sizeof(text), "%lu / 4  BREATHE", static_cast<unsigned long>(group));
            else if (motion_is_center(value.phase_elapsed_ms))
                std::snprintf(text, sizeof(text), "%lu / 4  CENTER", static_cast<unsigned long>(group));
            else
                std::snprintf(text, sizeof(text), "%lu / 4  %s %lu / 2", static_cast<unsigned long>(group),
                              direction, static_cast<unsigned long>(step));
            lv_label_set_text(detail, text);
            place(detail, 16, 215, 288);
            for (std::size_t i = 0; i < beat_dots.size(); ++i)
                lv_obj_set_style_bg_color(beat_dots[i], lv_color_hex(i == beat ? kMint : 0x283A3A), 0);
            lv_bar_set_value(progress, static_cast<int>(std::min<std::uint64_t>(1000,
                             static_cast<std::uint64_t>(value.phase_elapsed_ms) * 1000 / kMotionDurationMs)), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(progress, lv_color_hex(kMint), LV_PART_INDICATOR);
        } else if (rest) {
            lv_label_set_text(title, "REST");
            place(number, 37, 69, 82, 768);
            place(unit, 16, 164, 288);
            place(detail, 16, 200, 288);
            std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value.remaining_ms / 1000 +
                          (value.remaining_ms % 1000 != 0)));
            lv_label_set_text(number, text);
            lv_obj_set_style_text_color(number, lv_color_hex(0xFFF0A8), 0);
            lv_label_set_text(unit, "SECONDS LEFT");
            lv_obj_set_style_text_color(unit, lv_color_hex(0xD5E9FF), 0);
            lv_label_set_text(detail, "LOOK 6m AWAY");
            lv_obj_set_style_text_color(detail, lv_color_white(), 0);
            const auto duration = std::max<std::uint32_t>(1, value.config.rest_duration_ms);
            lv_bar_set_value(progress, static_cast<int>(std::min<std::uint64_t>(1000,
                             static_cast<std::uint64_t>(value.remaining_ms) * 1000 / duration)), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(progress, lv_color_hex(0x88FFEC), LV_PART_INDICATOR);
        } else if (eye) {
            lv_label_set_text(title, "GET READY");
            place(title, 16, 75, 288);
            lv_label_set_text(unit, "SHOREKEEPER");
            place(unit, 16, 119, 288);
            lv_obj_set_style_text_color(unit, lv_color_hex(kMint), 0);
            lv_label_set_text(detail, "Starting eye break...");
        } else {
            lv_label_set_text(title, "REST COMPLETE");
            lv_label_set_text(detail, "Nice! Back to your day");
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
