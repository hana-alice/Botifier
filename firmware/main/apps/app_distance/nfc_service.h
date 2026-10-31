#pragma once

#include "nfc_reader.h"
#include <esp_err.h>
#include <cstdint>

namespace nfc_sense {

struct Config {
    bool enabled = true;
};

enum class Status { Off, Starting, Ready, CardPresent, Error };

struct Snapshot {
    bool active = false;
    bool playback_paused = false;
    Config config;
    Status status = Status::Off;
    esp_err_t error = ESP_OK;
    // Process-lifetime counters. reads includes failed current-session polls;
    // errors counts initialization and poll failures. Held cards only add reads.
    std::uint32_t accepted = 0;
    std::uint32_t reads = 0;
    std::uint32_t errors = 0;
    std::uint32_t last_accepted_ms = 0;
    Card card;
    bool waiting_for_removal = false;
};

esp_err_t start();
// State changes are immediate. The worker closes the reader asynchronously.
void stop();
Config config();
void set_config(const Config& configuration);
Snapshot snapshot();
// Playback keeps polling and consumes presentations. No hardware I/O or joins.
void pause_for_playback();
void resume_after_playback();
const char* status_text(Status status);

}  // namespace nfc_sense
