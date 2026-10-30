#pragma once

#include "pet_motion.h"
#include <stackchan/avatar/avatar/avatar.h>
#include <lvgl.h>
#include <memory>

// The DISTANCE app owns this skin; other apps keep their original avatars.
class DistancePetAvatar final : public stackchan::avatar::Avatar {
public:
    DistancePetAvatar();
    ~DistancePetAvatar() override;
    void init(lv_obj_t* parent);
    void tick(std::uint32_t now_ms, distance_pet::Signal signal, bool react = false);

private:
    struct View;
    std::unique_ptr<View> _view;
    distance_pet::Controller _controller;
    distance_pet::Signal _last_signal = distance_pet::Signal::Unavailable;
    std::uint32_t _last_tick = 0;
    bool _started = false;
};
