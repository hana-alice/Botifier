#include "intro_player.h"
#include "intro_clip.h"
#include "nfc_service.h"

#include <hal/hal.h>
#include <hal/utils/jpeg_to_image/jpeg_decoder.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <array>
#include <cinttypes>
#include <mutex>
#include <new>

extern const std::uint8_t kuro_intro_start[] asm("_binary_kuro_intro_bin_start");
extern const std::uint8_t kuro_intro_end[] asm("_binary_kuro_intro_bin_end");
extern const std::uint8_t shorekeeper_intro_start[] asm("_binary_shorekeeper_intro_bin_start");
extern const std::uint8_t shorekeeper_intro_end[] asm("_binary_shorekeeper_intro_bin_end");

namespace intro_player {
namespace {

constexpr char tag[] = "intro-player";
std::mutex state_mutex;
std::mutex lifecycle_mutex;
Snapshot latest;
TaskHandle_t worker_task = nullptr;
bool cancel_requested = false;
std::uint32_t request_id = 0;
// The UI may briefly retain both its previous image and a copied snapshot.
// Reuse only expired slots, bounding decoded RGB ownership to two images even
// when UI rendering is delayed. Only the persistent worker touches these slots.
std::array<std::weak_ptr<LvglAllocatedImage>, 2> image_slots;

std::uint32_t now_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

bool cancelled(std::uint32_t request)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return cancel_requested || !latest.busy || request != request_id;
}

bool wait_until(std::uint32_t request, std::uint32_t begin, std::uint32_t elapsed)
{
    while (now_ms() - begin < elapsed) {
        if (cancelled(request)) return false;
        // UI, ToF and idle watchdog tasks must keep running during playback.
        vTaskDelay(1);
    }
    return !cancelled(request);
}

bool publish_frame(const intro_clip::Clip& clip, std::uint32_t index, std::uint32_t request)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (cancel_requested || request != request_id) return false;
        // Release the old snapshot before allocating the next RGB image.
        latest.frame.reset();
    }
    std::size_t slot = image_slots.size();
    while (slot == image_slots.size()) {
        if (cancelled(request)) return false;
        for (std::size_t i = 0; i < image_slots.size(); ++i) {
            if (image_slots[i].expired()) {
                slot = i;
                break;
            }
        }
        if (slot == image_slots.size()) vTaskDelay(1);
    }
    const auto jpeg = clip.frame(index);
    auto image = jpeg_dec::decode_to_lvgl(jpeg.data, jpeg.size);
    if (!image) return false;
    const auto* descriptor = image->image_dsc();
    if (!descriptor || descriptor->header.w != 320 || descriptor->header.h != 240) return false;
    image_slots[slot] = image;
    std::lock_guard<std::mutex> lock(state_mutex);
    if (cancel_requested || request != request_id) return false;
    latest.frame = std::move(image);
    ++latest.frame_sequence;
    return true;
}

esp_err_t play(std::uint32_t request, ClipId clip_id, bool& sensing_paused)
{
    // Consume presentations during every accepted playback, including invalid
    // media or cancellation before the worker first runs.
    nfc_sense::pause_for_playback();
    sensing_paused = true;
    if (cancelled(request)) return ESP_OK;
    const auto* begin = clip_id == ClipId::Shorekeeper ? shorekeeper_intro_start : kuro_intro_start;
    const auto* end = clip_id == ClipId::Shorekeeper ? shorekeeper_intro_end : kuro_intro_end;
    const auto begin_address = reinterpret_cast<std::uintptr_t>(begin);
    const auto end_address = reinterpret_cast<std::uintptr_t>(end);
    intro_clip::Clip clip;
    if (end_address < begin_address || !clip.parse(begin, end_address - begin_address)) {
        ESP_LOGE(tag, "embedded intro pack rejected");
        return ESP_ERR_INVALID_SIZE;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        latest.duration_ms = clip.duration_ms();
    }
    if (cancelled(request)) return ESP_OK;
    // Decode the initial frame before starting the media clock so allocation
    // latency does not consume the opening frame's presentation interval.
    if (!publish_frame(clip, 0, request)) return cancelled(request) ? ESP_OK : ESP_FAIL;
    if (cancelled(request)) return ESP_OK;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        latest.state = State::Playing;
    }
    ESP_LOGI(tag, "playing frames=%" PRIu32 " fps=%u duration_ms=%" PRIu32,
             clip.frame_count(), static_cast<unsigned>(clip.fps()), clip.duration_ms());

    const auto started = now_ms();
    std::uint32_t shown_frame = 0;
    while (!cancelled(request)) {
        const auto elapsed = now_ms() - started;
        if (elapsed >= clip.duration_ms()) break;
        const auto target = std::min<std::uint32_t>(clip.frame_count() - 1, elapsed * clip.fps() / 1000u);
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            latest.position_ms = std::min(elapsed, clip.duration_ms());
        }
        if (target != shown_frame) {
            if (!publish_frame(clip, target, request)) return cancelled(request) ? ESP_OK : ESP_FAIL;
            shown_frame = target;
        }
        const auto next_frame_ms = shown_frame + 1 < clip.frame_count()
                                       ? ((shown_frame + 1) * 1000u + clip.fps() - 1) / clip.fps()
                                       : clip.duration_ms();
        // A slow decode skips straight to the current frame on the next loop;
        // no frame backlog or media request queue is retained.
        if (!wait_until(request, started, std::min(next_frame_ms, clip.duration_ms()))) break;
    }
    return ESP_OK;
}

void worker(void*)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        std::uint32_t request;
        ClipId clip_id;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (!latest.busy) continue;
            request = request_id;
            clip_id = latest.clip;
        }
        bool sensing_paused = false;
        esp_err_t error = ESP_OK;
        try {
            error = play(request, clip_id, sensing_paused);
        } catch (const std::bad_alloc&) {
            error = ESP_ERR_NO_MEM;
        } catch (...) {
            error = ESP_FAIL;
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            latest.state = State::Stopping;
        }
        // Capture the old accepted-event boundary before restarting sensing.
        // Resume never revives a closed app or queues a tap made during playback.
        const auto baseline = nfc_sense::snapshot().accepted;
        if (sensing_paused) nfc_sense::resume_after_playback();
        const bool was_cancelled = cancelled(request);
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            latest.frame.reset();
            latest.sensing_baseline = baseline;
            ++latest.completed_count;
            latest.error = error;
            latest.state = error == ESP_OK ? State::Idle : State::Error;
            if (error == ESP_OK && !was_cancelled) latest.position_ms = latest.duration_ms;
            latest.busy = false;
        }
        ESP_LOGI(tag, "finished error=%d cancelled=%u", static_cast<int>(error), was_cancelled ? 1u : 0u);
    }
}

}  // namespace

esp_err_t start(ClipId clip)
{
    if (clip != ClipId::Kuro && clip != ClipId::Shorekeeper) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    TaskHandle_t task;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (latest.busy) return ESP_ERR_INVALID_STATE;
        task = worker_task;
    }
    if (!task) {
        if (xTaskCreatePinnedToCore(worker, "intro-player", 8192, nullptr, 3, &task, 0) != pdPASS) {
            std::lock_guard<std::mutex> lock(state_mutex);
            latest.state = State::Error;
            latest.error = ESP_ERR_NO_MEM;
            return ESP_ERR_NO_MEM;
        }
        std::lock_guard<std::mutex> lock(state_mutex);
        worker_task = task;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        ++request_id;
        cancel_requested = false;
        latest.frame.reset();
        latest.position_ms = 0;
        latest.duration_ms = 0;
        latest.error = ESP_OK;
        latest.state = State::Starting;
        latest.clip = clip;
        latest.busy = true;
    }
    ESP_LOGI(tag, "accepted request=%" PRIu32 " clip=%s", request_id,
             clip == ClipId::Shorekeeper ? "shorekeeper" : "kuro");
    xTaskNotifyGive(task);
    return ESP_OK;
}

void stop()
{
    // Hold lifecycle across acknowledgement so another start cannot resurrect
    // playback while the app is closing. The worker never takes this mutex.
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        cancel_requested = true;
        if (!latest.busy) {
            latest.frame.reset();
            return;
        }
        latest.state = State::Stopping;
    }
    while (true) {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (!latest.busy) break;
        }
        GetHAL().feedTheDog();
    }
}

Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return latest;
}

}  // namespace intro_player
