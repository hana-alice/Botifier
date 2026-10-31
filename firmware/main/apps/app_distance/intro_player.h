#pragma once

#include <esp_err.h>
#include <cstdint>
#include <memory>

class LvglAllocatedImage;

namespace intro_player {

enum class State { Idle, Starting, Playing, Stopping, Error };

struct Snapshot {
    bool busy = false;
    State state = State::Idle;
    esp_err_t error = ESP_OK;
    // Monotonic within this boot. The UI retains its image until a new frame
    // arrives; frame may be null while decoding or after playback ends.
    std::uint32_t frame_sequence = 0;
    std::shared_ptr<LvglAllocatedImage> frame;
    std::uint32_t position_ms = 0;
    std::uint32_t duration_ms = 0;
    // Every accepted start completes once, including cancellation/error paths.
    // Captured before sensing resumes, allowing the app to discard old taps
    // without dropping a new card presentation after the handoff.
    std::uint32_t completed_count = 0;
    std::uint32_t sensing_baseline = 0;
};

// ESP_OK means accepted; a busy player rejects the request without queuing it.
// start() contains no codec, camera, decoder, or LVGL operations.
esp_err_t start();
// Call outside LVGL locks. Requests cancellation and waits for the worker.
void stop();
Snapshot snapshot();

}  // namespace intro_player
