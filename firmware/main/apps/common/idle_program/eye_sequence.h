#pragma once

#include <array>
#include <cstdint>

namespace idle_program {

inline constexpr std::uint32_t kMotionBeatMs = 1000u;
inline constexpr std::uint32_t kMotionDurationMs = 12000u;
inline constexpr std::uint16_t kMotionMoveMs = 350u;

// Relative offsets in tenths of a degree. Left/right/up/down each receive two
// one-second beats followed by a complete one-second hold at centre.
struct MotionBeat {
    std::int16_t yaw_delta;
    std::int16_t pitch_delta;
};

inline constexpr std::array<MotionBeat, 12> kMotionBeats{{
    {-40, 0}, {-80, 0}, {0, 0},
    {40, 0}, {80, 0}, {0, 0},
    {0, -20}, {0, -40}, {0, 0},
    {0, 20}, {0, 40}, {0, 0},
}};

inline constexpr MotionBeat motion_beat(std::uint32_t elapsed_ms) noexcept
{
    return elapsed_ms >= kMotionDurationMs ? MotionBeat{0, 0}
                                          : kMotionBeats[elapsed_ms / kMotionBeatMs];
}

struct MotionVisualOffset {
    float yaw_delta;
    float pitch_delta;
};

// The servo receives a complete target once per beat. The face follows that
// target continuously for the same duration, then holds until the next beat.
inline constexpr MotionVisualOffset motion_visual_offset(std::uint32_t elapsed_ms) noexcept
{
    if (elapsed_ms >= kMotionDurationMs) return {0, 0};
    const auto beat = elapsed_ms / kMotionBeatMs;
    const auto from = beat == 0 ? MotionBeat{0, 0} : kMotionBeats[beat - 1];
    const auto to = kMotionBeats[beat];
    const auto within = elapsed_ms % kMotionBeatMs;
    const float progress = within >= kMotionMoveMs ? 1.0f : static_cast<float>(within) / kMotionMoveMs;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    return {from.yaw_delta + (to.yaw_delta - from.yaw_delta) * eased,
            from.pitch_delta + (to.pitch_delta - from.pitch_delta) * eased};
}

// Display ordinals: group 1..4, step 1/2 or 0 for the centring hold.
// A completed sequence has no current group or step.
inline constexpr std::uint32_t motion_group(std::uint32_t elapsed_ms) noexcept
{
    return elapsed_ms >= kMotionDurationMs ? 0u : elapsed_ms / (3u * kMotionBeatMs) + 1u;
}

inline constexpr std::uint32_t motion_step(std::uint32_t elapsed_ms) noexcept
{
    return elapsed_ms >= kMotionDurationMs ? 0u : (elapsed_ms / kMotionBeatMs + 1u) % 3u;
}

inline constexpr bool motion_is_center(std::uint32_t elapsed_ms) noexcept
{
    return motion_step(elapsed_ms) == 0u;
}

} // namespace idle_program
