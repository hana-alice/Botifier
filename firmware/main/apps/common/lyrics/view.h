#pragma once

#include "types.h"

namespace lyrics {

// LVGL render-cycle work while this view exists, not panel vsync/FPS.
struct RenderStats {
    bool active = false;
    std::uint32_t elapsed_ms = 0, cycles = 0, render_ms = 0, max_render_ms = 0;
    std::uint32_t flushed_pixels = 0, scaled_glyph_rasters = 0, cue_rasters = 0, cache_failures = 0;
};
RenderStats render_stats();

// Presentation never owns the scheduler. Call only after higher-priority
// activities have yielded, and clear while another surface owns the display.
bool wants_surface(const Snapshot& value, std::uint64_t now_ms);
void render_locked(const Snapshot& value);
void clear_locked();

} // namespace lyrics
