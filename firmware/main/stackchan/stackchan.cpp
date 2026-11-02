/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stackchan.h"
#include <distance_sensor/service.h>

namespace {

// Own only warning graphics. Removing them exposes the current face instead
// of restoring a saved emotion over newer AI or remote expression updates.
class DistanceWarningDecorator final : public stackchan::avatar::AngryDecorator {
public:
    explicit DistanceWarningDecorator(lv_obj_t* parent) : AngryDecorator(parent, 0, 0)
    {
        const auto red = lv_color_hex(0xED1724);
        // AngryDecorator just created this image as the parent's last child.
        // Keep that real warning glyph above the opaque panel created next.
        auto* warning_icon = lv_obj_get_child(parent, -1);
        setScale(512); // LVGL 256 = 1x; 40 px becomes 80 px.
        setPosition(0, -40);
        setRotation(0);
        setColor(lv_color_white());

        _frame = lv_obj_create(parent);
        lv_obj_remove_style_all(_frame);
        lv_obj_set_size(_frame, lv_pct(100), lv_pct(100));
        lv_obj_center(_frame);
        lv_obj_remove_flag(_frame, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(_frame, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(_frame, red, 0);
        lv_obj_set_style_bg_opa(_frame, LV_OPA_COVER, 0);

        auto* text = lv_label_create(_frame);
        lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(text, LV_OBJ_FLAG_SCROLLABLE);
        lv_label_set_text(text, "TOO CLOSE");
        lv_obj_set_style_text_font(text, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(text, lv_color_white(), 0);
        // Reuse the configured 24 px font at 1.5x instead of adding a font.
        lv_obj_set_style_transform_pivot_x(text, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(text, lv_pct(50), 0);
        lv_obj_set_style_transform_scale_x(text, 384, 0);
        lv_obj_set_style_transform_scale_y(text, 384, 0);
        lv_obj_align(text, LV_ALIGN_CENTER, 0, 25);
        if (warning_icon) lv_obj_move_foreground(warning_icon);
        // Do not raise either object during updates: AppDistance raises live
        // controls or the NFC intro above both after updating this decorator.
    }

    ~DistanceWarningDecorator() override
    {
        lv_obj_delete(_frame);
    }

private:
    lv_obj_t* _frame = nullptr;
};

} // namespace

stackchan::StackChan& GetStackChan()
{
    static stackchan::StackChan stackchan;
    return stackchan;
}

void stackchan::StackChan::updateDistanceWarning()
{
    const auto& face = _avatar->getKeyElements();
    const bool face_visible = (face.leftEye && face.leftEye->getVisible()) ||
                              (face.rightEye && face.rightEye->getVisible()) ||
                              (face.mouth && face.mouth->getVisible());
    if (!face_visible || !distance_sensor::alert_active()) {
        _distance_warning.reset();
        return;
    }
    // A separate decoration preserves AI emotions and remote custom eye shapes.
    // Its lifetime follows the attached avatar, not the sensor task or menu app.
    if (!_distance_warning) {
        _distance_warning = std::make_unique<DistanceWarningDecorator>(lv_screen_active());
    }
    _distance_warning->_update();
}
