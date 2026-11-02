#pragma once
#include "types.h"

namespace idle_program {
// Called only with LVGL held. Owns no foreground app or shared avatar.
void render_locked(const Snapshot& value);
void clear_locked();
} // namespace idle_program
