#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace snap_sense {

inline constexpr std::size_t kBlockFrames = 240;
inline constexpr std::uint32_t kSampleRate = 24000;
inline constexpr std::size_t kGridWidth = 32;
inline constexpr std::size_t kGridHeight = 24;
inline constexpr std::size_t kGridCells = kGridWidth * kGridHeight;

enum class AudioReason { Warming, Quiet, Candidate, LongSound, Tonal, Playback, Clipped, Invalid };

struct AudioFeatures {
    std::uint32_t peak = 0;
    std::uint32_t rms = 0;
    std::uint32_t noise = 0;
    std::uint32_t duration_ms = 0;
    float edge_ratio = 0;
    float crest = 0;
    bool candidate = false;
    AudioReason reason = AudioReason::Quiet;
};

// A bounded transient heuristic, not a trained finger-snap classifier. Input is
// interleaved PCM: exactly 10 ms at 24 kHz. With two channels the hardware
// supplies [microphone, speaker reference]; only channel zero is analyzed.
class AudioDetector {
public:
    void reset() noexcept
    {
        const auto sensitivity = sensitivity_;
        *this = AudioDetector{};
        sensitivity_ = sensitivity;
    }

    void setSensitivity(unsigned value) noexcept
    {
        sensitivity_ = std::max(1u, std::min(value, 5u));
    }

    AudioFeatures process(const std::int16_t* samples, std::size_t frames, std::size_t channels,
                          std::uint32_t now_ms, bool playback) noexcept
    {
        AudioFeatures result;
        result.noise = noise_value();
        if (!samples || frames != kBlockFrames || (channels != 1 && channels != 2)) {
            clear_burst();
            previous_rms_ = 0;
            result.reason = AudioReason::Invalid;
            return result;
        }

        if (have_last_block_ && now_ms - last_block_ms_ > 200u) {
            clear_burst();
            previous_rms_ = 0;
        }
        have_last_block_ = true;
        last_block_ms_ = now_ms;

        std::int64_t sum = 0;
        for (std::size_t frame = 0; frame < frames; ++frame) sum += samples[frame * channels];
        const auto mean = static_cast<std::int32_t>(sum / static_cast<std::int64_t>(frames));
        std::uint64_t energy = 0;
        std::uint64_t edge_energy = 0;
        std::size_t clipped = 0;
        std::int32_t previous = 0;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto raw = static_cast<std::int32_t>(samples[frame * channels]);
            if (raw >= 32700 || raw <= -32700) ++clipped;
            const auto value = raw - mean;
            const auto magnitude = static_cast<std::uint32_t>(value < 0 ? -value : value);
            result.peak = std::max(result.peak, magnitude);
            energy += static_cast<std::uint64_t>(static_cast<std::int64_t>(value) * value);
            if (frame) {
                const auto edge = static_cast<std::int64_t>(value) - previous;
                edge_energy += static_cast<std::uint64_t>(edge * edge);
            }
            previous = value;
        }
        const auto count = static_cast<std::uint32_t>(frames);
        result.rms = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(energy) / count));
        result.edge_ratio = energy ? static_cast<float>(std::sqrt(static_cast<double>(edge_energy) / energy)) : 0;
        result.crest = result.rms ? static_cast<float>(result.peak) / result.rms : 0;

        if (!initialized_) {
            initialized_ = true;
            started_ms_ = now_ms;
        }
        if (playback) {
            playback_seen_ = true;
            playback_ms_ = now_ms;
        }
        // Do not learn the speaker output as ambient noise. The tail limit is
        // inclusive: the first eligible block is more than 200 ms after output.
        if (playback || (playback_seen_ && now_ms - playback_ms_ <= 200u)) {
            clear_burst();
            previous_rms_ = result.rms;
            result.reason = AudioReason::Playback;
            return result;
        }
        if (clipped * 20 >= frames) {
            clear_burst();
            previous_rms_ = result.rms;
            result.reason = AudioReason::Clipped;
            return result;
        }
        if (now_ms - started_ms_ < 500u) {
            adapt_noise(result.rms, 0.08f);
            previous_rms_ = result.rms;
            result.noise = noise_value();
            result.reason = AudioReason::Warming;
            return result;
        }

        static constexpr std::array<float, 5> ratios{8, 7, 6, 5, 4};
        static constexpr std::array<std::uint32_t, 5> minimum_rms{600, 450, 320, 220, 140};
        static constexpr std::array<std::uint32_t, 5> minimum_peak{3000, 2600, 2200, 1800, 1300};
        const auto index = sensitivity_ - 1;
        const auto onset = std::max(static_cast<float>(minimum_rms[index]), noise_ * ratios[index]);
        const auto tail = std::max(static_cast<float>(minimum_rms[index]) * 0.35f, noise_ * 2.0f);
        const auto previous_rms = previous_rms_;
        previous_rms_ = result.rms;

        if (in_burst_) {
            if (result.rms >= tail) {
                accumulate(energy, edge_energy, count, result.peak, now_ms);
                result.duration_ms = burst_duration_ms_;
                result.reason = burst_duration_ms_ > 60u ? AudioReason::LongSound : AudioReason::Quiet;
                if (result.reason == AudioReason::LongSound) {
                    adapt_noise(result.rms, 0.01f);
                    result.noise = noise_value();
                }
                return result;
            }

            result.peak = burst_peak_;
            result.rms = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(burst_energy_) / burst_samples_));
            result.edge_ratio = burst_energy_ ? static_cast<float>(std::sqrt(static_cast<double>(burst_edges_) / burst_energy_)) : 0;
            result.crest = result.rms ? static_cast<float>(burst_peak_) / result.rms : 0;
            result.duration_ms = std::max(burst_duration_ms_, now_ms - burst_started_ms_);
            const bool broadband = result.edge_ratio >= 0.8f && result.crest >= 2.5f;
            // Short, high-crest decays can have less adjacent-sample edge energy.
            const bool short_transient = result.duration_ms <= 30u && result.edge_ratio >= 0.45f && result.crest >= 3.0f;
            if (result.duration_ms > 60u) {
                result.reason = AudioReason::LongSound;
            } else if (!broadband && !short_transient) {
                result.reason = AudioReason::Tonal;
            } else if (result.peak >= minimum_peak[index] && result.rms >= minimum_rms[index]) {
                result.candidate = true;
                result.reason = AudioReason::Candidate;
            }
            clear_burst();
            return result;
        }

        if (result.rms >= onset && result.peak >= minimum_peak[index] &&
            result.rms >= std::max(1.0f, static_cast<float>(previous_rms)) * 2.2f) {
            in_burst_ = true;
            burst_started_ms_ = now_ms;
            accumulate(energy, edge_energy, count, result.peak, now_ms);
            result.duration_ms = burst_duration_ms_;
            return result;
        }
        if (result.rms < std::max(noise_ * 1.8f, static_cast<float>(minimum_rms[index]))) {
            adapt_noise(result.rms, result.rms < noise_ ? 0.04f : 0.01f);
            result.noise = noise_value();
        } else if (result.rms < std::max(1.0f, static_cast<float>(previous_rms)) * 1.2f) {
            // A stable background that rose after warm-up must eventually be
            // learned as well. A sudden onset takes the burst branch first.
            adapt_noise(result.rms, 0.002f);
            result.noise = noise_value();
        }
        return result;
    }

private:
    void clear_burst() noexcept
    {
        in_burst_ = false;
        burst_energy_ = burst_edges_ = 0;
        burst_samples_ = burst_blocks_ = burst_peak_ = burst_duration_ms_ = 0;
    }

    void accumulate(std::uint64_t energy, std::uint64_t edges, std::uint32_t count,
                    std::uint32_t peak, std::uint32_t now_ms) noexcept
    {
        // Stop accumulating a rejected continuous sound. This bounds storage
        // even if a microphone remains loud for hours.
        if (burst_duration_ms_ > 60u) return;
        burst_energy_ += energy;
        burst_edges_ += edges;
        burst_samples_ += count;
        ++burst_blocks_;
        burst_peak_ = std::max(burst_peak_, peak);
        burst_duration_ms_ = std::max(burst_blocks_ * 10u, now_ms - burst_started_ms_ + 10u);
    }

    void adapt_noise(std::uint32_t rms, float rate) noexcept
    {
        noise_ = std::max(20.0f, noise_ + (static_cast<float>(rms) - noise_) * rate);
    }

    std::uint32_t noise_value() const noexcept { return static_cast<std::uint32_t>(noise_); }

    unsigned sensitivity_ = 3;
    bool initialized_ = false;
    bool have_last_block_ = false;
    bool playback_seen_ = false;
    bool in_burst_ = false;
    std::uint32_t started_ms_ = 0;
    std::uint32_t last_block_ms_ = 0;
    std::uint32_t playback_ms_ = 0;
    std::uint32_t previous_rms_ = 0;
    std::uint32_t burst_started_ms_ = 0;
    std::uint32_t burst_samples_ = 0;
    std::uint32_t burst_blocks_ = 0;
    std::uint32_t burst_peak_ = 0;
    std::uint32_t burst_duration_ms_ = 0;
    std::uint64_t burst_energy_ = 0;
    std::uint64_t burst_edges_ = 0;
    float noise_ = 80;
};

struct VisualEvidence {
    bool available = false;
    bool motion = false;
    bool global_change = false;
    std::uint32_t captured_ms = 0;
    std::uint32_t motion_ms = 0;
    std::uint16_t changed_cells = 0;
    std::uint16_t mean_delta = 0;
};

class MotionDetector {
public:
    void reset() noexcept { *this = MotionDetector{}; }

    VisualEvidence process(const std::array<std::uint8_t, kGridCells>& grid, std::uint32_t now_ms) noexcept
    {
        VisualEvidence result;
        result.available = true;
        result.captured_ms = now_ms;
        if (!initialized_) {
            previous_ = grid;
            initialized_ = true;
            return result;
        }

        // A fixed histogram gives a robust common brightness shift without
        // allocations or sorting. Uniform exposure flicker leaves no local
        // residual; broad raw changes remain ineligible as corroboration.
        std::array<std::uint16_t, 511> differences{};
        std::size_t raw_changed = 0;
        for (std::size_t i = 0; i < kGridCells; ++i) {
            const auto delta = static_cast<int>(grid[i]) - previous_[i];
            ++differences[static_cast<std::size_t>(delta + 255)];
            if (std::abs(delta) >= 12) ++raw_changed;
        }
        std::size_t cumulative = 0;
        int common_shift = 0;
        for (std::size_t i = 0; i < differences.size(); ++i) {
            cumulative += differences[i];
            if (cumulative > kGridCells / 2) {
                common_shift = static_cast<int>(i) - 255;
                break;
            }
        }
        std::uint32_t total_delta = 0;
        for (std::size_t i = 0; i < kGridCells; ++i) {
            const auto delta = std::abs(static_cast<int>(grid[i]) - previous_[i] - common_shift);
            total_delta += static_cast<std::uint32_t>(delta);
            if (delta >= 12) ++result.changed_cells;
        }
        result.mean_delta = static_cast<std::uint16_t>(total_delta / kGridCells);
        result.global_change = raw_changed * 100 >= kGridCells * 60 ||
                               static_cast<std::size_t>(result.changed_cells) * 100 >= kGridCells * 60;
        previous_ = grid;
        if (result.global_change) {
            has_motion_ = false;
        } else if (result.changed_cells >= 3) {
            has_motion_ = true;
            motion_ms_ = now_ms;
        }
        result.motion = has_motion_ && now_ms - motion_ms_ <= 600u;
        result.motion_ms = motion_ms_;
        return result;
    }

private:
    std::array<std::uint8_t, kGridCells> previous_{};
    bool initialized_ = false;
    bool has_motion_ = false;
    std::uint32_t motion_ms_ = 0;
};

enum class Decision { None, Accepted, Suppressed, Cooldown, NoVisual };

class Fusion {
public:
    void reset() noexcept { *this = Fusion{}; }

    Decision offer(std::uint32_t now_ms) noexcept
    {
        if (has_accepted_ && now_ms - accepted_ms_ < 1000u) return Decision::Cooldown;
        // Each offered candidate must get a verdict. Reject another onset
        // while the first awaits vision, without extending its deadline.
        if (pending_) return Decision::Cooldown;
        pending_ = true;
        offered_ms_ = now_ms;
        return Decision::None;
    }

    Decision update(std::uint32_t now_ms, bool camera_required, const VisualEvidence& visual, bool blocked) noexcept
    {
        if (!pending_) return Decision::None;
        if (blocked) {
            pending_ = false;
            return Decision::Suppressed;
        }
        if (!camera_required) return accept(now_ms);
        const auto since_offer = now_ms - offered_ms_;
        // Unsigned differences are wrap-safe for these short windows. The
        // extra age check rejects future motion timestamps and stale history.
        const bool nearby_motion = offered_ms_ - visual.motion_ms <= 600u ||
                                   visual.motion_ms - offered_ms_ <= 350u;
        const bool fresh = visual.available && now_ms - visual.captured_ms <= 500u &&
                           now_ms - visual.motion_ms <= 950u &&
                           visual.captured_ms - visual.motion_ms <= 950u;
        if (since_offer <= 350u && fresh && visual.motion && !visual.global_change && nearby_motion) {
            return accept(now_ms);
        }
        if (since_offer >= 350u) {
            pending_ = false;
            return Decision::NoVisual;
        }
        return Decision::None;
    }

private:
    Decision accept(std::uint32_t now_ms) noexcept
    {
        pending_ = false;
        has_accepted_ = true;
        accepted_ms_ = now_ms;
        return Decision::Accepted;
    }

    bool pending_ = false;
    bool has_accepted_ = false;
    std::uint32_t offered_ms_ = 0;
    std::uint32_t accepted_ms_ = 0;
};

}  // namespace snap_sense
