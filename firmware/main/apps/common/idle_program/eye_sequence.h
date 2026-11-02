#pragma once

#include <array>
#include <cstdint>

namespace idle_program {

inline constexpr std::uint32_t kMotionBeatMs = 500u;
inline constexpr std::uint32_t kMotionDurationMs = 4500u;

// Relative offsets in tenths of a degree; the final beat returns to centre.
struct MotionBeat {
    std::int16_t yaw_delta;
    std::int16_t pitch_delta;
};

inline constexpr std::array<MotionBeat, 9> kMotionBeats{{
    {0, 0}, {-80, 0}, {80, 0}, {-60, 0}, {60, 0},
    {0, -40}, {0, 40}, {0, -25}, {0, 0},
}};

inline constexpr MotionBeat motion_beat(std::uint32_t elapsed_ms) noexcept
{
    return elapsed_ms >= kMotionDurationMs ? MotionBeat{0, 0}
                                          : kMotionBeats[elapsed_ms / kMotionBeatMs];
}

} // namespace idle_program
