#include "service.h"
#include <apps/common/runtime_diagnostics.h>
#include "protocol.h"
#include "ble_client.h"

#include <apps/common/codex_weekly/service.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>
#include <nvs.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <strings.h>

namespace lyrics {
namespace {
std::mutex state_mutex;
protocol::Engine engine;
TaskHandle_t worker_task = nullptr;
bool command_pending = false;
bool forget_pending = false;
bool desired_enabled = false;
bool touched_enabled = false;
std::uint32_t command_generation = 0;

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }

bool current(const protocol::Request& request)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return engine.current(request);
}

struct HttpResponse {
    std::unique_ptr<char[]> bytes{new (std::nothrow) char[protocol::kMaxResponseBytes + 1]};
    std::size_t size = 0;
    std::string retry_after;
    std::uint64_t started_ms = now_ms();
    std::uint64_t overall_deadline = started_ms + 45000;
    std::uint64_t body_deadline = 0;
    int phase = 1;
    const protocol::Request* request = nullptr;
    bool invalid = false;
    bool interrupted = false;
};

enum class LookupStage { Lrclib, LrclibWithoutAlbum, NeteaseSearch, NeteaseLyrics, KugouSearch, KugouLyrics,
                         LrclibCatalogSearch, LrclibCatalogLyrics, NeteasePhoneSearch };

bool expired(const HttpResponse& response, std::uint64_t now)
{
    return now >= response.overall_deadline || (response.body_deadline && now >= response.body_deadline);
}

esp_err_t receive(esp_http_client_event_t* event)
{
    auto& response = *static_cast<HttpResponse*>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_CONNECTED && event->event_id != HTTP_EVENT_ON_HEADER &&
        event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    const auto now = now_ms();
    if (event->event_id == HTTP_EVENT_ON_CONNECTED && !response.body_deadline) {
        response.phase = 2;
        response.body_deadline = now + 10000;
    } else if (event->event_id == HTTP_EVENT_ON_HEADER) response.phase = std::max(response.phase, 3);
    else if (event->event_id == HTTP_EVENT_ON_DATA) response.phase = 4;
    if (expired(response, now) || !current(*response.request)) {
        response.interrupted = true;
        esp_http_client_close(event->client);
        return ESP_FAIL;
    }
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (strcasecmp(event->header_key, "Retry-After") == 0) {
            const auto size = strnlen(event->header_value, 32);
            if (size < 32) response.retry_after.assign(event->header_value, size);
        }
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        const auto count = static_cast<std::size_t>(event->data_len);
        if (!response.bytes || count > protocol::kMaxResponseBytes - response.size) {
            response.invalid = true;
            // IDF ignores an ON_DATA return code: close the transport too.
            esp_http_client_close(event->client);
            return ESP_FAIL;
        }
        std::memcpy(response.bytes.get() + response.size, event->data, count);
        response.size += count;
        response.bytes[response.size] = 0;
    }
    return ESP_OK;
}

protocol::Response fetch_step(const protocol::Request& request, LookupStage stage = LookupStage::Lrclib,
                             protocol::Candidate* selection = nullptr, std::uint64_t deadline = 0)
{
    protocol::Response result; result.state = LyricState::NetworkError; result.phase = 1;
    HttpResponse response; response.request = &request;
    if (deadline) response.overall_deadline = deadline;
    if (!response.bytes) { result.error = ESP_ERR_NO_MEM; return result; }
    const protocol::Candidate empty;
    const auto& candidate = selection ? *selection : empty;
    std::string url;
    switch (stage) {
    case LookupStage::Lrclib: url = protocol::request_url(request.track); break;
    case LookupStage::LrclibWithoutAlbum: url = protocol::request_url(request.track, false); break;
    case LookupStage::NeteaseSearch: url = protocol::netease_search_url(request.track); break;
    case LookupStage::NeteasePhoneSearch: url = protocol::netease_search_url(request.track, true); break;
    case LookupStage::NeteaseLyrics: url = protocol::netease_lyric_url(candidate.id); break;
    case LookupStage::KugouSearch: url = protocol::kugou_search_url(request.track); break;
    case LookupStage::KugouLyrics: url = protocol::kugou_lyric_url(candidate); break;
    case LookupStage::LrclibCatalogSearch: url = protocol::lrclib_catalog_search_url(request.track); break;
    case LookupStage::LrclibCatalogLyrics: url = protocol::lrclib_catalog_lyric_url(candidate); break;
    }
    if (url.empty()) { result.state = LyricState::Invalid; result.error = ESP_ERR_INVALID_RESPONSE; return result; }
    esp_http_client_config_t config{};
    config.url = url.c_str(); config.method = HTTP_METHOD_GET;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    // Match the transport already used successfully by the native account
    // worker. This worker alone blocks; UI/BLE callbacks only publish state.
    config.is_async = false;
    const auto before_connect = now_ms();
    if (expired(response, before_connect) || !current(request)) { result.error = ESP_ERR_TIMEOUT; return result; }
    config.timeout_ms = static_cast<int>(std::min<std::uint64_t>(10000, response.overall_deadline - before_connect));
    config.event_handler = receive; config.user_data = &response;
    config.buffer_size_tx = static_cast<int>(url.size() + 256);
    auto client = esp_http_client_init(&config);
    if (!client) { result.error = ESP_ERR_NO_MEM; return result; }
    auto error = esp_http_client_set_header(client, "User-Agent", "Botifier/0.1 (https://github.com/hana-alice/Botifier)");
    if (error == ESP_OK) error = esp_http_client_set_header(client, "Accept", "application/json");
    if (error == ESP_OK && (stage == LookupStage::NeteaseSearch || stage == LookupStage::NeteasePhoneSearch ||
                           stage == LookupStage::NeteaseLyrics))
        error = esp_http_client_set_header(client, "Referer", "https://music.163.com/");
    if (error == ESP_OK) {
        if (expired(response, now_ms()) || !current(request)) error = ESP_ERR_TIMEOUT;
        else error = esp_http_client_perform(client);
    }
    const auto status = esp_http_client_get_status_code(client);
    const auto socket_error = esp_http_client_get_errno(client);
    int tls_error = 0, tls_flags = 0;
    esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &tls_flags);
    // Synchronous DNS cannot be interrupted here. Enforce the overall budget
    // in callbacks and again on return, so a late result is never published.
    const auto finished_ms = now_ms();
    const bool timed_out = response.interrupted || expired(response, finished_ms) || !current(request);
    esp_http_client_cleanup(client);
    if (response.invalid) { result.state = LyricState::Invalid; result.error = ESP_ERR_INVALID_RESPONSE; }
    else if (timed_out) result.error = ESP_ERR_TIMEOUT;
    else if (error != ESP_OK) result.error = error;
    else {
        const std::string_view body(response.bytes.get(), response.size);
        if (stage == LookupStage::Lrclib || stage == LookupStage::LrclibWithoutAlbum) {
            auto expected = request.track;
            if (stage == LookupStage::LrclibWithoutAlbum) expected.album.clear();
            result = protocol::parse_response(status, body, expected, response.retry_after);
        } else if (stage == LookupStage::NeteaseLyrics) {
            result = protocol::parse_netease_lyrics(status, body, request.track);
            result.phone_identity = candidate.phone_identity && result.lines && !result.lines->empty() &&
                (result.state == LyricState::Synced || result.state == LyricState::Unsynced);
            result.phone_canonical_title = result.phone_identity && candidate.phone_canonical_title;
        }
        else if (stage == LookupStage::KugouLyrics) result = protocol::parse_kugou_lyrics(status, body, request.track);
        else if (stage == LookupStage::LrclibCatalogLyrics)
            result = protocol::parse_lrclib_catalog_lyrics(status, body, request.track, candidate, response.retry_after);
        else if (status == 200 && selection) {
            *selection = stage == LookupStage::LrclibCatalogSearch ? protocol::parse_lrclib_catalog_search(body, request.track)
                       : (stage == LookupStage::NeteaseSearch || stage == LookupStage::NeteasePhoneSearch) ?
                           protocol::parse_netease_search(body, request.track, request.phone_metadata)
                                                          : protocol::parse_kugou_search(body, request.track);
            result.state = selection->valid ? LyricState::NotFound : LyricState::Invalid;
        } else result = protocol::parse_response(status, {}, request.track, response.retry_after);
    }
    result.http_status = status;
    result.phase = response.phase;
    result.elapsed_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX, finished_ms - response.started_ms));
    result.socket_errno = socket_error; result.tls_error = tls_error; result.tls_flags = tls_flags;
    return result;
}

protocol::Response fetch(const protocol::Request& request)
{
    const auto started = now_ms();
    const auto deadline = started + 45000;
    protocol::Candidate selection;
    unsigned requests = 0;
    std::uint32_t retry_after = 0;
    protocol::Response result;
    protocol::Response failure;
    protocol::Response text_fallback;
    bool had_failure = false;
    bool lrclib_rate_limited = false;
    auto run = [&](LookupStage stage) {
        if (requests) vTaskDelay(pdMS_TO_TICKS(250));
        if (requests >= 8 || now_ms() >= deadline || !current(request)) {
            result.state = LyricState::NetworkError; result.error = ESP_ERR_TIMEOUT;
            return;
        }
        result = fetch_step(request, stage, &selection, deadline);
        ++requests;
        if (result.state == LyricState::Unsynced && result.lines && !result.lines->empty() && !text_fallback.lines)
            text_fallback = result;
        if (result.http_status == 429 && (stage == LookupStage::Lrclib || stage == LookupStage::LrclibWithoutAlbum ||
            stage == LookupStage::LrclibCatalogSearch || stage == LookupStage::LrclibCatalogLyrics)) lrclib_rate_limited = true;
        retry_after = std::max(retry_after, result.retry_after_ms);
        if ((result.state == LyricState::NetworkError || result.state == LyricState::Invalid) &&
            (!had_failure || (result.state == LyricState::NetworkError && failure.state != LyricState::NetworkError) ||
             (result.state == failure.state && result.retry_after_ms > failure.retry_after_ms))) {
            failure = result;
            had_failure = true;
        }
        // Numeric lookup provenance only. Never log URLs, names or lyric text.
        std::printf("SC_LYRICS {\"v\":1,\"generation\":%lu,\"stage\":%u,\"http_status\":%d,"
                    "\"state\":%u,\"error\":%d,\"lines\":%u,\"requests\":%u}\n",
                    static_cast<unsigned long>(request.generation), static_cast<unsigned>(stage), result.http_status,
                    static_cast<unsigned>(result.state), result.error,
                    result.lines ? static_cast<unsigned>(result.lines->size()) : 0u, requests);
    };
    auto finished = [&]() {
        return result.state == LyricState::Synced || result.state == LyricState::Instrumental ||
               (request.phone_metadata && result.phone_identity && result.state == LyricState::Unsynced) ||
               result.error == ESP_ERR_NO_MEM || requests >= 8 || now_ms() >= deadline || !current(request);
    };
    auto download = [&](LookupStage stage) {
        run(stage);
        if (!finished() && selection.fallback_id && (result.state == LyricState::NotFound ||
            result.state == LyricState::Invalid || result.state == LyricState::Unsynced)) {
            selection.id = selection.fallback_id; selection.fallback_id = 0;
            run(stage);
        }
    };
    if (request.phone_metadata) {
        bool ordinary_search = request.track.album.empty();
        if (!request.track.album.empty()) {
            // Lock-screen lyrics change title every line. Search stable metadata
            // first; source reconstruction still proves artist, recording and
            // duration before the engine can expose any downloaded lyric.
            run(LookupStage::NeteasePhoneSearch);
            ordinary_search = result.error == 0 && result.http_status == 200 && !selection.id;
            if (!finished() && result.error == 0 && result.http_status == 200 && selection.valid && selection.id)
                download(LookupStage::NeteaseLyrics);
        }
        if (!finished() && ordinary_search) {
            // A slash can also belong to an ordinary artist such as AC/DC.
            // Keep its original exact query; do not repeat a failed/rate-limited host.
            selection = {};
            run(LookupStage::NeteaseSearch);
            if (!finished() && result.error == 0 && result.http_status == 200 && selection.valid && selection.id)
                download(LookupStage::NeteaseLyrics);
        }
    }
    if (!finished()) run(LookupStage::Lrclib);
    const bool exact_miss = (result.state == LyricState::NotFound || result.state == LyricState::Unsynced) &&
                            !request.track.album.empty();
    // The compact catalog can resolve a same-album duration discrepancy in
    // two requests without first waiting for unrelated providers to miss.
    if (!finished() && exact_miss) {
        selection = {};
        run(LookupStage::LrclibCatalogSearch);
        if (!finished() && result.error == 0 && result.http_status == 200 && selection.valid && selection.id)
            download(LookupStage::LrclibCatalogLyrics);
    }
    if (!finished() && exact_miss && !lrclib_rate_limited)
        run(LookupStage::LrclibWithoutAlbum);
    if (!finished() && !request.phone_metadata) {
        selection = {};
        run(LookupStage::NeteaseSearch);
        if (!finished() && result.error == 0 && result.http_status == 200 && selection.valid && selection.id)
            download(LookupStage::NeteaseLyrics);
    }
    if (!finished()) {
        selection = {};
        run(LookupStage::KugouSearch);
        if (!finished() && result.error == 0 && result.http_status == 200 && selection.valid && selection.id)
            run(LookupStage::KugouLyrics);
    }
    // A missing or malformed result must not erase a transport failure
    // or a provider's Retry-After when none of the fallbacks succeeded.
    if ((result.state == LyricState::NotFound || result.state == LyricState::Invalid) && had_failure)
        result = std::move(failure);
    if (result.state != LyricState::Synced && result.state != LyricState::Instrumental && text_fallback.lines)
        result = std::move(text_fallback);
    result.retry_after_ms = std::max(result.retry_after_ms, retry_after);
    result.elapsed_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX, now_ms() - started));
    return result;
}

bool load_enabled(esp_err_t& error)
{
    nvs_handle_t handle;
    error = nvs_open("sc_lyrics", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) { error = ESP_OK; return false; }
    if (error != ESP_OK) return false;
    std::uint8_t value = 0;
    error = nvs_get_u8(handle, "enabled", &value);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK && value > 1) error = ESP_ERR_INVALID_STATE;
    return error == ESP_OK && value == 1;
}

esp_err_t save_enabled(bool enabled)
{
    nvs_handle_t handle;
    auto error = nvs_open("sc_lyrics", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 16, 0);
    error = nvs_set_u8(handle, "enabled", enabled ? 1 : 0);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 17, error);
    if (error == ESP_OK) {
        botifier_diag_mark(BOTIFIER_DIAG_BLE, 18, 0);
        error = nvs_commit(handle);
        botifier_diag_mark(BOTIFIER_DIAG_BLE, 19, error);
    }
    nvs_close(handle);
    return error;
}

void worker(void*)
{
    esp_err_t storage_error = ESP_OK;
    const bool saved = load_enabled(storage_error);
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!touched_enabled) {
            desired_enabled = saved;
            engine.set_enabled(saved, now_ms());
            command_pending = true;
        }
        if (storage_error != ESP_OK) engine.error(storage_error);
    }
    while (true) {
        bool command, forget, enabled;
        std::uint32_t revision;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            command = command_pending; forget = forget_pending; enabled = desired_enabled;
            revision = command_generation;
            command_pending = forget_pending = false;
        }
        if (forget) botifier_ams_forget_phone();
        if (command || forget) {
            botifier_diag_mark(BOTIFIER_DIAG_BLE, 1, 0);
            botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
            const auto error = botifier_ams_set_enabled(enabled);
            botifier_diag_mark(BOTIFIER_DIAG_BLE, 15, 0);
            botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
            const auto saved_error = save_enabled(enabled);
            botifier_diag_mark(BOTIFIER_DIAG_BLE, 20, saved_error);
            botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
            if (error != ESP_OK || saved_error != ESP_OK) {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (revision == command_generation) {
                    if (error != ESP_OK) engine.link(LinkState::Error, error, now_ms());
                    else engine.error(saved_error);
                }
            }
            botifier_diag_mark(BOTIFIER_DIAG_BLE, 21, error != ESP_OK ? error : saved_error);
        }
        // Native account service already owns the network/clock startup. Only
        // inspect its safe Wi-Fi flag; never inspect credentials or its HTTP.
        const bool network_ready = codex_weekly::snapshot().wifi_connected && std::time(nullptr) >= 1577836800;
        protocol::Request request;
        bool run = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            const auto now = now_ms();
            engine.advance(now);
            if (network_ready) run = engine.take_request(now, request);
        }
        if (run) {
            auto response = fetch(request);
            std::lock_guard<std::mutex> lock(state_mutex);
            engine.complete(request, std::move(response), now_ms());
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
}

void notify(TaskHandle_t task) { if (task) xTaskNotifyGive(task); }
} // namespace

esp_err_t start()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    if (worker_task) return ESP_OK;
    // Explicit internal RAM: BLE/TLS can call code while flash cache is gated.
    if (xTaskCreatePinnedToCoreWithCaps(worker, "lyrics", 8192, nullptr, 1, &worker_task, 0,
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        worker_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void set_enabled(bool enabled)
{
    TaskHandle_t task;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        touched_enabled = true; desired_enabled = enabled; command_pending = true; ++command_generation;
        engine.set_enabled(enabled, now_ms()); task = worker_task;
    }
    notify(task);
}

void forget_phone()
{
    TaskHandle_t task;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        forget_pending = command_pending = true; ++command_generation;
        const auto now = now_ms();
        engine.set_enabled(false, now); engine.set_enabled(desired_enabled, now); task = worker_task;
    }
    notify(task);
}

Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return engine.snapshot(now_ms());
}

void on_link(LinkState state, int error)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    engine.link(state, error, now_ms());
}

void on_attribute(std::uint8_t entity, std::uint8_t attribute, std::string_view value)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    engine.attribute(entity, attribute, value, now_ms());
}

void on_track_invalidated()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    engine.invalidate_track(now_ms());
}

void on_track_snapshot(const Track& track)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    engine.track_snapshot(track, now_ms());
}

const char* link_text(LinkState state)
{
    switch (state) {
    case LinkState::Off: return "off"; case LinkState::Pairing: return "pair_phone";
    case LinkState::Connecting: return "connecting"; case LinkState::Discovering: return "discovering";
    case LinkState::Ready: return "ready"; case LinkState::Error: return "bluetooth_error";
    }
    return "bluetooth_error";
}

const char* state_text(LyricState state)
{
    switch (state) {
    case LyricState::Waiting: return "waiting_track"; case LyricState::Loading: return "loading_lyrics";
    case LyricState::Synced: return "synced"; case LyricState::NotFound: return "no_synced_lyrics";
    case LyricState::Instrumental: return "instrumental"; case LyricState::NetworkError: return "network_error";
    case LyricState::Invalid: return "invalid_lyrics";
    case LyricState::Unsynced: return "unsynced_text";
    }
    return "invalid_lyrics";
}
} // namespace lyrics
