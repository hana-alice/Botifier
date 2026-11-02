#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>

namespace idle_program::art {

inline constexpr std::uint32_t width = 160;
inline constexpr std::uint32_t height = 160;
inline constexpr std::size_t frame_count = 6;
inline constexpr std::size_t frame_bytes = width * height * 4;

// A gentle wave/blink, followed by a still pose. No LVGL timer or worker owns
// the animation: clearing the programme immediately clears all of its artwork.
inline std::size_t frame_index(std::uint32_t elapsed_ms) noexcept
{
    const auto phase = elapsed_ms % 3600u;
    return phase < 1800u ? phase / 300u : 0u;
}

const lv_image_dsc_t* shorekeeper_frame(std::size_t index) noexcept;

} // namespace idle_program::art
