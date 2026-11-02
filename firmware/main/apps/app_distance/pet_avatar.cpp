#include "pet_avatar.h"

#include <esp_random.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr std::uint32_t kEyes = 0xBDF8EE;
constexpr std::uint32_t kSerious = 0xFFD0B6;
constexpr std::uint32_t kCheeks = 0xFF91AE;
constexpr std::uint32_t kTears = 0x8ACDFF;

int pixel(float value) { return static_cast<int>(std::lround(value)); }
lv_opa_t opacity(float value) { return static_cast<lv_opa_t>(pixel(std::clamp(value, 0.0f, 1.0f) * 255)); }

lv_obj_t* container(lv_obj_t* parent, int width, int height)
{
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return object;
}

lv_obj_t* stroke(lv_obj_t* parent, int width)
{
    auto* line = lv_line_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_remove_flag(line, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

class PetEye final : public stackchan::avatar::Feature {
public:
    explicit PetEye(lv_obj_t* parent)
    {
        _group = container(parent, 84, 100);
        lv_obj_set_style_transform_pivot_x(_group, 42, 0);
        lv_obj_set_style_transform_pivot_y(_group, 50, 0);
        _eye = container(_group, 46, 58);
        _shine = container(_group, 8, 10);
        lv_obj_set_style_bg_color(_shine, lv_color_white(), 0);
        lv_obj_set_style_radius(_shine, LV_RADIUS_CIRCLE, 0);
        _lid = container(_group, 80, 60);
        lv_obj_set_style_bg_color(_lid, lv_color_black(), 0);
        _smile = stroke(_group, 8);
        lv_obj_set_size(_smile, 84, 100);
    }

    ~PetEye() override { lv_obj_delete(_group); }

    void setVisible(bool visible) override
    {
        Element::setVisible(visible);
        if (visible) lv_obj_remove_flag(_group, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(_group, LV_OBJ_FLAG_HIDDEN);
    }

    void render(const distance_pet::EyePose& pose, lv_color_t color)
    {
        lv_obj_set_pos(_group, pixel(pose.x) - 42, pixel(pose.y) - 50);
        const int angle = ((pixel(pose.tilt_deg * 10) % 3600) + 3600) % 3600;
        lv_obj_set_style_transform_rotation(_group, angle, 0);
        const int width = std::max(2, pixel(pose.width));
        const float smile = std::clamp(pose.smile * 2, 0.0f, 1.0f);
        const float flatten = std::min(1.0f, smile * 2);
        const int thickness = std::clamp(pixel(pose.height), 3, 8);
        const int height = std::max(2, pixel(pose.height * (1 - flatten) + thickness * flatten));
        lv_obj_set_size(_eye, width, height);
        lv_obj_set_pos(_eye, 42 - width / 2, 50 - height / 2);
        lv_obj_set_style_radius(_eye, std::min(width, height) / 2, 0);
        lv_obj_set_style_bg_color(_eye, color, 0);
        lv_obj_set_style_bg_opa(_eye, smile < 0.5f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_pos(_shine, 42 - width / 2 + 8, 50 - height / 2 + 7);
        lv_obj_set_style_bg_opa(_shine, opacity((1 - flatten) * std::clamp((pose.height - 14) / 32, 0.0f, 0.65f)), 0);
        lv_obj_set_pos(_lid, 2, pixel(50 - pose.height * 0.1f) - 60);
        lv_obj_set_style_bg_opa(_lid, opacity(std::clamp((std::abs(pose.tilt_deg) - 10) / 7, 0.0f, 1.0f) * (1 - smile)), 0);

        const float bend = std::max(0.0f, smile * 2 - 1) * std::clamp(pose.height / 12, 0.0f, 1.0f);
        for (std::size_t i = 0; i < _points.size(); ++i) {
            const float u = static_cast<float>(i) / (_points.size() - 1);
            const float x = u * 2 - 1;
            _points[i] = {static_cast<lv_value_precise_t>(42 + x * (pose.width - thickness) * 0.5f),
                          static_cast<lv_value_precise_t>(50 + bend * (5 - 11 * (1 - x * x)))};
        }
        // LVGL retains the point address; storage lives with this feature.
        lv_obj_invalidate(_smile);
        lv_line_set_points_mutable(_smile, _points.data(), _points.size());
        lv_obj_set_style_line_color(_smile, color, 0);
        lv_obj_set_style_line_width(_smile, thickness, 0);
        lv_obj_set_style_line_opa(_smile, smile >= 0.5f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }

private:
    lv_obj_t* _group = nullptr;
    lv_obj_t* _eye = nullptr;
    lv_obj_t* _shine = nullptr;
    lv_obj_t* _lid = nullptr;
    lv_obj_t* _smile = nullptr;
    std::array<lv_point_precise_t, 17> _points{};
};

class PetMouth final : public stackchan::avatar::Feature {
public:
    explicit PetMouth(lv_obj_t* parent)
    {
        _group = container(parent, 84, 48);
        _line = stroke(_group, 5);
        lv_obj_set_size(_line, 84, 48);
    }

    ~PetMouth() override { lv_obj_delete(_group); }

    void setVisible(bool visible) override
    {
        Element::setVisible(visible);
        if (visible) lv_obj_remove_flag(_group, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(_group, LV_OBJ_FLAG_HIDDEN);
    }

    void render(const distance_pet::Frame& frame, lv_color_t color)
    {
        lv_obj_set_pos(_group, pixel(frame.mouth_x) - 42, pixel(frame.mouth_y) - 24);
        for (std::size_t i = 0; i < _points.size(); ++i) {
            const float angle = static_cast<float>(i) / (_points.size() - 1) * 6.283185307f;
            const float x = std::cos(angle);
            _points[i] = {static_cast<lv_value_precise_t>(42 + x * frame.mouth_width * 0.5f),
                          static_cast<lv_value_precise_t>(24 + frame.mouth_curve * frame.mouth_height *
                                                       (0.7f * (1 - x * x) - 0.35f) +
                                                       std::sin(angle) * frame.mouth_height * frame.mouth_open * 0.5f)};
        }
        lv_obj_invalidate(_line);
        lv_line_set_points_mutable(_line, _points.data(), _points.size());
        lv_obj_set_style_line_color(_line, color, 0);
    }

private:
    lv_obj_t* _group = nullptr;
    lv_obj_t* _line = nullptr;
    std::array<lv_point_precise_t, 41> _points{};
};

}  // namespace

struct DistancePetAvatar::View {
    lv_obj_t* panel = nullptr;
    lv_obj_t* left_cheek = nullptr;
    lv_obj_t* right_cheek = nullptr;
    lv_obj_t* tear = nullptr;

    explicit View(lv_obj_t* parent)
    {
        panel = container(parent, 320, 240);
        lv_obj_set_pos(panel, 0, 0);
        lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        for (auto** cheek : {&left_cheek, &right_cheek}) {
            *cheek = container(panel, 27, 11);
            lv_obj_set_style_bg_color(*cheek, lv_color_hex(kCheeks), 0);
            lv_obj_set_style_radius(*cheek, LV_RADIUS_CIRCLE, 0);
        }
        tear = container(panel, 9, 15);
        lv_obj_set_style_bg_color(tear, lv_color_hex(kTears), 0);
        lv_obj_set_style_radius(tear, LV_RADIUS_CIRCLE, 0);
    }

    ~View() { lv_obj_delete(panel); }
};

DistancePetAvatar::DistancePetAvatar() : _controller(esp_random()) {}

DistancePetAvatar::~DistancePetAvatar()
{
    // Features own LVGL groups parented to panel; release them before panel.
    clearDecorators();
    _key_elements = {};
    _view.reset();
}

void DistancePetAvatar::init(lv_obj_t* parent)
{
    _view = std::make_unique<View>(parent);
    _key_elements.leftEye = std::make_unique<PetEye>(_view->panel);
    _key_elements.rightEye = std::make_unique<PetEye>(_view->panel);
    _key_elements.mouth = std::make_unique<PetMouth>(_view->panel);
}

void DistancePetAvatar::setMood(distance_pet::Mood mood, std::uint32_t now_ms)
{
    _controller.play(mood, now_ms);
    _started = false;
}

void DistancePetAvatar::tick(std::uint32_t now_ms, distance_pet::Signal signal, bool react)
{
    if (!react && _started && signal == _last_signal && now_ms - _last_tick < 33) return;
    _started = true;
    _last_tick = now_ms;
    _last_signal = signal;
    auto frame = _controller.update(now_ms, signal);
    if (react && signal == distance_pet::Signal::Ready) {
        _controller.react(now_ms);
        frame = _controller.update(now_ms, signal);
    }
    const auto color = lv_color_hex(frame.mood == distance_pet::Mood::Alert ? kSerious : kEyes);
    static_cast<PetEye*>(_key_elements.leftEye.get())->render(frame.left_eye, color);
    static_cast<PetEye*>(_key_elements.rightEye.get())->render(frame.right_eye, color);
    static_cast<PetMouth*>(_key_elements.mouth.get())->render(frame, color);
    lv_obj_set_pos(_view->left_cheek, pixel(frame.left_eye.x) - 42, pixel(frame.left_eye.y) + 40);
    lv_obj_set_pos(_view->right_cheek, pixel(frame.right_eye.x) + 15, pixel(frame.right_eye.y) + 40);
    lv_obj_set_style_bg_opa(_view->left_cheek, opacity(frame.cheek * 0.8f), 0);
    lv_obj_set_style_bg_opa(_view->right_cheek, opacity(frame.cheek * 0.8f), 0);
    lv_obj_set_pos(_view->tear, pixel(frame.left_eye.x) - 32, pixel(frame.left_eye.y) + 26);
    lv_obj_set_style_bg_opa(_view->tear, opacity(frame.tear), 0);
}
