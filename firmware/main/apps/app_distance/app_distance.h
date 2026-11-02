#pragma once

#include <mooncake.h>
#include <cstdint>
#include <memory>

// A live distance-warning face and explicit, persistent proximity tuning.
class AppDistance : public mooncake::AppAbility {
public:
    AppDistance();
    ~AppDistance() override;

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    struct View;
    std::unique_ptr<View> _view;
    std::uint32_t _last_refresh_ms = 0;
    std::uint32_t _last_nfc_event = 0;
    std::uint32_t _intro_completed_count = 0;
    std::uint32_t _eye_intro_generation = 0;
    std::uint32_t _eye_intro_completion_baseline = 0;
    bool _eye_intro_started = false;
};
