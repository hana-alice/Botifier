#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace distance_pet {

struct ContentFrameTiming {
    std::uint32_t start_ms = 0;
    std::uint32_t end_ms = 0;
    std::uint32_t lock_wait_ms = 0;
    std::uint32_t head_before_ms = 0;
    std::uint32_t head_after_ms = 0;
    std::uint32_t stack_ms = 0;
    std::uint32_t render_ms = 0;
};

struct ContentStageTiming {
    std::uint32_t sum_ms = 0;
    std::uint32_t max_ms = 0;
    void add(std::uint32_t duration) noexcept
    {
        sum_ms += duration;
        max_ms = std::max(max_ms, duration);
    }
};

struct ContentTimingReport {
    std::uint32_t generation = 0;
    std::uint32_t window_start_ms = 0;
    std::uint32_t first_uptime_ms = 0;
    std::uint32_t last_uptime_ms = 0;
    std::uint32_t content_update = 0;
    std::uint32_t span_ms = 0;
    std::uint32_t gaps = 0;
    std::uint32_t gap_sum_ms = 0;
    std::uint32_t gap_max_ms = 0;
    std::uint32_t gap_ge50 = 0;
    std::uint32_t gap_ge100 = 0;
    std::uint32_t tail = 0;
    ContentStageTiming content;
    ContentStageTiming lock_wait;
    ContentStageTiming head_before;
    ContentStageTiming head_after;
    ContentStageTiming head;
    ContentStageTiming stack;
    ContentStageTiming render;
};

struct ContentTimingBatch {
    std::array<ContentTimingReport, 2> reports{};
    std::uint32_t count = 0;
};

// Counts submitted app content-update calls; rendering may internally throttle.
// These are not actual draws, LVGL flushes or physically displayed frames.
// The first report starts with content; subsequent windows start at the previous
// report's completion, preserving waiting time in the rate denominator. Gaps are
// between adjacent content starts; keep the cross-window gap within one session.
class ContentTimingWindow {
public:
    static constexpr std::uint32_t kWindowMs = 2000;

    ContentTimingBatch add(bool eligible, std::uint32_t generation, const ContentFrameTiming& frame) noexcept
    {
        ContentTimingBatch ready;
        if (!eligible || (_have_session && generation != _session_generation)) {
            append(ready, 1);
            _have_previous = _have_session = false;
        }
        if (!eligible) return ready;
        _have_session = true;
        _session_generation = generation;
        if (!_active) {
            _active = true;
            _report.generation = generation;
            _report.window_start_ms = _have_previous ? _previous_end_ms : frame.start_ms;
            _report.first_uptime_ms = frame.start_ms;
        }
        if (_have_previous) {
            const auto gap = frame.start_ms - _previous_start_ms;
            ++_report.gaps;
            _report.gap_sum_ms += gap;
            _report.gap_max_ms = std::max(_report.gap_max_ms, gap);
            if (gap >= 50) ++_report.gap_ge50;
            if (gap >= 100) ++_report.gap_ge100;
        }
        _have_previous = true;
        _previous_start_ms = frame.start_ms;
        _previous_end_ms = frame.end_ms;
        ++_report.content_update;
        _report.last_uptime_ms = frame.end_ms;
        _report.span_ms = frame.end_ms - _report.window_start_ms;
        _report.content.add(frame.end_ms - frame.start_ms);
        _report.lock_wait.add(frame.lock_wait_ms);
        _report.head_before.add(frame.head_before_ms);
        _report.head_after.add(frame.head_after_ms);
        _report.head.add(frame.head_before_ms + frame.head_after_ms);
        _report.stack.add(frame.stack_ms);
        _report.render.add(frame.render_ms);
        if (_report.span_ms >= kWindowMs) append(ready, 0);
        return ready;
    }

    ContentTimingBatch finish() noexcept
    {
        ContentTimingBatch ready;
        append(ready, 1);
        _have_previous = _have_session = false;
        return ready;
    }

private:
    void append(ContentTimingBatch& ready, std::uint32_t tail) noexcept
    {
        if (!_active) return;
        _report.tail = tail;
        ready.reports[ready.count++] = _report;
        _report = {};
        _active = false;
    }

    ContentTimingReport _report;
    std::uint32_t _previous_start_ms = 0;
    std::uint32_t _previous_end_ms = 0;
    std::uint32_t _session_generation = 0;
    bool _active = false;
    bool _have_previous = false;
    bool _have_session = false;
};

} // namespace distance_pet
