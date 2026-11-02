#pragma once
#include "types.h"
#include <esp_err.h>

namespace idle_program {

esp_err_t start();
Config config();
esp_err_t set_config(const Config& value);
esp_err_t save_config();
void set_context(const Context& value);
void note_activity();
// Atomically claims the foreground slot unless a valid EyeRest already owns it.
// Caller must publish the actual playback context in this frame, even on failure.
bool try_begin_foreground_animation();
Snapshot snapshot();
const char* program_text(Program value);
const char* weekly_status_text(WeeklyStatus value);

} // namespace idle_program
