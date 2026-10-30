#pragma once

#include <algorithm>

// Avatar's element interfaces need only these math types, not LVGL widgets.
namespace smooth_ui_toolkit {

template <typename T>
constexpr T clamp(T value, T minimum, T maximum)
{
    return std::clamp(value, minimum, maximum);
}

struct Vector2i {
    int x = 0;
    int y = 0;

    void clamp(const Vector2i& minimum, const Vector2i& maximum)
    {
        x = smooth_ui_toolkit::clamp(x, minimum.x, maximum.x);
        y = smooth_ui_toolkit::clamp(y, minimum.y, maximum.y);
    }
};

} // namespace smooth_ui_toolkit
