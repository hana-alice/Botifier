#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace distance_sensor {

struct ProximityConfig {
    std::uint16_t threshold_mm = 400;
    std::uint16_t hysteresis_mm = 80;
    std::uint32_t near_duration_ms = 1000;
    std::uint32_t sample_timeout_ms = 1500;
};

enum class ProximityState { Waiting, Safe, Near, Alert };

// Call feed only for fresh measurements. Reading a snapshot or calling expire
// does not extend the sample history or advance the proximity dwell time.
class ProximityPolicy {
public:
    explicit ProximityPolicy(ProximityConfig config = {}) noexcept : config_(config) {}

    void feed(bool valid, std::uint16_t distance_mm, std::uint32_t now_ms) noexcept
    {
        if (!valid || distance_mm < 40 || distance_mm > 4000) {
            invalidate();
            return;
        }
        expire(now_ms);
        last_sample_ms_ = now_ms;
        samples_[next_sample_] = distance_mm;
        next_sample_ = (next_sample_ + 1) % samples_.size();
        if (sample_count_ < samples_.size()) {
            ++sample_count_;
        }
        if (sample_count_ < samples_.size()) {
            return;
        }

        auto sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        filtered_mm_ = sorted[1];
        if (state_ == ProximityState::Near || state_ == ProximityState::Alert) {
            const auto restore_mm = std::uint32_t{config_.threshold_mm} + config_.hysteresis_mm;
            if (static_cast<std::uint32_t>(filtered_mm_) >= restore_mm) {
                state_ = ProximityState::Safe;
                near_elapsed_ms_ = 0;
                return;
            }
        } else if (filtered_mm_ < config_.threshold_mm) {
            state_ = ProximityState::Near;
            near_since_ms_ = now_ms;
        } else {
            // Without a previous near latch, the initial dead band is safe.
            state_ = ProximityState::Safe;
            return;
        }

        near_elapsed_ms_ = now_ms - near_since_ms_;
        if (near_elapsed_ms_ >= config_.near_duration_ms) {
            state_ = ProximityState::Alert;
        }
    }

    void invalidate() noexcept
    {
        sample_count_ = 0;
        next_sample_ = 0;
        filtered_mm_ = -1;
        near_elapsed_ms_ = 0;
        state_ = ProximityState::Waiting;
    }

    void expire(std::uint32_t now_ms) noexcept
    {
        // Unsigned subtraction also covers a single uint32 uptime rollover.
        if (sample_count_ != 0 && now_ms - last_sample_ms_ > config_.sample_timeout_ms) {
            invalidate();
        }
    }

    ProximityState state() const noexcept { return state_; }
    bool alert_active() const noexcept { return state_ == ProximityState::Alert; }
    int filtered_mm() const noexcept { return filtered_mm_; }
    std::uint32_t near_elapsed_ms() const noexcept { return near_elapsed_ms_; }

private:
    ProximityConfig config_;
    std::array<std::uint16_t, 3> samples_{};
    std::size_t sample_count_ = 0;
    std::size_t next_sample_ = 0;
    ProximityState state_ = ProximityState::Waiting;
    int filtered_mm_ = -1;
    std::uint32_t last_sample_ms_ = 0;
    std::uint32_t near_since_ms_ = 0;
    std::uint32_t near_elapsed_ms_ = 0;
};

} // namespace distance_sensor
