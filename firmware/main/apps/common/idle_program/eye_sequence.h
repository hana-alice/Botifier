#pragma once

#include <array>
#include <cstdint>

namespace idle_program {

inline constexpr std::uint32_t kMotionDurationMs = 12000u;
inline constexpr std::uint32_t kMotionGroupMs = 3000u;
inline constexpr std::uint32_t kMotionCueLeadMs = 120u;

// Relative offsets in tenths of a degree. Each direction makes two complete
// out-and-back movements, then holds at centre before the next group.
struct MotionBeat {
    std::int16_t yaw_delta;
    std::int16_t pitch_delta;
    std::uint32_t start_ms;
    std::uint16_t move_ms;
};

inline constexpr std::array<MotionBeat, 16> kMotionBeats{{
    {-60, 0, 120, 360}, {0, 0, 720, 300}, {-80, 0, 1400, 420}, {0, 0, 2100, 340},
    {60, 0, 3120, 360}, {0, 0, 3720, 300}, {80, 0, 4400, 420}, {0, 0, 5100, 340},
    {0, -30, 6120, 360}, {0, 0, 6720, 300}, {0, -40, 7400, 420}, {0, 0, 8100, 340},
    {0, 30, 9120, 360}, {0, 0, 9720, 300}, {0, 40, 10400, 420}, {0, 0, 11100, 340},
}};

inline constexpr std::uint32_t motion_index(std::uint32_t elapsed_ms) noexcept
{
    std::uint32_t index = 0;
    while (index + 1u < kMotionBeats.size() && elapsed_ms >= kMotionBeats[index + 1u].start_ms) ++index;
    return index;
}

inline constexpr std::uint32_t motion_cue_index(std::uint32_t elapsed_ms) noexcept
{
    std::uint32_t index = 0;
    while (index + 1u < kMotionBeats.size() &&
           elapsed_ms >= kMotionBeats[index + 1u].start_ms - kMotionCueLeadMs) ++index;
    return index;
}

inline constexpr MotionBeat motion_beat(std::uint32_t elapsed_ms) noexcept
{
    return elapsed_ms < kMotionBeats.front().start_ms || elapsed_ms >= kMotionDurationMs
               ? MotionBeat{0, 0, 0, 0} : kMotionBeats[motion_index(elapsed_ms)];
}

struct MotionVisualOffset {
    float yaw_delta;
    float pitch_delta;
};

inline constexpr float motion_ease(std::uint32_t elapsed_ms, std::uint32_t duration_ms) noexcept
{
    const float progress = elapsed_ms >= duration_ms ? 1.0f : static_cast<float>(elapsed_ms) / duration_ms;
    return progress * progress * (3.0f - 2.0f * progress);
}

// Follow the scheduled head target for that target's own travel duration.
inline constexpr MotionVisualOffset motion_visual_offset(std::uint32_t elapsed_ms) noexcept
{
    if (elapsed_ms < kMotionBeats.front().start_ms || elapsed_ms >= kMotionDurationMs) return {0, 0};
    const auto beat = motion_index(elapsed_ms);
    const auto from = beat == 0 ? MotionBeat{0, 0, 0, 0} : kMotionBeats[beat - 1];
    const auto to = kMotionBeats[beat];
    const auto eased = motion_ease(elapsed_ms - to.start_ms, to.move_ms);
    return {from.yaw_delta + (to.yaw_delta - from.yaw_delta) * eased,
            from.pitch_delta + (to.pitch_delta - from.pitch_delta) * eased};
}

// Eyes begin 120 ms before the head and settle on the target exactly when
// that head movement begins.
inline constexpr MotionVisualOffset motion_gaze_offset(std::uint32_t elapsed_ms) noexcept
{
    if (elapsed_ms >= kMotionDurationMs) return {0, 0};
    const auto beat = motion_cue_index(elapsed_ms);
    const auto from = beat == 0 ? MotionBeat{0, 0, 0, 0} : kMotionBeats[beat - 1];
    const auto to = kMotionBeats[beat];
    const auto eased = motion_ease(elapsed_ms - (to.start_ms - kMotionCueLeadMs), kMotionCueLeadMs);
    return {from.yaw_delta + (to.yaw_delta - from.yaw_delta) * eased,
            from.pitch_delta + (to.pitch_delta - from.pitch_delta) * eased};
}

inline constexpr float motion_blink_open(std::uint32_t elapsed_ms) noexcept
{
    if (elapsed_ms >= kMotionDurationMs) return 1.0f;
    const auto within = elapsed_ms % kMotionGroupMs;
    if (within < 2560u || within >= 2780u) return 1.0f;
    return within < 2670u ? 1.0f - motion_ease(within - 2560u, 110u)
                          : motion_ease(within - 2670u, 110u);
}

// Display ordinals follow the eye cue: two excursions per group, with a
// centring return between them and a longer final pause.
inline constexpr std::uint32_t motion_group(std::uint32_t elapsed_ms) noexcept
{
    return elapsed_ms >= kMotionDurationMs ? 0u : elapsed_ms / kMotionGroupMs + 1u;
}

inline constexpr std::uint32_t motion_step(std::uint32_t elapsed_ms) noexcept
{
    if (elapsed_ms >= kMotionDurationMs) return 0u;
    const auto within = motion_cue_index(elapsed_ms) % 4u;
    return within == 0u ? 1u : within == 2u ? 2u : 0u;
}

inline constexpr bool motion_is_center(std::uint32_t elapsed_ms) noexcept
{
    return motion_step(elapsed_ms) == 0u;
}

inline constexpr bool motion_is_pause(std::uint32_t elapsed_ms) noexcept
{
    return elapsed_ms < kMotionDurationMs && elapsed_ms % kMotionGroupMs >= 2440u;
}

} // namespace idle_program
