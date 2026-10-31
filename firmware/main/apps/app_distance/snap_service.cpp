#include "snap_service.h"
#include "hand_model.h"

#include <board.h>
#include <hal/board/cores3_audio_codec.h>
#include <hal/board/hal_bridge.h>
#include <hal/hal.h>
#include <distance_sensor/service.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <memory>
#include <new>
#include <vector>

namespace snap_sense {
namespace {

constexpr char tag[] = "snap-sense";
constexpr std::uint32_t visual_timeout_ms = 500;
constexpr std::uint32_t camera_period_ms = 200;
constexpr std::uint32_t retry_ms = 1000;
constexpr std::uint32_t telemetry_period_ms = 200;
// AC reference-channel RMS threshold is a conservative, uncalibrated playback
// heuristic. It is not acoustic echo cancellation or proof of a finger snap.
constexpr std::uint32_t reference_rms_threshold = 200;

std::mutex state_mutex;
std::mutex lifecycle_mutex;
Snapshot latest;
TaskHandle_t audio_task = nullptr;
TaskHandle_t camera_task = nullptr;
std::uint32_t epoch = 1;
std::uint32_t camera_wait_since_ms = 0;
bool audio_idle = true;
std::uint32_t audio_idle_epoch = 1;
bool candidate_pending = false;
HandGate hand_gate; // Pure state; always protected by state_mutex.

std::uint32_t now_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

struct Control {
    Config config;
    bool listening;
    std::uint32_t epoch;
    bool active;
};

bool listening_enabled(const Snapshot& state)
{
    return state.active && state.config.listening && !state.playback_paused;
}

Control control()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return {latest.config, listening_enabled(latest), epoch, latest.active};
}

bool camera_enabled(const Control& request)
{
    return request.listening && request.config.camera_required;
}

void notify_workers()
{
    TaskHandle_t audio;
    TaskHandle_t camera;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        audio = audio_task;
        camera = camera_task;
    }
    if (audio) xTaskNotifyGive(audio);
    if (camera) xTaskNotifyGive(camera);
}

// All *_locked helpers require state_mutex and contain no I/O or waits.
void cancel_pending_locked()
{
    if (candidate_pending) {
        ++latest.rejected;
        latest.last_decision = Decision::Suppressed;
        candidate_pending = false;
    }
}

void reset_request_locked(std::uint32_t timestamp)
{
    ++epoch;
    cancel_pending_locked();
    latest.audio = {};
    latest.audio.reason = listening_enabled(latest) ? AudioReason::Warming : AudioReason::Quiet;
    latest.visual = {};
    hand_gate.reset();
    latest.hand = {};
    latest.hand_box = {};
    latest.hand_in_zone = false;
    latest.camera_capture_ms = 0;
    latest.hand_inference_ms = 0;
    latest.camera_error = ESP_OK;
    latest.last_audio_event = {};
    latest.last_audio_event_ms = 0;
    latest.has_audio_event = false;
    latest.camera_status = listening_enabled(latest) && latest.config.camera_required
                               ? CameraStatus::Warming : CameraStatus::Off;
    camera_wait_since_ms = timestamp;
    if (!audio_task) {
        audio_idle = true;
        audio_idle_epoch = epoch;
    }
}

float finite_value(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

void emit_telemetry(std::uint32_t& last_emitted_ms)
{
    if (now_ms() - last_emitted_ms < telemetry_period_ms) return;
    const auto current = snapshot();
    // Sample the reporting clock after the copied event/frame timestamps.
    const auto timestamp = now_ms();
    last_emitted_ms = timestamp;
    // ESP-IDF's configured ROM nano printf cannot consume 64-bit integers.
    // The snapshot only exposes fresh visual evidence (<=500ms); age needs
    // a signed sentinel, not a long long that corrupts later varargs parsing.
    const int visual_age = current.visual.available
                               ? static_cast<int>(timestamp - current.visual.captured_ms) : -1;
    char event[240];
    if (current.has_audio_event) {
        std::snprintf(event, sizeof(event),
                      "\"event_reason\":\"%s\",\"event_duration_ms\":%" PRIu32
                      ",\"event_edge_ratio\":%.4f,\"event_crest\":%.3f,\"event_age_ms\":%" PRIu32,
                      reason_text(current.last_audio_event.reason), current.last_audio_event.duration_ms,
                      finite_value(current.last_audio_event.edge_ratio), finite_value(current.last_audio_event.crest),
                      timestamp - current.last_audio_event_ms);
    } else {
        std::snprintf(event, sizeof(event),
                      "\"event_reason\":null,\"event_duration_ms\":null,\"event_edge_ratio\":null,"
                      "\"event_crest\":null,\"event_age_ms\":null");
    }
    std::printf("SC_SNAP {\"v\":1,\"uptime_ms\":%" PRIu32 ",\"status\":\"%s\",\"active\":%s,"
                "\"audio_open\":%s,\"playback_paused\":%s,\"peak\":%" PRIu32 ",\"rms\":%" PRIu32 ",\"noise\":%" PRIu32
                ",\"duration\":%" PRIu32 ",\"edge_ratio\":%.4f,\"crest\":%.3f,\"audio_reason\":\"%s\","
                "\"camera_status\":\"%s\",\"camera_samples\":%" PRIu32 ",\"motion_cells\":%u,"
                "\"visual_age\":%d,\"candidates\":%" PRIu32 ",\"accepted\":%" PRIu32
                ",\"rejected\":%" PRIu32 ",\"camera_required\":%s,\"sensitivity\":%u,\"listening\":%s,"
                "\"last_decision\":\"%s\",\"error\":%d,\"camera_error\":%d,"
                "\"hand_phase\":\"%s\",\"hand_detected\":%s,\"hand_score\":%.3f,"
                "\"hand_in_zone\":%s,\"hand_armed\":%s,\"camera_capture_ms\":%" PRIu32
                ",\"hand_inference_ms\":%" PRIu32 ","
                "\"hand_box\":[%d,%d,%d,%d],%s}\n",
                timestamp, status_text(current.status), current.active ? "true" : "false",
                current.audio_open ? "true" : "false", current.playback_paused ? "true" : "false",
                current.audio.peak, current.audio.rms, current.audio.noise,
                current.audio.duration_ms, finite_value(current.audio.edge_ratio), finite_value(current.audio.crest),
                reason_text(current.audio.reason), camera_status_text(current.camera_status), current.camera_samples,
                static_cast<unsigned>(current.visual.changed_cells), visual_age, current.candidates, current.accepted,
                current.rejected, current.config.camera_required ? "true" : "false", current.config.sensitivity,
                current.config.listening ? "true" : "false", decision_text(current.last_decision),
                static_cast<int>(current.error), static_cast<int>(current.camera_error),
                hand_phase_text(current.hand.phase), current.hand_box.detected ? "true" : "false",
                finite_value(current.hand_box.score), current.hand_in_zone ? "true" : "false",
                current.visual.motion ? "true" : "false", current.camera_capture_ms, current.hand_inference_ms,
                current.hand_box.left, current.hand_box.top, current.hand_box.right, current.hand_box.bottom, event);
}

void audio_error(std::uint32_t expected_epoch, esp_err_t error)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    if (epoch != expected_epoch || !listening_enabled(latest)) return;
    cancel_pending_locked();
    latest.audio = {};
    latest.audio.reason = AudioReason::Invalid;
    latest.status = Status::AudioError;
    latest.error = error;
}

// An enable failure can leave vendor cleanup pending even when input_enabled()
// is false. Always complete a checked close after any enable attempt.
void close_input(CoreS3AudioCodec* codec, bool& touched, bool& opened, std::uint32_t& last_emitted_ms)
{
    if (!touched) return;
    while (!codec->TryEnableInput(false)) {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            latest.status = listening_enabled(latest) ? Status::AudioError : Status::Stopping;
            latest.error = ESP_FAIL;
        }
        emit_telemetry(last_emitted_ms);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    touched = false;
    opened = false;
    std::lock_guard<std::mutex> lock(state_mutex);
    latest.audio_open = false;
}

bool reference_playback(const std::vector<std::int16_t>& samples)
{
    std::int64_t sum = 0;
    for (std::size_t i = 0; i < kBlockFrames; ++i) sum += samples[i * 2 + 1];
    const auto mean = static_cast<std::int32_t>(sum / static_cast<std::int64_t>(kBlockFrames));
    std::uint64_t energy = 0;
    for (std::size_t i = 0; i < kBlockFrames; ++i) {
        const auto sample = static_cast<std::int64_t>(samples[i * 2 + 1]) - mean;
        energy += static_cast<std::uint64_t>(sample * sample);
    }
    return energy > std::uint64_t{kBlockFrames} * reference_rms_threshold * reference_rms_threshold;
}

void audio_worker(void*)
{
    AudioDetector detector;
    Fusion fusion;
    CoreS3AudioCodec* codec = nullptr;
    bool touched = false;
    bool opened = false;
    bool pending = false;
    std::uint32_t local_epoch = 0;
    std::uint32_t last_emitted_ms = 0;
    // AudioCodec exposes vector InputData; allocate its fixed block once.
    std::vector<std::int16_t> pcm(kBlockFrames * 2);
    std::array<std::int16_t, kBlockFrames> microphone{};

    while (true) {
        const auto request = control();
        if (local_epoch != request.epoch) {
            detector.reset();
            detector.setSensitivity(request.config.sensitivity);
            fusion.reset();
            pending = false;
            local_epoch = request.epoch;
        }
        if (!request.listening) {
            close_input(codec, touched, opened, last_emitted_ms);
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (!listening_enabled(latest)) {
                    audio_idle = true;
                    audio_idle_epoch = epoch;
                    latest.audio_open = false;
                    latest.status = latest.active ? Status::Paused : Status::Off;
                    latest.error = ESP_OK;
                }
            }
            emit_telemetry(last_emitted_ms);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (epoch != request.epoch || !listening_enabled(latest)) continue;
            // Mark acquisition in flight before the first codec call. stop()
            // cannot treat audio_open=false during that call as an idle ack.
            audio_idle = false;
            if (!opened) latest.status = Status::Starting;
        }
        if (!codec) {
            auto* base = Board::GetInstance().GetAudioCodec();
            if (base && base->input_sample_rate() == static_cast<int>(kSampleRate) && base->input_channels() == 2) {
                // This project's StackChan board returns its fixed CoreS3 codec.
                codec = static_cast<CoreS3AudioCodec*>(base);
            }
        }
        if (!codec) {
            audio_error(request.epoch, ESP_ERR_NOT_SUPPORTED);
            ESP_LOGE(tag, "24 kHz stereo CoreS3 codec unavailable");
            emit_telemetry(last_emitted_ms);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
            continue;
        }
        if (!opened) {
            if (!touched && codec->input_enabled()) {
                audio_error(request.epoch, ESP_ERR_INVALID_STATE);
                ESP_LOGE(tag, "input already enabled by another owner");
                emit_telemetry(last_emitted_ms);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
                continue;
            }
            touched = true;
            if (!codec->TryEnableInput(true)) {
                audio_error(request.epoch, ESP_FAIL);
                ESP_LOGE(tag, "checked microphone enable failed");
                detector.reset();
                fusion.reset();
                pending = false;
                close_input(codec, touched, opened, last_emitted_ms);
                emit_telemetry(last_emitted_ms);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
                continue;
            }
            opened = true;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                latest.audio_open = true;
                latest.status = listening_enabled(latest) ? Status::Warming : Status::Stopping;
            }
        }
        if (control().epoch != request.epoch) continue;

        std::fill(pcm.begin(), pcm.end(), 0);
        if (!codec->InputData(pcm)) {
            audio_error(request.epoch, ESP_FAIL);
            ESP_LOGE(tag, "audio read failed; discarding the entire block");
            detector.reset();
            fusion.reset();
            pending = false;
            close_input(codec, touched, opened, last_emitted_ms);
            emit_telemetry(last_emitted_ms);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
            continue;
        }
        const auto timestamp = now_ms();
        if (control().epoch != request.epoch) continue;
        for (std::size_t i = 0; i < kBlockFrames; ++i) microphone[i] = pcm[i * 2];
        const bool playback = reference_playback(pcm);
        const auto features = detector.process(microphone.data(), kBlockFrames, 1, timestamp, playback);
        const auto visual = snapshot().visual;
        const auto distance = distance_sensor::snapshot();
        const bool blocked = !distance.valid || distance.stale || distance.filtered_mm < 0 ||
                             distance.proximity_state == distance_sensor::ProximityState::Alert ||
                             playback || features.reason == AudioReason::Playback ||
                             (request.config.camera_required && (!visual.available || !visual.motion));
        std::uint32_t new_candidates = 0;
        std::uint32_t new_accepted = 0;
        std::uint32_t new_rejected = 0;
        Decision decision = Decision::None;
        auto complete = [&](Decision result) {
            if (result == Decision::None) return;
            decision = result;
            if (result == Decision::Accepted) ++new_accepted;
            else ++new_rejected;
        };
        if (features.candidate) {
            ++new_candidates;
            const auto offered = fusion.offer(timestamp);
            if (offered == Decision::None) pending = true;
            complete(offered);
        }
        const auto updated = fusion.update(timestamp, request.config.camera_required, visual, blocked);
        if (updated != Decision::None) pending = false;
        complete(updated);

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (epoch == request.epoch && listening_enabled(latest)) {
                // Recheck under the gate's lock: a frame can invalidate the
                // permission between the copied evidence and this publication.
                if (new_accepted && request.config.camera_required &&
                    (!hand_gate.snapshot(now_ms()).armed || !latest.hand_in_zone)) {
                    new_accepted = 0;
                    ++new_rejected;
                    decision = Decision::Suppressed;
                }
                latest.audio = features;
                latest.last_audio_ms = timestamp;
                latest.status = features.reason == AudioReason::Warming ? Status::Warming : Status::Listening;
                latest.error = ESP_OK;
                latest.candidates += new_candidates;
                latest.accepted += new_accepted;
                latest.rejected += new_rejected;
                if (new_accepted) {
                    latest.last_accepted_ms = timestamp;
                    // One raised-hand presentation buys one interaction. Keeping
                    // hands on a keyboard cannot continuously re-arm the gate.
                    if (request.config.camera_required) {
                        hand_gate.consume();
                        latest.hand = hand_gate.snapshot(timestamp);
                        latest.visual.motion = false;
                    }
                }
                if (decision != Decision::None) latest.last_decision = decision;
                candidate_pending = pending;
                if (features.reason == AudioReason::Candidate || features.reason == AudioReason::Tonal ||
                    features.reason == AudioReason::LongSound || features.reason == AudioReason::Clipped) {
                    latest.last_audio_event = features;
                    latest.last_audio_event_ms = timestamp;
                    latest.has_audio_event = true;
                }
            }
        }
        emit_telemetry(last_emitted_ms);
        vTaskDelay(1);
    }
}

void camera_worker(void*)
{
    std::unique_ptr<HandModel> model;
    std::unique_ptr<std::uint8_t, decltype(&heap_caps_free)> rgb(nullptr, heap_caps_free);
    constexpr std::size_t rgb_bytes = 320 * 240 * 3;
    bool reported_slow_frame = false;
    std::uint32_t last_slow_report_ms = 0;
    auto failure = [](std::uint32_t expected_epoch, esp_err_t error, CameraStatus status,
                      std::uint32_t capture_ms = 0, std::uint32_t inference_ms = 0) {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (epoch != expected_epoch || !listening_enabled(latest) || !latest.config.camera_required) return;
        latest.camera_status = status;
        latest.camera_error = error;
        latest.visual = {};
        latest.hand_box = {};
        latest.hand_in_zone = false;
        latest.camera_capture_ms = capture_ms;
        latest.hand_inference_ms = inference_ms;
        hand_gate.observe(now_ms(), false, false);
        latest.hand = hand_gate.snapshot(now_ms());
    };
    while (true) {
        const auto request = control();
        if (!camera_enabled(request)) {
            // Keep weights during the short clip, release them when this app or
            // camera assist closes. The worker owns destruction, never the UI.
            if (!request.active || !request.config.listening || !request.config.camera_required) {
                model.reset();
                rgb.reset();
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (!rgb) {
            rgb.reset(static_cast<std::uint8_t*>(
                heap_caps_aligned_alloc(16, rgb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        }
        if (!rgb) {
            failure(request.epoch, ESP_ERR_NO_MEM, CameraStatus::Unavailable);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
            continue;
        }
        if (!model) {
            std::unique_ptr<HandModel> candidate(new (std::nothrow) HandModel());
            const auto error = candidate ? candidate->init() : ESP_ERR_NO_MEM;
            if (error != ESP_OK) {
                failure(request.epoch, error, CameraStatus::Unavailable);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(retry_ms));
                continue;
            }
            model = std::move(candidate);
            ESP_LOGI(tag, "hand model ready; camera stack_free=%u",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        }
        if (control().epoch != request.epoch) continue;
        const auto started = now_ms();
        auto* camera = hal_bridge::board_get_camera();
        std::size_t width = 0, height = 0;
        const auto result = camera ? camera->TrySampleRgbFrame(rgb.get(), rgb_bytes, width, height)
                                   : StackChanCamera::LumaSampleResult::Unavailable;
        // RGB sampling discards the queued frame and waits for a new one.
        // Use the request start as a conservative capture-time lower bound;
        // dequeue-completion time would understate the actual frame age.
        const auto captured = started;
        // DQBUF may block indefinitely. Nothing else waits for this task, and
        // inference runs only after QBUF and camera mutex release.
        const auto after = control();
        if (after.epoch != request.epoch || !camera_enabled(after)) continue;
        if (result == StackChanCamera::LumaSampleResult::Busy) {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (epoch == request.epoch) latest.camera_status = CameraStatus::Busy;
        } else if (result != StackChanCamera::LumaSampleResult::Ready) {
            failure(request.epoch, ESP_FAIL, result == StackChanCamera::LumaSampleResult::InvalidFrame
                                               ? CameraStatus::InvalidFrame : CameraStatus::Unavailable);
        } else {
            HandDetections detections;
            const auto inference_started = now_ms();
            const auto error = model->infer(rgb.get(), width, height, detections);
            const auto completed = now_ms();
            if (error != ESP_OK || completed - captured > visual_timeout_ms) {
                if (!reported_slow_frame || completed - last_slow_report_ms >= 5000u) {
                    reported_slow_frame = true;
                    last_slow_report_ms = completed;
                    ESP_LOGW(tag, "hand frame refused: capture_ms=%u inference_ms=%u total_ms=%u error=%d stack_free=%u",
                             static_cast<unsigned>(inference_started - started),
                             static_cast<unsigned>(completed - inference_started),
                             static_cast<unsigned>(completed - started), static_cast<int>(error),
                             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
                }
                failure(request.epoch, error == ESP_OK ? ESP_ERR_TIMEOUT : error, CameraStatus::Unavailable,
                        inference_started - started, completed - inference_started);
            } else {
                HandBox selected;
                bool in_zone = false;
                for (std::size_t i = 0; i < detections.count; ++i) {
                    const auto& box = detections.boxes[i];
                    const bool eligible = is_interaction_hand(box, width, height);
                    if ((eligible && !in_zone) || (eligible == in_zone && box.score > selected.score)) {
                        selected = box;
                        in_zone = eligible;
                    }
                }
                std::lock_guard<std::mutex> lock(state_mutex);
                if (epoch == request.epoch && listening_enabled(latest) && latest.config.camera_required) {
                    hand_gate.observe(captured, in_zone);
                    latest.hand = hand_gate.snapshot(completed);
                    latest.hand_box = selected;
                    latest.hand_in_zone = in_zone;
                    latest.camera_capture_ms = inference_started - started;
                    latest.hand_inference_ms = completed - inference_started;
                    latest.camera_status = CameraStatus::Ready;
                    latest.camera_error = ESP_OK;
                    latest.visual = {};
                    latest.visual.available = latest.hand.available;
                    latest.visual.captured_ms = captured;
                    latest.visual.motion = latest.hand.armed && in_zone;
                    latest.visual.motion_ms = captured;
                    ++latest.camera_samples;
                    camera_wait_since_ms = completed;
                }
            }
        }
        const auto elapsed = now_ms() - started;
        const auto wait = elapsed < camera_period_ms ? camera_period_ms - elapsed : 1;
        ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(wait)));
    }
}

esp_err_t ensure_tasks()
{
    // lifecycle_mutex serializes creation; task handles are never deleted.
    TaskHandle_t existing_audio;
    TaskHandle_t existing_camera;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        existing_audio = audio_task;
        existing_camera = camera_task;
    }
    TaskHandle_t created = nullptr;
    if (!existing_audio) {
        if (xTaskCreatePinnedToCore(audio_worker, "snap-audio", 6144, nullptr, 3, &created, 0) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
        std::lock_guard<std::mutex> lock(state_mutex);
        audio_task = created;
    }
    if (!existing_camera) {
        if (xTaskCreatePinnedToCore(camera_worker, "snap-camera", 16384, nullptr, 1, &created, 1) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
        std::lock_guard<std::mutex> lock(state_mutex);
        camera_task = created;
    }
    return ESP_OK;
}

// Caller holds lifecycle_mutex, preventing start/resume from reopening input
// before the worker acknowledges this (or a newer config) epoch as closed.
void wait_for_audio_idle()
{
    notify_workers();
    while (true) {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (audio_idle && audio_idle_epoch == epoch && !latest.audio_open) break;
        }
        GetHAL().feedTheDog();
        vTaskDelay(1);
    }
}

}  // namespace

esp_err_t start()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    const auto error = ensure_tasks();
    const auto timestamp = now_ms();
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (error != ESP_OK) {
            latest.error = error;
            latest.status = Status::AudioError;
            return error;
        }
        if (latest.active) return ESP_OK;
        latest.active = true;
        latest.error = ESP_OK;
        reset_request_locked(timestamp);
        latest.status = listening_enabled(latest) ? Status::Starting : Status::Paused;
    }
    ESP_LOGI(tag, "DISTANCE local sensing requested (no AI AudioService)");
    notify_workers();
    return ESP_OK;
}

void stop()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    const auto timestamp = now_ms();
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!latest.active && audio_idle && !latest.audio_open) return;
        latest.active = false;
        reset_request_locked(timestamp);
        latest.status = audio_task ? Status::Stopping : Status::Off;
    }
    ESP_LOGI(tag, "stop requested; waiting for checked microphone cleanup");
    wait_for_audio_idle();
    ESP_LOGI(tag, "microphone handed back; camera session invalidated without joining DQBUF");
}

void pause_for_playback()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!latest.playback_paused) {
            latest.playback_paused = true;
            reset_request_locked(now_ms());
        }
        latest.status = audio_task ? Status::Stopping : (latest.active ? Status::Paused : Status::Off);
    }
    ESP_LOGI(tag, "playback pause requested; waiting for checked microphone cleanup");
    wait_for_audio_idle();
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        latest.status = latest.active ? Status::Paused : Status::Off;
    }
    ESP_LOGI(tag, "playback pause acknowledged; microphone handed back");
}

void resume_after_playback()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!latest.playback_paused) return;
        latest.playback_paused = false;
        reset_request_locked(now_ms());
        latest.status = listening_enabled(latest) ? Status::Warming : (latest.active ? Status::Paused : Status::Off);
    }
    // stop() intentionally leaves the pause latch intact until the output owner
    // finishes cleanup. Clearing it here never changes the app's active state.
    notify_workers();
    ESP_LOGI(tag, "playback output closed; sensing session reset");
}

Config config()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return latest.config;
}

void set_config(const Config& configuration)
{
    auto next = configuration;
    next.sensitivity = std::clamp(next.sensitivity, 1u, 5u);
    const auto timestamp = now_ms();
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (latest.config.listening == next.listening && latest.config.camera_required == next.camera_required &&
            latest.config.sensitivity == next.sensitivity) return;
        latest.config = next;
        reset_request_locked(timestamp);
        if (latest.active) {
            latest.status = listening_enabled(latest) ? Status::Warming
                                          : (audio_idle && !latest.audio_open ? Status::Paused : Status::Stopping);
        }
    }
    notify_workers();
}

Snapshot snapshot()
{
    Snapshot result;
    std::uint32_t waiting_since;
    std::uint32_t timestamp;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        timestamp = now_ms();
        result = latest;
        result.hand = hand_gate.snapshot(timestamp);
        waiting_since = camera_wait_since_ms;
    }
    if (!listening_enabled(result) || !result.config.camera_required) {
        result.camera_status = CameraStatus::Off;
        result.visual = {};
        result.hand = {};
        result.hand_box = {};
        result.hand_in_zone = false;
    } else if ((result.visual.available && timestamp - result.visual.captured_ms > visual_timeout_ms) ||
               (!result.visual.available && timestamp - waiting_since > visual_timeout_ms)) {
        result.camera_status = CameraStatus::Unavailable;
        result.visual.available = false;
        result.visual.motion = false;
        result.hand = {};
        result.hand_box = {};
        result.hand_in_zone = false;
    } else {
        result.visual.available = result.hand.available;
        result.visual.motion = result.hand.armed && result.hand_in_zone;
    }
    return result;
}

const char* status_text(Status status)
{
    switch (status) {
        case Status::Off: return "off";
        case Status::Starting: return "starting";
        case Status::Warming: return "warming";
        case Status::Listening: return "listening";
        case Status::Paused: return "paused";
        case Status::AudioError: return "audio_error";
        case Status::Stopping: return "stopping";
    }
    return "unknown";
}

const char* camera_status_text(CameraStatus status)
{
    switch (status) {
        case CameraStatus::Off: return "off";
        case CameraStatus::Warming: return "warming";
        case CameraStatus::Ready: return "ready";
        case CameraStatus::Busy: return "busy";
        case CameraStatus::Unavailable: return "unavailable";
        case CameraStatus::InvalidFrame: return "invalid_frame";
    }
    return "unknown";
}

const char* hand_phase_text(HandPhase phase)
{
    switch (phase) {
        case HandPhase::NeedClear: return "clear";
        case HandPhase::Looking: return "show";
        case HandPhase::Stabilizing: return "hold";
        case HandPhase::Armed: return "armed";
    }
    return "unknown";
}

const char* decision_text(Decision decision)
{
    switch (decision) {
        case Decision::None: return "none";
        case Decision::Accepted: return "accepted";
        case Decision::Suppressed: return "suppressed";
        case Decision::Cooldown: return "cooldown";
        case Decision::NoVisual: return "no_visual";
    }
    return "unknown";
}

const char* reason_text(AudioReason reason)
{
    switch (reason) {
        case AudioReason::Warming: return "warming";
        case AudioReason::Quiet: return "quiet";
        case AudioReason::Candidate: return "candidate";
        case AudioReason::LongSound: return "long_sound";
        case AudioReason::Tonal: return "tonal";
        case AudioReason::Playback: return "playback";
        case AudioReason::Clipped: return "clipped";
        case AudioReason::Invalid: return "invalid";
    }
    return "unknown";
}

}  // namespace snap_sense
