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
Snapshot snapshot();
const char* program_text(Program value);
const char* weekly_status_text(WeeklyStatus value);

} // namespace idle_program
