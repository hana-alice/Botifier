/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <hal/hal.h>
#include <cstdint>
#include <atomic>

namespace stackchan {

/**
 * @brief
 *
 */
class ImuEventModifier : public Modifier {
public:
    ImuEventModifier(uint32_t reactionDurationMs = 4000) : _reaction_duration_ms(reactionDurationMs)
    {
        _signal_connection = GetHAL().onImuMotionEvent.connect([this](ImuMotionEvent event) {
            if (event == ImuMotionEvent::Shake) {
                record_shake();
            }
        });
    }

    ~ImuEventModifier()
    {
        GetHAL().onImuMotionEvent.disconnect(_signal_connection);
    }

    void _update(Modifiable& stackchan) override
    {
        if (stackchan.interactionsSuspended() || !stackchan.hasAvatar()) {
            disable_events();
            restore_state(stackchan);
            return;
        }
        const auto events = _events.fetch_and(~kShake, std::memory_order_relaxed);
        _events.fetch_or(kAcceptEvents, std::memory_order_relaxed);
        if (_active_avatar && _active_avatar != &stackchan.avatar()) restore_state(stackchan);
        uint32_t now = GetHAL().millis();

        // 收到晃动事件
        if (events & kShake) {
            handle_shake_start(stackchan, now);
        }

        // 如果处于晃动反应状态
        if (_is_reacting) {
            if (now >= _next_toggle_tick) {
                _next_toggle_tick = now + 600;

                _toggle_phase      = !_toggle_phase;
                int mouth_rotation = _toggle_phase ? -25 : 25;

                auto& avatar = stackchan.avatar();
                avatar.mouth().setRotation(mouth_rotation);
                avatar.mouth().setWeight(65);
            }

            //  Lock motion modify and move home
            auto& motion = stackchan.motion();
            if (_owns_motion_lock && !motion.isModifyLocked()) {
                _owns_motion_lock = false;
                _motion_ownership_lost = true;
            }
            if (!_motion_ownership_lost && !motion.isModifyLocked()) {
                motion.setModifyLock(true);
                _owns_motion_lock = true;
                motion.goHome(300);
            }

            // 检查是否结束反应
            if (now >= _restore_at) {
                restore_state(stackchan);
            }
        }
    }

private:
    void record_shake()
    {
        auto pending = _events.load(std::memory_order_relaxed);
        const auto generation = pending & ~(kEpochStep - 1u);
        while ((pending & kAcceptEvents) && (pending & ~(kEpochStep - 1u)) == generation) {
            if (_events.compare_exchange_weak(pending, pending | kShake, std::memory_order_relaxed)) return;
        }
    }

    void disable_events()
    {
        auto pending = _events.load(std::memory_order_relaxed);
        while (pending & kAcceptEvents) {
            const auto disabled = (pending + kEpochStep) & ~(kEpochStep - 1u);
            if (_events.compare_exchange_weak(pending, disabled, std::memory_order_relaxed)) return;
        }
    }

    void handle_shake_start(Modifiable& stackchan, uint32_t now)
    {
        if (!_is_reacting) {
            // 首次触发时，记录状态以便恢复
            auto& avatar = stackchan.avatar();
            // A legacy reaction must not take over an already-owned face.
            if (avatar.isModifyLocked()) return;
            _is_reacting = true;
            _active_avatar = &avatar;
            _motion_ownership_lost = false;

            avatar.setModifyLock(true);
            _owns_avatar_lock = true;
            avatar.leftEye().setVisible(false);
            avatar.rightEye().setVisible(false);

            stackchan.avatar().removeDecorator(_dizzy_decorator_id);
            stackchan.avatar().removeDecorator(_shy_decorator_id);
            _dizzy_decorator_id =
                stackchan.avatar().addDecorator(std::make_unique<avatar::DizzyDecorator>(lv_screen_active(), 0, 300));
            _shy_decorator_id =
                stackchan.avatar().addDecorator(std::make_unique<avatar::ShyDecorator>(lv_screen_active(), 0));
        }

        // 刷新恢复时间和切换时间
        _restore_at = now + _reaction_duration_ms;
        if (_next_toggle_tick <= now) {
            _next_toggle_tick = now;  // 立即触发第一次嘴巴动作
        }
    }

    void restore_state(Modifiable& stackchan)
    {
        if (!_is_reacting) {
            return;
        }

        if (stackchan.hasAvatar() && _active_avatar == &stackchan.avatar()) {
            auto& avatar = stackchan.avatar();
            if (_owns_avatar_lock && avatar.isModifyLocked()) {
                avatar.leftEye().setVisible(true);
                avatar.rightEye().setVisible(true);
                avatar.mouth().setWeight(0);
                avatar.mouth().setRotation(0);
                avatar.setModifyLock(false);
            }
            avatar.removeDecorator(_dizzy_decorator_id);
            avatar.removeDecorator(_shy_decorator_id);
        }
        _dizzy_decorator_id = -1;
        _shy_decorator_id   = -1;

        auto& motion = stackchan.motion();
        if (_owns_motion_lock && motion.isModifyLocked()) motion.setModifyLock(false);

        _active_avatar = nullptr;
        _owns_avatar_lock = _owns_motion_lock = _motion_ownership_lost = false;
        _is_reacting = false;
    }

    // 信号相关
    std::size_t _signal_connection;
    static constexpr std::uint32_t kAcceptEvents = 1u;
    static constexpr std::uint32_t kShake = 2u;
    static constexpr std::uint32_t kEpochStep = 8u;
    std::atomic<std::uint32_t> _events{kAcceptEvents};
    avatar::Avatar* _active_avatar = nullptr;
    bool _owns_avatar_lock = false;
    bool _owns_motion_lock = false;
    bool _motion_ownership_lost = false;

    // 状态控制
    bool _is_reacting          = false;
    bool _toggle_phase         = false;
    uint32_t _restore_at       = 0;
    uint32_t _next_toggle_tick = 0;
    uint32_t _reaction_duration_ms;

    int _dizzy_decorator_id = -1;
    int _shy_decorator_id   = -1;
};

}  // namespace stackchan
