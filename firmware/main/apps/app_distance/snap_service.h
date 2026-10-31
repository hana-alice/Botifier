#pragma once

#include "snap_detector.h"
#include "hand_gate.h"
#include <esp_err.h>
#include <cstdint>

namespace snap_sense {

struct Config {
    bool listening = true;
    bool camera_required = true;
    unsigned sensitivity = 3;  // Clamped to 1..5; RAM only.
};

enum class Status { Off, Starting, Warming, Listening, Paused, AudioError, Stopping };
enum class CameraStatus { Off, Warming, Ready, Busy, Unavailable, InvalidFrame };

struct Snapshot {
    Config config;
    Status status = Status::Off;
    CameraStatus camera_status = CameraStatus::Off;
    AudioFeatures audio;
    AudioFeatures last_audio_event;
    std::uint32_t last_audio_event_ms = 0;
    bool has_audio_event = false;
    VisualEvidence visual;
    HandGateState hand;
    HandBox hand_box;
    bool hand_in_zone = false;
    std::uint32_t hand_inference_ms = 0;
    esp_err_t camera_error = ESP_OK;
    bool active = false;
    bool audio_open = false;
    bool playback_paused = false;
    // Process-lifetime counters; changing pages/config never replays a decision.
    std::uint32_t candidates = 0;
    std::uint32_t accepted = 0;
    std::uint32_t rejected = 0;
    std::uint32_t last_accepted_ms = 0;
    std::uint32_t last_audio_ms = 0;
    std::uint32_t camera_samples = 0;
    Decision last_decision = Decision::None;
    esp_err_t error = ESP_OK;
};

// DISTANCE is the sole owner. start() never initializes the AI AudioService.
// Allocation failure is returned to the app without aborting ToF or the menu.
esp_err_t start();
// Call outside the LVGL lock. Waits for checked microphone cleanup, yielding
// via the existing watchdog helper; it has no guaranteed hard timeout. It never
// deletes an I2S task or joins the potentially blocked camera task.
void stop();
// Call outside the LVGL lock before enabling playback output. Returns only after
// checked microphone cleanup, with the same unbounded wait as stop(). Config
// and cumulative counters are preserved; stop() does not clear this pause latch.
void pause_for_playback();
// Call after playback output is fully closed, including cancellation/error paths.
// Flushes old evidence and resumes only if DISTANCE is still active/listening.
void resume_after_playback();
Config config();
void set_config(const Config& configuration);
// Copies state only: no codec/camera access. Visual evidence older than 500ms
// is unavailable. A candidate is a heuristic transient, not proof of a snap.
Snapshot snapshot();

const char* status_text(Status status);
const char* camera_status_text(CameraStatus status);
const char* hand_phase_text(HandPhase phase);
const char* decision_text(Decision decision);
const char* reason_text(AudioReason reason);

}  // namespace snap_sense
