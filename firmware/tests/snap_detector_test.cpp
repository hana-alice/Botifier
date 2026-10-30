#include <apps/app_distance/snap_detector.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace snap_sense;

namespace {

using Block = std::array<std::int16_t, kBlockFrames>;
using Grid = std::array<std::uint8_t, kGridCells>;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

Block silence{};

Block impulse(int amplitude = 20000)
{
    Block block{};
    block[0] = static_cast<std::int16_t>(amplitude);
    block[1] = static_cast<std::int16_t>(-amplitude * 3 / 4);
    block[2] = static_cast<std::int16_t>(amplitude / 2);
    block[3] = static_cast<std::int16_t>(-amplitude / 3);
    return block;
}

Block noise(int amplitude, std::uint32_t seed = 123)
{
    Block block{};
    for (auto& sample : block) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        sample = static_cast<std::int16_t>(static_cast<int>(seed % (amplitude * 2 + 1)) - amplitude);
    }
    return block;
}

Block tone(double frequency)
{
    Block block{};
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<std::int16_t>(10000 * std::sin(i * 6.283185307179586 * frequency / kSampleRate));
    }
    return block;
}

void warm(AudioDetector& detector, std::uint32_t start = 0)
{
    for (std::uint32_t elapsed = 0; elapsed < 500; elapsed += 10) {
        const auto result = detector.process(silence.data(), silence.size(), 1, start + elapsed, false);
        check(result.reason == AudioReason::Warming && !result.candidate, "500 ms warm-up failed");
    }
    check(detector.process(silence.data(), silence.size(), 1, start + 500, false).reason == AudioReason::Quiet,
          "warm-up must finish exactly at 500 ms");
}

AudioFeatures burst(AudioDetector& detector, std::uint32_t start, unsigned blocks, const Block& source)
{
    for (unsigned i = 0; i < blocks; ++i) {
        check(!detector.process(source.data(), source.size(), 1, start + i * 10, false).candidate,
              "a still-active sound was accepted before its duration was known");
    }
    return detector.process(silence.data(), silence.size(), 1, start + blocks * 10, false);
}

void warmup_and_invalid_arguments()
{
    AudioDetector detector;
    check(detector.process(nullptr, 240, 1, 0, false).reason == AudioReason::Invalid, "null PCM was accepted");
    for (const auto frames : {std::size_t{0}, std::size_t{239}, std::size_t{241}, std::numeric_limits<std::size_t>::max()}) {
        check(detector.process(silence.data(), frames, 1, 0, false).reason == AudioReason::Invalid, "invalid frame count was accepted");
    }
    for (const auto channels : {std::size_t{0}, std::size_t{3}, std::numeric_limits<std::size_t>::max()}) {
        check(detector.process(silence.data(), 240, channels, 0, false).reason == AudioReason::Invalid, "invalid channel count was accepted");
    }
    warm(detector);
    const auto quiet = detector.process(silence.data(), 240, 1, 510, false);
    check(quiet.rms == 0 && quiet.peak == 0 && quiet.edge_ratio == 0 && quiet.crest == 0,
          "silence must have finite zero-valued features");
    check(quiet.noise >= 20 && !quiet.candidate, "noise floor was not bounded");
    const auto snap = impulse();
    detector.process(snap.data(), 240, 1, 600, false);
    check(detector.process(nullptr, 240, 1, 610, false).reason == AudioReason::Invalid, "invalid input did not reject active burst");
    check(!detector.process(silence.data(), 240, 1, 620, false).candidate, "invalid PCM left a pending burst");
    detector.reset();
    check(detector.process(silence.data(), 240, 1, 0, false).reason == AudioReason::Warming, "reset did not restore warm-up");
}

void broadband_bursts_and_duration_boundaries()
{
    const auto snap = impulse();
    for (const unsigned blocks : {1u, 2u, 6u}) {
        AudioDetector detector;
        warm(detector);
        const auto result = burst(detector, 600, blocks, snap);
        check(result.candidate && result.reason == AudioReason::Candidate, "short broadband burst was not a candidate");
        check(result.duration_ms == blocks * 10 && result.edge_ratio > 0.8f && result.crest >= 2.5f,
              "candidate features do not describe the completed burst");
        check(!detector.process(silence.data(), 240, 1, 700, false).candidate, "one burst yielded multiple candidates");
    }
    AudioDetector detector;
    warm(detector);
    const auto long_sound = burst(detector, 600, 7, snap);
    check(!long_sound.candidate && long_sound.reason == AudioReason::LongSound, "70 ms sound was not rejected");

    detector.reset();
    warm(detector);
    detector.process(snap.data(), 240, 1, 600, false);
    const auto missing_audio = detector.process(silence.data(), 240, 1, 670, false);
    check(missing_audio.reason == AudioReason::LongSound && !missing_audio.candidate,
          "a scheduling gap made an old transient eligible");
}

void sinusoid_dc_and_continuous_noise_are_rejected()
{
    for (const auto frequency : {300.0, 3000.0, 6000.0, 10000.0}) {
        AudioDetector detector;
        warm(detector);
        const auto result = burst(detector, 600, 3, tone(frequency));
        check(!result.candidate && result.reason == AudioReason::Tonal, "a short sinusoid was accepted");
    }
    AudioDetector detector;
    warm(detector);
    Block dc;
    dc.fill(12000);
    const auto offset = detector.process(dc.data(), 240, 1, 600, false);
    check(offset.rms == 0 && !offset.candidate, "DC offset was mistaken for sound energy");
    const auto ambient = noise(3000);
    for (unsigned i = 0; i < 300; ++i) {
        const auto result = detector.process(ambient.data(), 240, 1, 700 + i * 10, false);
        check(!result.candidate, "continuous noise generated a transient");
    }
    const auto learned = detector.process(ambient.data(), 240, 1, 3700, false);
    check(learned.noise > 400, "adaptive noise did not follow a stable background after warm-up");
    for (unsigned i = 0; i < 300; ++i) detector.process(silence.data(), 240, 1, 3710 + i * 10, false);
    check(detector.process(silence.data(), 240, 1, 6710, false).noise < learned.noise,
          "adaptive noise did not decay after the room became quiet");
}

void playback_tail_and_clipping()
{
    AudioDetector detector;
    warm(detector);
    const auto snap = impulse();
    check(detector.process(snap.data(), 240, 1, 600, true).reason == AudioReason::Playback, "speaker output was analyzed");
    check(detector.process(snap.data(), 240, 1, 800, false).reason == AudioReason::Playback, "inclusive 200 ms speaker tail was analyzed");
    detector.process(silence.data(), 240, 1, 801, false);
    check(burst(detector, 810, 1, snap).candidate, "microphone did not resume after playback tail");

    for (const auto level : {std::int16_t{32767}, std::numeric_limits<std::int16_t>::min()}) {
        Block clipped;
        clipped.fill(level);
        check(detector.process(clipped.data(), 240, 1, 2000, false).reason == AudioReason::Clipped,
              "persistent positive/negative saturation was accepted");
        check(!detector.process(silence.data(), 240, 1, 2010, false).candidate, "clipping created a delayed candidate");
    }
    Block partial{};
    for (std::size_t i = 0; i < 12; ++i) partial[i] = i % 2 ? -32768 : 32767;
    check(detector.process(partial.data(), 240, 1, 2100, false).reason == AudioReason::Clipped,
          "5 percent clipping boundary was not rejected");
}

void reference_isolation_sensitivity_and_uint32_rollover()
{
    AudioDetector detector;
    warm(detector);
    const auto snap = impulse();
    std::array<std::int16_t, kBlockFrames * 2> reference_only{};
    std::array<std::int16_t, kBlockFrames * 2> zero{};
    for (std::size_t i = 0; i < snap.size(); ++i) {
        reference_only[i * 2 + 1] = snap[i];
    }
    const auto reference = detector.process(reference_only.data(), 240, 2, 600, false);
    check(reference.peak == 0 && reference.rms == 0 && !reference.candidate,
          "speaker-reference channel contributed microphone features");
    check(!detector.process(zero.data(), 240, 2, 610, false).candidate, "reference-only sound triggered a candidate");

    AudioDetector mono;
    warm(mono);
    mono.process(silence.data(), 240, 1, 600, false);
    mono.process(silence.data(), 240, 1, 610, false);
    std::array<std::int16_t, kBlockFrames * 2> mic_and_reference{};
    for (std::size_t i = 0; i < snap.size(); ++i) {
        mic_and_reference[i * 2] = snap[i];
        mic_and_reference[i * 2 + 1] = i % 2 ? -32768 : 32767;
    }
    const auto same = [](const AudioFeatures& a, const AudioFeatures& b) {
        check(a.peak == b.peak && a.rms == b.rms && a.noise == b.noise && a.duration_ms == b.duration_ms &&
                  a.edge_ratio == b.edge_ratio && a.crest == b.crest && a.candidate == b.candidate && a.reason == b.reason,
              "same microphone PCM produced different mono/two-channel features");
    };
    same(mono.process(snap.data(), 240, 1, 620, false),
         detector.process(mic_and_reference.data(), 240, 2, 620, false));
    const auto mono_candidate = mono.process(silence.data(), 240, 1, 630, false);
    const auto pair_candidate = detector.process(zero.data(), 240, 2, 630, false);
    same(mono_candidate, pair_candidate);
    check(mono_candidate.candidate, "valid mic snap with clipped reference was lost");

    Block soft{};
    for (std::size_t i = 0; i < 8; ++i) soft[i] = i % 2 ? -2500 : 2500;
    for (const unsigned sensitivity : {0u, 1u, 5u, 99u}) {
        detector.setSensitivity(sensitivity);
        detector.reset();
        warm(detector);
        const auto result = burst(detector, 600, 1, soft);
        check(result.candidate == (sensitivity >= 5), "sensitivity was not clamped/preserved across reset");
    }
    detector.reset();
    constexpr auto start = std::numeric_limits<std::uint32_t>::max() - 250u;
    warm(detector, start);
    check(burst(detector, start + 600u, 1, snap).candidate, "audio clock wrap broke warm-up or duration");
    detector.reset();
    warm(detector, std::numeric_limits<std::uint32_t>::max() - 900u);
    detector.process(snap.data(), 240, 1, std::numeric_limits<std::uint32_t>::max() - 100u, true);
    check(detector.process(snap.data(), 240, 1, 99, false).reason == AudioReason::Playback,
          "playback tail failed across clock wrap");
}

void acquisition_gaps_clear_partial_bursts()
{
    const auto snap = impulse();
    AudioDetector detector;
    warm(detector);
    detector.process(snap.data(), 240, 1, 600, false);
    const auto after_gap = detector.process(silence.data(), 240, 1, 801, false);
    check(after_gap.reason == AudioReason::Quiet && !after_gap.candidate && after_gap.duration_ms == 0,
          "more-than-200-ms acquisition gap retained a partial burst");
    detector.process(snap.data(), 240, 1, 900, false);
    const auto new_onset = detector.process(snap.data(), 240, 1, 1101, false);
    check(new_onset.duration_ms == 10, "acquisition gap did not reset the previous RMS/onset gate");
    const auto candidate = detector.process(silence.data(), 240, 1, 1111, false);
    check(candidate.candidate && candidate.duration_ms == 10, "separate recordings were joined into one sound");

    detector.reset();
    warm(detector, std::numeric_limits<std::uint32_t>::max() - 500u);
    detector.process(snap.data(), 240, 1, 0, false);
    detector.process(snap.data(), 240, 1, 201, false);
    const auto wrapped = detector.process(silence.data(), 240, 1, 211, false);
    check(wrapped.candidate && wrapped.duration_ms == 10,
          "zero/wrapped last-block timestamp did not clear interrupted audio");
}

void motion_baseline_local_thresholds_and_retention()
{
    MotionDetector detector;
    Grid grid{};
    const auto initial = detector.process(grid, 0);
    check(initial.available && !initial.motion && !initial.global_change, "first camera frame was used as motion");
    grid[0] = grid[1] = 12;
    check(!detector.process(grid, 10).motion, "two changed cells reached the three-cell minimum");
    grid[2] = grid[3] = grid[4] = 11;
    check(!detector.process(grid, 20).motion, "sub-threshold deltas became motion");
    grid[5] = grid[6] = grid[7] = 12;
    auto visual = detector.process(grid, 30);
    check(visual.motion && visual.motion_ms == 30 && visual.changed_cells == 3, "three cells at delta 12 were not motion");
    check(detector.process(grid, 630).motion, "motion should be retained through 600 ms");
    check(!detector.process(grid, 631).motion, "motion was retained longer than 600 ms");
    detector.reset();
    check(!detector.process(grid, 1000).motion, "reset retained a previous image as a new event");

    detector.reset();
    grid.fill(0);
    detector.process(grid, std::numeric_limits<std::uint32_t>::max() - 20u);
    grid[1] = grid[2] = grid[3] = 20;
    check(detector.process(grid, 0).motion, "timestamp zero was treated as missing motion");
    check(detector.process(grid, 600).motion && !detector.process(grid, 601).motion,
          "visual motion retention failed across wrap");
}

void global_changes_and_brightness_compensation()
{
    MotionDetector detector;
    Grid grid{};
    grid.fill(100);
    detector.process(grid, 0);
    grid.fill(105);
    auto visual = detector.process(grid, 10);
    check(!visual.motion && visual.changed_cells == 0 && visual.mean_delta == 0,
          "uniform brightness shift was not compensated");
    grid[0] = grid[1] = grid[2] = 125;
    visual = detector.process(grid, 20);
    check(visual.motion && !visual.global_change, "local motion after exposure drift was discarded");
    grid.fill(180);
    visual = detector.process(grid, 30);
    check(visual.global_change && !visual.motion, "whole-frame lighting change corroborated audio");
    check(!detector.process(grid, 40).motion, "global frame left retained local evidence");

    detector.reset();
    grid.fill(0);
    detector.process(grid, 0);
    for (std::size_t i = 0; i < 460; ++i) grid[i] = 100;
    visual = detector.process(grid, 10);
    check(visual.motion && !visual.global_change, "less-than-60-percent local frame was marked global");
    detector.reset();
    grid.fill(0);
    detector.process(grid, 0);
    for (std::size_t i = 0; i < 461; ++i) grid[i] = 100;
    visual = detector.process(grid, 10);
    check(visual.global_change && !visual.motion, "60-percent global-change threshold was not enforced");
}

VisualEvidence evidence(std::uint32_t captured, std::uint32_t motion)
{
    VisualEvidence visual;
    visual.available = visual.motion = true;
    visual.captured_ms = captured;
    visual.motion_ms = motion;
    visual.changed_cells = 5;
    return visual;
}

void fusion_camera_optional_cooldown_and_blocking()
{
    Fusion fusion;
    check(fusion.update(0, false, {}, false) == Decision::None, "fusion accepted without audio");
    check(fusion.offer(0) == Decision::None, "first offer at timestamp zero was rejected");
    check(fusion.update(0, false, {}, false) == Decision::Accepted, "camera-optional mode did not accept immediately");
    check(fusion.offer(999) == Decision::Cooldown, "1000 ms cooldown ended early");
    check(fusion.update(999, false, {}, false) == Decision::None, "cooldown offer left a pending event");
    check(fusion.offer(1000) == Decision::None, "1000 ms cooldown did not end at its boundary");
    check(fusion.update(1000, false, {}, true) == Decision::Suppressed, "blocked app did not suppress a pending snap");
    check(fusion.update(1001, false, {}, false) == Decision::None, "blocked event survived to another frame");
    fusion.offer(1001);
    check(fusion.update(1001, false, {}, false) == Decision::Accepted, "suppressed event incorrectly started cooldown");
    fusion.reset();
    fusion.offer(0);
    check(fusion.update(0, true, evidence(0, 0), false) == Decision::Accepted, "zero-valued visual timestamp was missing");
}

void fusion_motion_window_and_pending_deadline()
{
    for (const auto before : {0u, 600u}) {
        Fusion fusion;
        fusion.offer(1000);
        check(fusion.update(1000, true, evidence(1000, 1000 - before), false) == Decision::Accepted,
              "inclusive pre-snap motion window failed");
    }
    for (const auto after : {1u, 350u}) {
        Fusion fusion;
        fusion.offer(1000);
        check(fusion.update(1000, true, {}, false) == Decision::None, "fusion did not wait for a camera frame");
        check(fusion.update(1000 + after, true, evidence(1000 + after, 1000 + after), false) == Decision::Accepted,
              "inclusive post-snap motion window failed");
    }
    Fusion fusion;
    fusion.offer(1000);
    check(fusion.update(1000, true, evidence(1000, 399), false) == Decision::None, "601 ms old motion was accepted");
    check(fusion.offer(1200) == Decision::Cooldown, "duplicate pending audio did not receive a rejection verdict");
    check(fusion.update(1349, true, {}, false) == Decision::None, "pending deadline ended before 350 ms");
    check(fusion.update(1350, true, {}, false) == Decision::NoVisual, "duplicate audio extended the first pending deadline");
    check(fusion.update(1351, true, evidence(1351, 1351), false) == Decision::None, "expired audio accepted later motion");
    fusion.offer(2000);
    check(fusion.update(2351, true, evidence(2351, 2351), false) == Decision::NoVisual,
          "motion more than 350 ms after audio was accepted");

    fusion.reset();
    unsigned candidates = 0;
    unsigned rejected = 0;
    unsigned accepted = 0;
    ++candidates;
    check(fusion.offer(3000) == Decision::None, "first accounted candidate was rejected");
    ++candidates;
    if (fusion.offer(3200) == Decision::Cooldown) ++rejected;
    const auto final = fusion.update(3350, true, {}, false);
    if (final == Decision::NoVisual) ++rejected;
    if (final == Decision::Accepted) ++accepted;
    check(final == Decision::NoVisual && candidates == accepted + rejected && rejected == 2,
          "pending duplicate lost a candidate verdict or extended the original 350 ms deadline");
}

void fusion_freshness_global_unavailable_and_future_frames()
{
    Fusion fusion;
    fusion.offer(1000);
    check(fusion.update(1000, true, evidence(500, 500), false) == Decision::Accepted,
          "500 ms fresh frame boundary was rejected");
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        fusion.reset();
        fusion.offer(1000);
        auto visual = evidence(1000, 1000);
        if (scenario == 0) visual.available = false;
        if (scenario == 1) visual.motion = false;
        if (scenario == 2) visual.global_change = true;
        if (scenario == 3) visual.captured_ms = 499;
        if (scenario == 4) visual.captured_ms = visual.motion_ms = 1001;
        check(fusion.update(1000, true, visual, false) == Decision::None, "unusable visual evidence was accepted");
        check(fusion.update(1350, true, {}, false) == Decision::NoVisual, "missing evidence did not end pending audio");
    }
    fusion.reset();
    fusion.offer(1000);
    auto future_motion = evidence(1000, 1001);
    check(fusion.update(1000, true, future_motion, false) == Decision::None, "future motion timestamp was accepted");
}

void fusion_rollover_and_cooldown_at_zero()
{
    Fusion fusion;
    constexpr auto start = std::numeric_limits<std::uint32_t>::max() - 99u;
    fusion.offer(start);
    check(fusion.update(0, true, evidence(0, 0), false) == Decision::Accepted,
          "fusion window failed across timestamp wrap");
    check(fusion.offer(999) == Decision::Cooldown && fusion.offer(1000) == Decision::None,
          "accepted timestamp zero broke cooldown");
    check(fusion.update(1000, false, {}, false) == Decision::Accepted, "cooldown equality boundary failed after wrap");
    fusion.reset();
    fusion.offer(start);
    check(fusion.update(249, true, {}, false) == Decision::None, "wrapped deadline ended early");
    check(fusion.update(250, true, {}, false) == Decision::NoVisual, "wrapped deadline did not expire");
}

}  // namespace

int main()
{
    const std::array tests{
        warmup_and_invalid_arguments,
        broadband_bursts_and_duration_boundaries,
        sinusoid_dc_and_continuous_noise_are_rejected,
        playback_tail_and_clipping,
        reference_isolation_sensitivity_and_uint32_rollover,
        acquisition_gaps_clear_partial_bursts,
        motion_baseline_local_thresholds_and_retention,
        global_changes_and_brightness_compensation,
        fusion_camera_optional_cooldown_and_blocking,
        fusion_motion_window_and_pending_deadline,
        fusion_freshness_global_unavailable_and_future_frames,
        fusion_rollover_and_cooldown_at_zero,
    };
    try {
        for (const auto test : tests) test();
    } catch (const std::exception& error) {
        std::cerr << "snap_detector_test: " << error.what() << '\n';
        return 1;
    }
    std::cout << "snap_detector_test: " << tests.size()
              << " groups passed (synthetic PCM/grids only; real-world accuracy is unmeasured)\n";
    return 0;
}
