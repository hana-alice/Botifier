#pragma once
#include <cstdint>

namespace idle_program {

enum class Program { Pet, EyeRest, Weekly, RestDone, EyeCountdown };
enum class EyePhase { Intro, Motion, Rest };
enum class WeeklyStatus { Waiting, Fresh, Stale, Unavailable };

struct Config {
    bool enabled = true;
    std::uint32_t eye_interval_ms = 20u * 60u * 1000u;
    std::uint32_t rest_duration_ms = 20000;
    std::uint32_t idle_grace_ms = 10000;
    std::uint32_t pet_gap_ms = 3000;
    std::uint32_t weekly_duration_ms = 12000;
    std::uint32_t weekly_interval_ms = 30000;
};

struct Context {
    bool surface_available = false;
    bool foreground_busy = false;
    bool distance_alert = false;
};

struct Snapshot {
    bool active = false;
    bool blocked = true;
    Config config;
    Program program = Program::Pet;
    std::uint32_t generation = 0;
    EyePhase eye_phase = EyePhase::Intro;
    std::uint32_t phase_elapsed_ms = 0;
    std::uint32_t remaining_ms = 0;
    std::uint32_t eye_remaining_ms = 0;
    std::uint32_t completed_rests = 0;
    WeeklyStatus weekly_status = WeeklyStatus::Waiting;
    int weekly_left = -1;
    std::uint32_t weekly_reset_at = 0;
    std::uint32_t weekly_observed_at = 0;
    std::uint32_t weekly_age_ms = 0;
};

} // namespace idle_program
