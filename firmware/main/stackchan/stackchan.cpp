/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stackchan.h"
#include <distance_sensor/service.h>

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
        _distance_warning = std::make_unique<avatar::AngryDecorator>(lv_screen_active(), 0, 500);
    }
    _distance_warning->_update();
}
