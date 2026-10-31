#pragma once

#include "hand_gate.h"
#include <esp_err.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace snap_sense {

struct HandDetections {
    std::array<HandBox, 4> boxes{};
    std::size_t count = 0;
};

// Owned and called synchronously by the camera worker. The caller owns the RGB
// buffer; inference keeps no reference to it or to the returned detections.
class HandModel {
public:
    HandModel() = default;
    ~HandModel();
    HandModel(const HandModel&) = delete;
    HandModel& operator=(const HandModel&) = delete;

    esp_err_t init();
    esp_err_t infer(const std::uint8_t* rgb, std::size_t width, std::size_t height,
                    HandDetections& detections);

private:
    class CheckedDetector;
    CheckedDetector* detector_ = nullptr;
};

}  // namespace snap_sense
