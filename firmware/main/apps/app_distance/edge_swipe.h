#pragma once

#include <cstdint>

namespace distance_ui {

enum class SwipeEdge { None, Top, Bottom };

// Native input owns the pressed/released events. No intermediate motion events
// are required, and every release consumes its gesture before validation.
class EdgeSwipe {
public:
    void press(int x, int y, std::uint32_t now_ms) noexcept
    {
        if (pressed_) {
            edge_ = SwipeEdge::None;
            return;
        }
        pressed_ = true;
        start_x_ = x;
        start_y_ = y;
        pressed_at_ms_ = now_ms;
        if (!inside(x, y)) edge_ = SwipeEdge::None;
        else if (y < 28) edge_ = SwipeEdge::Top;
        else if (y >= 240 - 28) edge_ = SwipeEdge::Bottom;
        else edge_ = SwipeEdge::None;
    }

    SwipeEdge release(int x, int y, std::uint32_t now_ms) noexcept
    {
        const auto edge = edge_;
        const bool pressed = pressed_;
        reset();
        if (!pressed || edge == SwipeEdge::None || !inside(x, y) ||
            now_ms - pressed_at_ms_ > 4000u) return SwipeEdge::None;
        const int dx = x - start_x_;
        const int dy = y - start_y_;
        const int horizontal = dx < 0 ? -dx : dx;
        const int inward = edge == SwipeEdge::Top ? dy : -dy;
        return inward >= 42 && inward > horizontal + 8 ? edge : SwipeEdge::None;
    }

    void reset() noexcept
    {
        pressed_ = false;
        edge_ = SwipeEdge::None;
    }

private:
    static bool inside(int x, int y) noexcept
    {
        return x >= 0 && x < 320 && y >= 0 && y < 240;
    }

    bool pressed_ = false;
    SwipeEdge edge_ = SwipeEdge::None;
    int start_x_ = 0;
    int start_y_ = 0;
    std::uint32_t pressed_at_ms_ = 0;
};

} // namespace distance_ui
