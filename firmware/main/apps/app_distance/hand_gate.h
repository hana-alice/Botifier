#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace snap_sense {

struct HandBox {
    bool detected = false;
    float score = 0;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// A large hand near the upper center is the requested interaction position;
// this geometric rule does not classify keyboard use. Coordinates are inclusive
// pixels from the model. Extents conservatively use right-left/bottom-top, so
// they are one pixel smaller than inclusive pixel counts.
inline bool is_interaction_hand(const HandBox& box, std::size_t width, std::size_t height) noexcept
{
    if (!box.detected || !std::isfinite(box.score) || box.score < 0.5f ||
        width == 0 || height == 0 || width > 4096 || height > 4096) return false;
    const auto frame_width = static_cast<int>(width);
    const auto frame_height = static_cast<int>(height);
    if (box.left < 0 || box.top < 0 || box.right <= box.left || box.bottom <= box.top ||
        box.right >= frame_width || box.bottom >= frame_height) return false;
    const auto box_width = box.right - box.left;
    const auto box_height = box.bottom - box.top;
    const auto center_x_twice = box.left + box.right;
    const auto center_y_twice = box.top + box.bottom;
    // After bounds validation all products fit int (at most 65520).
    return box_width * 6 >= frame_width && box_height * 4 >= frame_height &&
           center_x_twice * 5 >= frame_width * 2 && center_x_twice * 5 <= frame_width * 8 &&
           center_y_twice * 8 >= frame_height * 2 && center_y_twice * 3 <= frame_height * 4;
}

enum class HandPhase { NeedClear, Looking, Stabilizing, Armed };

struct HandGateState {
    HandPhase phase = HandPhase::NeedClear;
    bool available = false;
    bool armed = false;
    std::uint32_t captured_ms = 0;
};

// A single presentation grants one bounded permission. Model confidence and
// placement checks belong to the caller; this class only tracks eligible frames.
class HandGate {
public:
    void reset() noexcept { *this = HandGate{}; }

    void observe(std::uint32_t captured_ms, bool eligible, bool frame_valid = true) noexcept
    {
        if (have_frame_) {
            const auto elapsed = captured_ms - captured_ms_;
            // Equal timestamps cannot supply a second frame. Keep the time
            // high-water mark when rejecting late input, including across wrap.
            if (elapsed == 0 || elapsed > 0x7fffffffu) {
                need_clear();
                frame_valid_ = false;
                return;
            }
            if (elapsed > kFreshMs) need_clear();
        }
        have_frame_ = true;
        captured_ms_ = captured_ms;
        frame_valid_ = frame_valid;
        if (!frame_valid) {
            need_clear();
            return;
        }

        if (phase_ == HandPhase::Armed && captured_ms - armed_ms_ >= kArmedMs) need_clear();

        switch (phase_) {
        case HandPhase::NeedClear:
            if (eligible) {
                clearing_ = false;
            } else if (!clearing_) {
                clearing_ = true;
                phase_started_ms_ = captured_ms;
            } else if (captured_ms - phase_started_ms_ >= kClearMs) {
                phase_ = HandPhase::Looking;
                clearing_ = false;
            }
            break;
        case HandPhase::Looking:
            if (eligible) {
                phase_ = HandPhase::Stabilizing;
                phase_started_ms_ = captured_ms;
            }
            break;
        case HandPhase::Stabilizing:
            if (!eligible) {
                phase_ = HandPhase::Looking;
            } else if (captured_ms - phase_started_ms_ >= kStableMs) {
                phase_ = HandPhase::Armed;
                armed_ms_ = captured_ms;
            }
            break;
        case HandPhase::Armed:
            // Valid frames retain freshness, never extend the permission.
            break;
        }
    }

    void consume() noexcept { need_clear(); }

    HandGateState snapshot(std::uint32_t now_ms) const noexcept
    {
        HandGateState result;
        result.captured_ms = captured_ms_;
        result.available = have_frame_ && frame_valid_ && now_ms - captured_ms_ <= kFreshMs;
        if (!result.available) return result;
        result.phase = phase_;
        result.armed = phase_ == HandPhase::Armed && now_ms - armed_ms_ < kArmedMs;
        if (phase_ == HandPhase::Armed && !result.armed) result.phase = HandPhase::NeedClear;
        return result;
    }

private:
    void need_clear() noexcept
    {
        phase_ = HandPhase::NeedClear;
        clearing_ = false;
    }

    static constexpr std::uint32_t kClearMs = 250;
    static constexpr std::uint32_t kStableMs = 100;
    static constexpr std::uint32_t kFreshMs = 500;
    static constexpr std::uint32_t kArmedMs = 1800;

    HandPhase phase_ = HandPhase::NeedClear;
    bool have_frame_ = false;
    bool frame_valid_ = false;
    bool clearing_ = false;
    std::uint32_t captured_ms_ = 0;
    std::uint32_t phase_started_ms_ = 0;
    std::uint32_t armed_ms_ = 0;
};

}  // namespace snap_sense
