#pragma once

#include <mooncake.h>
#include <cstdint>
#include <memory>

// Read-only diagnostics for the process-lifetime distance sampler.
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
};
