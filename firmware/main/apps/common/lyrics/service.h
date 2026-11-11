#pragma once

#include "types.h"
#include <esp_err.h>
#include <cstddef>
#include <string_view>

namespace lyrics {

// Process-wide worker. UI actions only enqueue; TLS, BLE startup and NVS work
// are never performed while an LVGL event callback holds the display lock.
esp_err_t start();
void set_enabled(bool enabled);
void forget_phone();
Snapshot snapshot();

// Called by the BLE host: publish bounded complete UTF-8 values, never touch UI.
void on_link(LinkState state, int error = 0);
void on_attribute(std::uint8_t entity, std::uint8_t attribute, std::string_view value);
// BLE brackets its epoch-checked complete Track read with these calls so
// independently notified fields never create a mixture of two songs.
void on_track_invalidated();
void on_track_snapshot(const Track& track);
const char* link_text(LinkState state);
const char* state_text(LyricState state);

} // namespace lyrics
