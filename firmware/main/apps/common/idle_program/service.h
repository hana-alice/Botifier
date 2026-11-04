#pragma once
#include "types.h"
#include <esp_err.h>

namespace idle_program {

struct WeeklyUpdate;
esp_err_t start();
// The native account worker is the sole weekly-data producer. This only copies
// a validated update under the existing policy lock; no network or I/O occurs.
bool publish_weekly(const WeeklyUpdate& value);
Config config();
esp_err_t set_config(const Config& value);
esp_err_t save_config();
void set_context(const Context& value);
void note_activity();
// Atomically claims the foreground slot unless a valid EyeRest already owns it.
// Caller must publish the actual playback context in this frame, even on failure.
bool try_begin_foreground_animation();
// Acknowledges the actual end (or start failure) of this generation's intro.
// The eye sequence keeps ownership; ordinary foreground context must stay idle.
bool finish_eye_intro(std::uint32_t generation);
Snapshot snapshot();
const char* program_text(Program value);
const char* eye_phase_text(EyePhase value);
const char* weekly_status_text(WeeklyStatus value);

} // namespace idle_program
