#include <apps/app_distance/content_timing.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using distance_pet::ContentFrameTiming;
using distance_pet::ContentTimingWindow;

void check(bool value, const char* text)
{
    if (!value) throw std::runtime_error(text);
}

ContentFrameTiming frame(std::uint32_t start, std::uint32_t end)
{
    return {start, end, 3, 5, 7, 11, 13};
}

void inactive_and_empty_windows_emit_nothing()
{
    ContentTimingWindow window;
    check(window.add(false, 9, frame(0, 20)).count == 0 && window.finish().count == 0,
          "inactive or empty timing window emitted a report");
}

void actual_window_span_and_stage_totals_are_reported()
{
    ContentTimingWindow window;
    check(window.add(true, 12, frame(0, 40)).count == 0, "window emitted early");
    check(window.add(true, 12, frame(1000, 1040)).count == 0, "window emitted before two seconds");
    const auto ready = window.add(true, 12, frame(2000, 2040));
    check(ready.count == 1, "due content window did not emit");
    const auto& r = ready.reports[0];
    check(r.generation == 12 && r.content_update == 3 && r.span_ms == 2040 &&
          r.first_uptime_ms == 0 && r.last_uptime_ms == 2040 && r.tail == 0, "window used nominal time or lost frames");
    check(r.gaps == 2 && r.gap_sum_ms == 2000 && r.gap_max_ms == 1000 && r.gap_ge50 == 2 && r.gap_ge100 == 2,
          "adjacent content-start gaps were not counted");
    check(r.head.sum_ms == 36 && r.head.max_ms == 12 && r.head_before.sum_ms == 15 && r.head_after.sum_ms == 21 &&
          r.stack.sum_ms == 33 && r.render.sum_ms == 39 && r.lock_wait.sum_ms == 9 &&
          r.content.sum_ms == 120 && r.content.max_ms == 40, "stage sums or frame maxima were wrong");
    check(window.finish().count == 0, "already emitted window was duplicated on exit");
}

void long_frame_uses_actual_span_without_fake_fps()
{
    ContentTimingWindow window;
    const auto ready = window.add(true, 1, frame(50, 2800));
    check(ready.count == 1 && ready.reports[0].content_update == 1 && ready.reports[0].span_ms == 2750 &&
          ready.reports[0].gaps == 0 && ready.reports[0].content.max_ms == 2750, "long frame span was truncated");
}

void phase_exit_flushes_only_recorded_tail_and_resets_gap()
{
    ContentTimingWindow window;
    window.add(true, 1, frame(100, 120));
    const auto tail = window.add(false, 1, frame(4000, 4500));
    check(tail.count == 1 && tail.reports[0].tail == 1 && tail.reports[0].span_ms == 20 &&
          tail.reports[0].content_update == 1 && tail.reports[0].last_uptime_ms == 120,
          "phase exit included inactive time or lost its tail");
    window.add(true, 1, frame(6000, 6020));
    const auto next = window.finish();
    check(next.count == 1 && next.reports[0].gaps == 0 && next.reports[0].content_update == 1,
          "new phase inherited an inactive gap");
    check(window.finish().count == 0, "close emitted a duplicate tail");
}

void adjacent_gap_crosses_reports_within_same_session()
{
    ContentTimingWindow window;
    window.add(true, 3, frame(0, 5));
    window.add(true, 3, frame(2000, 2005));
    window.add(true, 3, frame(2500, 2505));
    const auto tail = window.finish();
    check(tail.count == 1 && tail.reports[0].content_update == 1 && tail.reports[0].span_ms == 500 &&
          tail.reports[0].window_start_ms == 2005 && tail.reports[0].first_uptime_ms == 2500 &&
          tail.reports[0].gaps == 1 && tail.reports[0].gap_sum_ms == 500, "report rollover lost an adjacent frame gap");
}

void generation_change_can_flush_old_tail_and_new_long_frame()
{
    ContentTimingWindow window;
    window.add(true, 7, frame(0, 10));
    const auto ready = window.add(true, 8, frame(50, 3050));
    check(ready.count == 2 && ready.reports[0].generation == 7 && ready.reports[0].tail == 1 &&
          ready.reports[0].span_ms == 10 && ready.reports[1].generation == 8 && ready.reports[1].tail == 0 &&
          ready.reports[1].span_ms == 3000 && ready.reports[1].gaps == 0, "generation change mixed sessions or lost a report");
}

void unsigned_clock_wrap_preserves_span_and_gaps()
{
    ContentTimingWindow window;
    const std::uint32_t start = 0xFFFFFFF0u;
    window.add(true, 1, frame(start, start + 10));
    window.add(true, 1, frame(start + 100, start + 130));
    const auto tail = window.finish();
    check(tail.count == 1 && tail.reports[0].span_ms == 130 && tail.reports[0].gap_sum_ms == 100 &&
          tail.reports[0].gap_ge50 == 1 && tail.reports[0].gap_ge100 == 1 && tail.reports[0].content.sum_ms == 40,
          "unsigned wrap corrupted timing evidence");
}

void threshold_boundaries_and_combined_head_max_are_exact()
{
    ContentTimingWindow window;
    window.add(true, 1, frame(0, 1));
    window.add(true, 1, frame(49, 50));
    window.add(true, 1, frame(99, 100));
    auto sample = frame(199, 200);
    sample.head_before_ms = 20;
    sample.head_after_ms = 1;
    window.add(true, 1, sample);
    const auto tail = window.finish();
    check(tail.reports[0].gap_ge50 == 2 && tail.reports[0].gap_ge100 == 1 && tail.reports[0].gap_max_ms == 100,
          "gap thresholds were not inclusive");
    check(tail.reports[0].head.max_ms == 21 && tail.reports[0].head_before.max_ms == 20 &&
          tail.reports[0].head_after.max_ms == 7, "combined head maximum was fabricated by adding independent maxima");
}
} // namespace

int main()
{
    try {
        inactive_and_empty_windows_emit_nothing();
        actual_window_span_and_stage_totals_are_reported();
        long_frame_uses_actual_span_without_fake_fps();
        phase_exit_flushes_only_recorded_tail_and_resets_gap();
        adjacent_gap_crosses_reports_within_same_session();
        generation_change_can_flush_old_tail_and_new_long_frame();
        unsigned_clock_wrap_preserves_span_and_gaps();
        threshold_boundaries_and_combined_head_max_are_exact();
        std::cout << "content timing: 8 groups passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
