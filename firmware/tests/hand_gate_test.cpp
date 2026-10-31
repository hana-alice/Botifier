#include <apps/app_distance/hand_gate.h>
#include <apps/app_distance/snap_detector.h>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace snap_sense;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void clear(HandGate& gate, std::uint32_t start = 0)
{
    gate.observe(start, false);
    gate.observe(start + 250u, false);
    const auto state = gate.snapshot(start + 250u);
    check(state.available && state.phase == HandPhase::Looking && !state.armed,
          "two clear frames spanning 250 ms must reach Looking");
}

void arm(HandGate& gate, std::uint32_t start = 0)
{
    clear(gate, start);
    gate.observe(start + 300u, true);
    gate.observe(start + 400u, true);
    check(gate.snapshot(start + 400u).armed, "two eligible frames spanning 100 ms must arm");
}

void interaction_region_requires_a_large_upper_central_hand()
{
    check(is_interaction_hand({true, 0.8f, 80, 50, 160, 130}, 240, 240),
          "a large central raised hand was rejected");
    check(!is_interaction_hand({true, 0.8f, 80, 160, 160, 239}, 240, 240),
          "a lower typing-position hand passed the interaction region");
    check(!is_interaction_hand({true, 0.8f, 110, 95, 130, 145}, 240, 240),
          "a small distant hand passed the minimum extents");
    check(!is_interaction_hand({true, 0.8f, 80, 70, 160, 129}, 240, 240),
          "a central but short hand passed the minimum height");
    check(!is_interaction_hand({true, 0.8f, 0, 50, 60, 130}, 240, 240),
          "a left-side hand passed the interaction region");
    check(!is_interaction_hand({true, 0.8f, 180, 50, 239, 130}, 240, 240),
          "a right-side hand passed the interaction region");
    check(!is_interaction_hand({true, 0.8f, 80, 0, 160, 59}, 240, 240),
          "a short top-edge hand passed the interaction region");
    check(is_interaction_hand({true, 0.8f, 1200, 1000, 2800, 2400}, 4096, 4096),
          "maximum supported frame dimensions were rejected");
}

void interaction_region_enforces_fractional_and_center_boundaries()
{
    check(is_interaction_hand({true, 0.5f, 100, 50, 140, 110}, 240, 240),
          "exact score and extent boundaries were rejected");
    check(!is_interaction_hand({true, 0.499f, 100, 50, 140, 110}, 240, 240),
          "score below 0.5 passed");
    check(!is_interaction_hand({true, 0.8f, 100, 50, 140, 111}, 241, 241),
          "fractional minimum width was rounded down");
    check(!is_interaction_hand({true, 0.8f, 100, 50, 141, 110}, 241, 241),
          "fractional minimum height was rounded down");
    check(is_interaction_hand({true, 0.8f, 100, 50, 141, 111}, 241, 241),
          "rounded-up fractional minimum extents were rejected");

    check(is_interaction_hand({true, 0.8f, 35, 50, 85, 110}, 300, 240),
          "center at the left interaction boundary was rejected");
    check(!is_interaction_hand({true, 0.8f, 34, 50, 85, 110}, 300, 240),
          "half-pixel center left of the boundary was rounded into the region");
    check(is_interaction_hand({true, 0.8f, 215, 50, 265, 110}, 300, 240),
          "center at the right interaction boundary was rejected");
    check(!is_interaction_hand({true, 0.8f, 215, 50, 266, 110}, 300, 240),
          "half-pixel center right of the boundary was rounded into the region");
    // A valid box meeting H/4 minimum height already has centerY >= H/8.
    check(is_interaction_hand({true, 0.8f, 80, 0, 160, 60}, 240, 240),
          "center at the upper interaction boundary was rejected");
    check(is_interaction_hand({true, 0.8f, 80, 130, 160, 190}, 240, 240),
          "center at the lower interaction boundary was rejected");
    check(!is_interaction_hand({true, 0.8f, 80, 130, 160, 191}, 240, 240),
          "half-pixel center below the boundary was rounded into the region");
}

void interaction_region_rejects_invalid_boxes_scores_and_dimensions()
{
    const HandBox good{true, 0.8f, 80, 50, 160, 130};
    const std::array invalid_boxes{
        HandBox{},
        HandBox{false, 0.8f, 80, 50, 160, 130},
        HandBox{true, 0.8f, -1, 50, 160, 130},
        HandBox{true, 0.8f, 80, -1, 160, 130},
        HandBox{true, 0.8f, 80, 50, 240, 130},
        HandBox{true, 0.8f, 80, 50, 160, 240},
        HandBox{true, 0.8f, 80, 50, 80, 130},
        HandBox{true, 0.8f, 80, 50, 160, 50},
        HandBox{true, 0.8f, 160, 50, 80, 130},
        HandBox{true, 0.8f, 80, 130, 160, 50},
        HandBox{true, 0.8f, std::numeric_limits<int>::min(), 50, std::numeric_limits<int>::max(), 130},
        HandBox{true, 0.8f, 80, std::numeric_limits<int>::min(), 160, std::numeric_limits<int>::max()},
        HandBox{true, std::numeric_limits<float>::quiet_NaN(), 80, 50, 160, 130},
        HandBox{true, std::numeric_limits<float>::infinity(), 80, 50, 160, 130},
        HandBox{true, -std::numeric_limits<float>::infinity(), 80, 50, 160, 130},
    };
    for (const auto& box : invalid_boxes) {
        check(!is_interaction_hand(box, 240, 240), "invalid hand box or non-finite score passed");
    }
    for (const auto invalid_dimension : {std::size_t{0}, std::size_t{4097}, std::numeric_limits<std::size_t>::max()}) {
        check(!is_interaction_hand(good, invalid_dimension, 240), "invalid frame width passed");
        check(!is_interaction_hand(good, 240, invalid_dimension), "invalid frame height passed");
    }
    check(!is_interaction_hand(good, 1, 240) && !is_interaction_hand(good, 240, 1),
          "box outside a one-pixel frame passed");
}

void hand_box_eligibility_drives_new_presentation()
{
    HandGate gate;
    const HandBox typing{true, 0.9f, 80, 160, 160, 239};
    const HandBox raised{true, 0.9f, 80, 50, 160, 130};
    gate.observe(0, is_interaction_hand(typing, 240, 240));
    gate.observe(250, is_interaction_hand(typing, 240, 240));
    check(gate.snapshot(250).phase == HandPhase::Looking && !gate.snapshot(250).armed,
          "out-of-region hand armed rather than supplying clear-region frames");
    gate.observe(300, is_interaction_hand(raised, 240, 240));
    gate.observe(400, is_interaction_hand(raised, 240, 240));
    check(gate.snapshot(400).armed, "raising a hand into the interaction region did not arm");
    gate.consume();
    gate.observe(500, is_interaction_hand(raised, 240, 240));
    gate.observe(600, is_interaction_hand(raised, 240, 240));
    check(!gate.snapshot(600).armed, "an accepted raised hand supplied a second permission without leaving");
}

void startup_and_reset_require_clear()
{
    HandGate gate;
    const auto initial = gate.snapshot(0);
    check(initial.phase == HandPhase::NeedClear && !initial.available && !initial.armed && initial.captured_ms == 0,
          "initial state must be unavailable and require clear");
    for (std::uint32_t now = 0; now <= 2000; now += 100) {
        gate.observe(now, true);
        const auto state = gate.snapshot(now);
        check(state.available && state.phase == HandPhase::NeedClear && !state.armed,
              "startup presence armed without first leaving the region");
    }
    gate.reset();
    arm(gate);
    gate.reset();
    const auto reset = gate.snapshot(400);
    check(!reset.available && !reset.armed && reset.phase == HandPhase::NeedClear && reset.captured_ms == 0,
          "reset retained a capture or permission");
}

void clear_requires_observed_frames_and_elapsed_time()
{
    HandGate gate;
    gate.observe(0, false);
    check(gate.snapshot(250).phase == HandPhase::NeedClear, "one clear frame advanced on wall time alone");
    gate.observe(249, false);
    check(gate.snapshot(249).phase == HandPhase::NeedClear, "clear interval ended before 250 ms");
    gate.observe(250, false);
    check(gate.snapshot(250).phase == HandPhase::Looking, "clear interval did not end at 250 ms");

    gate.reset();
    gate.observe(0, false);
    gate.observe(200, true);
    gate.observe(250, false);
    gate.observe(499, false);
    check(gate.snapshot(499).phase == HandPhase::NeedClear, "presence did not restart the clear interval");
    gate.observe(500, false);
    check(gate.snapshot(500).phase == HandPhase::Looking, "restarted clear interval did not complete");
}

void stabilization_requires_a_second_frame_and_100_ms()
{
    HandGate gate;
    clear(gate);
    gate.observe(300, true);
    check(gate.snapshot(400).phase == HandPhase::Stabilizing && !gate.snapshot(400).armed,
          "one eligible frame armed on wall time alone");
    gate.observe(399, true);
    check(!gate.snapshot(399).armed, "99 ms of eligible presence armed early");
    gate.observe(400, true);
    check(gate.snapshot(400).armed && gate.snapshot(400).phase == HandPhase::Armed,
          "stable interval did not arm at 100 ms");

    gate.reset();
    arm(gate);
    check(gate.snapshot(400).captured_ms == 400, "snapshot did not preserve the capture timestamp");
}

void ineligible_frame_interrupts_stabilization()
{
    HandGate gate;
    clear(gate);
    gate.observe(300, true);
    gate.observe(350, false);
    check(gate.snapshot(350).phase == HandPhase::Looking, "lost eligibility did not cancel stabilization");
    gate.observe(400, true);
    gate.observe(499, true);
    check(!gate.snapshot(499).armed, "interrupted stabilization retained old eligible time");
    gate.observe(500, true);
    check(gate.snapshot(500).armed, "new consecutive eligible interval did not arm");
}

void consume_requires_a_new_clear_and_entry()
{
    HandGate gate;
    arm(gate);
    gate.consume();
    check(gate.snapshot(400).available && !gate.snapshot(400).armed &&
              gate.snapshot(400).phase == HandPhase::NeedClear,
          "consume did not remove the permission immediately");
    gate.consume();
    for (std::uint32_t now = 450; now <= 10000; now += 50) {
        gate.observe(now, true);
        check(!gate.snapshot(now).armed && gate.snapshot(now).phase == HandPhase::NeedClear,
              "continuous presence rearmed a consumed presentation");
    }
    clear(gate, 10050);
    gate.observe(10350, true);
    gate.observe(10450, true);
    check(gate.snapshot(10450).armed, "leaving and re-entering did not create a new permission");
}

void timeout_does_not_extend_with_presence()
{
    HandGate gate;
    arm(gate);
    for (const auto now : {800u, 1200u, 1600u, 2000u}) gate.observe(now, true);
    check(gate.snapshot(2199).armed, "1800 ms permission ended before its deadline");
    const auto expired = gate.snapshot(2200);
    check(expired.available && !expired.armed && expired.phase == HandPhase::NeedClear,
          "permission did not expire exactly 1800 ms after arming");
    check(gate.snapshot(2199).armed, "snapshot changed the stored phase while checking timeout");
    gate.observe(2200, true);
    gate.observe(2300, true);
    check(!gate.snapshot(2300).armed && gate.snapshot(2300).phase == HandPhase::NeedClear,
          "presence immediately rearmed after timeout");
    clear(gate, 2400);
    gate.observe(2700, true);
    gate.observe(2800, true);
    check(gate.snapshot(2800).armed, "fresh clear and presentation after timeout did not arm");
}

void armed_absence_does_not_preclear_a_consumed_permission()
{
    HandGate gate;
    arm(gate);
    gate.observe(500, false);
    gate.observe(800, false);
    check(gate.snapshot(800).armed, "valid absence prematurely removed the bounded permission");
    gate.consume();
    gate.observe(900, true);
    gate.observe(1000, true);
    check(!gate.snapshot(1000).armed, "absence before consume was reused to rearm after consume");
}

void snapshots_reject_stale_and_future_capture_times_without_mutating()
{
    HandGate gate;
    arm(gate);
    check(gate.snapshot(900).available && gate.snapshot(900).armed, "500 ms freshness boundary was rejected");
    const auto stale = gate.snapshot(901);
    check(!stale.available && !stale.armed && stale.phase == HandPhase::NeedClear,
          "501 ms old capture remained available or armed");
    const auto future = gate.snapshot(399);
    check(!future.available && !future.armed, "future capture was accepted by snapshot");
    check(gate.snapshot(400).armed, "snapshot mutated the stored permission");
    gate.observe(901, true);
    check(!gate.snapshot(901).armed && gate.snapshot(901).phase == HandPhase::NeedClear,
          "observation after a 501 ms gap retained the stored permission");
}

void uneven_sampling_and_gaps_restart_evidence()
{
    HandGate gate;
    gate.observe(0, false);
    gate.observe(501, false);
    check(gate.snapshot(501).phase == HandPhase::NeedClear, "gap joined two otherwise clear frames");
    gate.observe(751, false);
    check(gate.snapshot(751).phase == HandPhase::Looking, "new clear evidence after gap did not complete");
    gate.observe(800, true);
    gate.observe(1301, true);
    check(!gate.snapshot(1301).armed && gate.snapshot(1301).phase == HandPhase::NeedClear,
          "gap joined eligible frames into one presentation");

    gate.reset();
    gate.observe(0, false);
    gate.observe(500, false);
    gate.observe(1000, true);
    gate.observe(1500, true);
    check(gate.snapshot(1500).armed, "exactly 500 ms gaps or uneven stable sampling were rejected");
    gate.observe(2001, false);
    check(!gate.snapshot(2001).armed && gate.snapshot(2001).phase == HandPhase::NeedClear,
          "an armed acquisition gap did not close the permission");
    gate.observe(2251, false);
    check(gate.snapshot(2251).phase == HandPhase::Looking, "gap frame was not a new clear interval start");
}

void invalid_frames_close_every_phase()
{
    for (unsigned phase = 0; phase < 4; ++phase) {
        HandGate gate;
        gate.observe(0, false);
        if (phase >= 1) gate.observe(250, false);
        if (phase >= 2) gate.observe(300, true);
        if (phase >= 3) gate.observe(400, true);
        gate.observe(450, false, false);
        const auto invalid = gate.snapshot(450);
        check(!invalid.available && !invalid.armed && invalid.phase == HandPhase::NeedClear,
              "invalid capture did not fail closed");
        gate.observe(500, false);
        gate.observe(749, false);
        check(gate.snapshot(749).phase == HandPhase::NeedClear, "invalid frame was counted as clear evidence");
        gate.observe(750, false);
        check(gate.snapshot(750).phase == HandPhase::Looking, "clear did not recover after invalid capture");
    }
}

void duplicate_and_backdated_frames_fail_closed()
{
    HandGate gate;
    arm(gate);
    gate.observe(399, true);
    const auto late = gate.snapshot(400);
    check(!late.available && !late.armed && late.phase == HandPhase::NeedClear && late.captured_ms == 400,
          "backdated capture retained permission or rewound the time high-water mark");
    gate.observe(398, false);
    gate.observe(500, true);
    check(!gate.snapshot(500).armed, "backdated captures contributed evidence");
    clear(gate, 600);
    gate.observe(900, true);
    gate.observe(900, true);
    check(!gate.snapshot(900).available && !gate.snapshot(900).armed,
          "duplicate frame remained available or supplied a second stable frame");
    gate.observe(1000, true);
    check(gate.snapshot(1000).phase == HandPhase::NeedClear, "duplicate frame did not reset presentation evidence");

    gate.reset();
    gate.observe(0, false);
    gate.observe(0, false);
    gate.observe(250, false);
    check(gate.snapshot(250).phase == HandPhase::NeedClear, "duplicate clear frame retained an old clear start");
    gate.observe(500, false);
    check(gate.snapshot(500).phase == HandPhase::Looking, "duplicate recovery did not require a fresh clear interval");
}

void uint32_rollover_preserves_all_windows()
{
    HandGate gate;
    constexpr auto start = std::numeric_limits<std::uint32_t>::max() - 349u;
    arm(gate, start);
    check(gate.snapshot(start + 400u).captured_ms == 50, "test did not cross the timestamp wrap");
    for (const auto offset : {800u, 1200u, 1600u, 2000u}) gate.observe(start + offset, true);
    check(gate.snapshot(start + 2199u).armed && !gate.snapshot(start + 2200u).armed,
          "armed deadline failed across timestamp wrap");
    gate.observe(start + 2200u, true);
    check(gate.snapshot(start + 2200u).phase == HandPhase::NeedClear, "wrapped timeout did not require clear");

    gate.reset();
    constexpr auto before_wrap = std::numeric_limits<std::uint32_t>::max() - 99u;
    gate.observe(before_wrap, false);
    gate.observe(150, false);
    gate.observe(200, true);
    gate.observe(300, true);
    check(gate.snapshot(300).armed, "clear interval failed across timestamp wrap");
    check(gate.snapshot(800).armed && !gate.snapshot(801).available, "freshness failed after wrapped clear");
    gate.observe(299, false);
    check(!gate.snapshot(300).available && !gate.snapshot(300).armed,
          "late input after a timestamp wrap bypassed chronological checks");

    gate.reset();
    gate.observe(0, false);
    check(gate.snapshot(0).available, "capture timestamp zero was treated as missing");
}

void sustained_typing_presence_motion_only_negative_control()
{
    HandGate gate;
    arm(gate);
    gate.consume();
    MotionDetector motion;
    std::array<std::uint8_t, kGridCells> grid{};
    motion.process(grid, 400);
    unsigned motion_only_windows = 0;
    unsigned hand_rearmed_windows = 0;
    // Synthetic local pixel changes stand in for repeated keyboard movement.
    // The supplied presence label remains true throughout; it is not a claim
    // about model detections or real-world recognition accuracy.
    for (std::uint32_t now = 500; now <= 4100; now += 100) {
        grid[0] = grid[1] = grid[2] = grid[0] ? 0 : 20;
        const auto visual = motion.process(grid, now);
        const bool typing_presence = true;
        gate.observe(now, typing_presence);
        if ((now - 500u) % 1200u != 0) continue;
        const bool old_motion_gate = visual.available && visual.motion && !visual.global_change &&
                                     now - visual.captured_ms <= 500u;
        if (old_motion_gate) ++motion_only_windows;
        if (gate.snapshot(now).armed) ++hand_rearmed_windows;
    }
    check(motion_only_windows == 4 && hand_rearmed_windows == 0,
          "sustained-presence negative control did not distinguish reusable motion from a consumed presentation");
    std::cout << "synthetic sustained typing presence: motion-only eligible windows=" << motion_only_windows
              << ", hand rearmed windows=" << hand_rearmed_windows << '\n';
}

}  // namespace

int main()
{
    const std::array tests{
        interaction_region_requires_a_large_upper_central_hand,
        interaction_region_enforces_fractional_and_center_boundaries,
        interaction_region_rejects_invalid_boxes_scores_and_dimensions,
        hand_box_eligibility_drives_new_presentation,
        startup_and_reset_require_clear,
        clear_requires_observed_frames_and_elapsed_time,
        stabilization_requires_a_second_frame_and_100_ms,
        ineligible_frame_interrupts_stabilization,
        consume_requires_a_new_clear_and_entry,
        timeout_does_not_extend_with_presence,
        armed_absence_does_not_preclear_a_consumed_permission,
        snapshots_reject_stale_and_future_capture_times_without_mutating,
        uneven_sampling_and_gaps_restart_evidence,
        invalid_frames_close_every_phase,
        duplicate_and_backdated_frames_fail_closed,
        uint32_rollover_preserves_all_windows,
        sustained_typing_presence_motion_only_negative_control,
    };
    try {
        for (const auto test : tests) test();
    } catch (const std::exception& error) {
        std::cerr << "hand_gate_test: " << error.what() << '\n';
        return 1;
    }
    std::cout << "hand_gate_test: " << tests.size() << " groups passed (synthetic state/motion only)\n";
    return 0;
}
