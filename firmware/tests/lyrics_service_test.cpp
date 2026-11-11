// The real adapter/worker is compiled here; only platform I/O is substituted.
#include "../main/apps/common/lyrics/service.cpp"
#include <ArduinoJson.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

struct FakeHttpClient {
    esp_http_client_config_t config;
    std::string url;
    bool closed = false;
    int status = 200;
    int socket_error = 0, tls_error = 0, tls_flags = 0;
};

namespace {
unsigned checks = 0;
std::uint64_t clock_ms = 1000;
unsigned perform_calls = 0, close_calls = 0, cleanup_calls = 0, nvs_calls = 0;
unsigned ble_calls = 0, forget_calls = 0, notify_calls = 0, task_creates = 0;
unsigned task_stack = 0, task_caps = 0, task_priority = 0;
int task_core = -1;
TaskFunction_t created_task = nullptr;
bool wifi_connected = false, stored_enabled = false;
esp_err_t nvs_open_error = ESP_OK, nvs_commit_error = ESP_OK, ble_error = ESP_OK;
std::vector<std::pair<std::string, std::string>> headers;
std::function<esp_err_t(FakeHttpClient&)> perform_action;
struct StopWorker {};

void check(bool value, const char* description)
{
    ++checks;
    if (!value) throw std::runtime_error(description);
}
lyrics::Track song() { return {"Spotify", "Original Test", "Botifier", "Demo", 60000}; }

void reset()
{
    lyrics::engine = lyrics::protocol::Engine{};
    lyrics::worker_task = nullptr;
    lyrics::command_pending = lyrics::forget_pending = lyrics::desired_enabled = lyrics::touched_enabled = false;
    lyrics::command_generation = 0;
    clock_ms = 1000;
    perform_calls = close_calls = cleanup_calls = nvs_calls = ble_calls = forget_calls = notify_calls = task_creates = 0;
    nvs_open_error = nvs_commit_error = ble_error = ESP_OK;
    stored_enabled = wifi_connected = false; headers.clear(); perform_action = {};
}

lyrics::protocol::Request ready(const lyrics::Track& track = song())
{
    lyrics::set_enabled(true);
    lyrics::on_link(lyrics::LinkState::Ready);
    lyrics::on_track_snapshot(track);
    clock_ms += lyrics::protocol::kMetadataSettleMs;
    lyrics::protocol::Request request;
    check(lyrics::engine.take_request(clock_ms, request), "prepare request through actual service publication");
    return request;
}

esp_err_t emit(FakeHttpClient& client, esp_http_client_event_id_t id, std::string_view data = {})
{
    esp_http_client_event_t event{};
    event.client = &client; event.event_id = id;
    event.data = const_cast<char*>(data.data()); event.data_len = static_cast<int>(data.size());
    event.user_data = client.config.user_data;
    return client.config.event_handler(&event);
}

std::string body(const lyrics::Track& track = song())
{
    JsonDocument doc;
    doc["id"] = 42;
    doc["trackName"] = track.title; doc["artistName"] = track.artist; doc["albumName"] = track.album;
    doc["duration"] = track.duration_ms / 1000.0; doc["instrumental"] = false;
    doc["syncedLyrics"] = "[00:01.00]Original window phrase\n[00:04.00]A second original phrase";
    std::string result; serializeJson(doc, result); return result;
}

void http_transport()
{
    reset(); const auto request = ready();
    perform_action = [](FakeHttpClient& client) {
        check(client.config.disable_auto_redirect && !client.config.is_async, "HTTPS synchronous and redirects disabled");
        check(client.config.crt_bundle_attach == esp_crt_bundle_attach && client.config.timeout_ms == 10000, "TLS bundle and ten second socket budget");
        check(client.url.find("https://lrclib.net/api/get?") == 0, "fixed HTTPS endpoint");
        clock_ms += 9000; // DNS/connect does not spend the response-read budget.
        emit(client, HTTP_EVENT_ON_CONNECTED);
        clock_ms += 1000;
        emit(client, HTTP_EVENT_ON_HEADER);
        clock_ms += 2000;
        emit(client, HTTP_EVENT_ON_DATA, body()); return ESP_OK;
    };
    auto result = lyrics::fetch_step(request);
    check(result.state == lyrics::LyricState::Synced && result.lines && result.lines->size() == 2, "real fetch parses response");
    check(cleanup_calls == 1, "transport cleanup after successful fetch");
    check(std::find(headers.begin(), headers.end(), std::make_pair(std::string("User-Agent"), std::string("Botifier/0.1 (https://github.com/hana-alice/Botifier)"))) != headers.end(), "attributed user agent");
    check(headers.size() == 2, "no credential or account headers");
    check(result.phase == 4 && result.elapsed_ms == 12000 && result.socket_errno == 0 && result.tls_error == 0 && result.tls_flags == 0,
          "separate connection and response budgets permit valid twelve second request");

    perform_action = [](FakeHttpClient& client) {
        emit(client, HTTP_EVENT_ON_CONNECTED);
        std::string chunk(4096, 'x');
        for (unsigned i = 0; i < 17; ++i) if (emit(client, HTTP_EVENT_ON_DATA, chunk) != ESP_OK) return ESP_FAIL;
        return ESP_OK;
    };
    result = lyrics::fetch_step(request);
    check(result.state == lyrics::LyricState::Invalid && result.error == ESP_ERR_INVALID_RESPONSE, "over 64KiB response invalid");
    check(close_calls == 1 && cleanup_calls == 2, "oversized stream actively closed and cleaned");

    // EAGAIN was the former asynchronous regression. A synchronous adapter does
    // not poll it; preserve an unexpected SDK return and always clean up.
    perform_action = [](FakeHttpClient&) { return ESP_ERR_HTTP_EAGAIN; };
    const auto previous_calls = perform_calls;
    result = lyrics::fetch_step(request);
    check(result.state == lyrics::LyricState::NetworkError && result.error == ESP_ERR_HTTP_EAGAIN && perform_calls == previous_calls + 1,
          "synchronous unexpected EAGAIN is returned without former async retry loop");
    check(cleanup_calls == 3, "unexpected transport return cleanup");

    perform_action = [](FakeHttpClient& client) {
        emit(client, HTTP_EVENT_ON_CONNECTED);
        clock_ms += 10001;
        return emit(client, HTTP_EVENT_ON_DATA, "late");
    };
    result = lyrics::fetch_step(request);
    check(result.state == lyrics::LyricState::NetworkError && result.error == ESP_ERR_TIMEOUT && result.phase == 4 && result.elapsed_ms == 10001,
          "callback rejects trickling data after response deadline");
    check(close_calls == 2 && cleanup_calls == 4, "expired callback closes once without disconnect recursion");

    perform_action = [](FakeHttpClient& client) {
        lyrics::on_track_invalidated();
        return emit(client, HTTP_EVENT_ON_DATA, body());
    };
    result = lyrics::fetch_step(request);
    check(!lyrics::engine.complete(request, std::move(result), clock_ms) && !lyrics::snapshot().lines, "stale inflight result cannot publish");
    check(close_calls == 3 && cleanup_calls == 5, "stale inflight request closes transport");
    const auto calls = perform_calls;
    result = lyrics::fetch_step(request);
    check(perform_calls == calls && result.error == ESP_ERR_TIMEOUT, "already stale request never performs HTTP");

    reset(); const auto connected_request = ready();
    perform_action = [](FakeHttpClient& client) {
        emit(client, HTTP_EVENT_ON_CONNECTED);
        clock_ms += 10000;
        return ESP_OK; // No further callback: the post-perform check is required.
    };
    result = lyrics::fetch_step(connected_request);
    check(result.error == ESP_ERR_TIMEOUT && result.phase == 2 && result.elapsed_ms == 10000 && cleanup_calls == 1,
          "connection without timely response expires even when SDK returns success");

    perform_action = [](FakeHttpClient& client) {
        emit(client, HTTP_EVENT_ON_CONNECTED);
        clock_ms += 9000;
        emit(client, HTTP_EVENT_ON_CONNECTED);
        clock_ms += 1001;
        return ESP_OK;
    };
    result = lyrics::fetch_step(connected_request);
    check(result.error == ESP_ERR_TIMEOUT && result.elapsed_ms == 10001, "duplicate connected event cannot extend first body deadline");

    perform_action = [](FakeHttpClient& client) {
        clock_ms += 44000;
        emit(client, HTTP_EVENT_ON_CONNECTED);
        clock_ms += 1001;
        return emit(client, HTTP_EVENT_ON_DATA, body());
    };
    result = lyrics::fetch_step(connected_request);
    check(result.error == ESP_ERR_TIMEOUT && result.phase == 4 && result.elapsed_ms == 45001 && close_calls == 1,
          "overall forty-five second budget still limits late connection plus body");

    perform_action = [](FakeHttpClient& client) {
        client.status = 0;
        clock_ms += 46000; // Model blocking DNS: it cannot be hard-interrupted.
        return ESP_OK;
    };
    result = lyrics::fetch_step(connected_request);
    check(result.error == ESP_ERR_TIMEOUT && result.phase == 1 && result.elapsed_ms == 46000,
          "late blocking connect return discarded without claiming a hard DNS deadline");

    perform_action = [](FakeHttpClient& client) {
        client.status = 0; client.socket_error = 111; client.tls_error = 123; client.tls_flags = 456;
        clock_ms += 1500;
        return static_cast<esp_err_t>(28674); // ESP_ERR_HTTP_CONNECT
    };
    result = lyrics::fetch_step(connected_request);
    check(result.state == lyrics::LyricState::NetworkError && result.error == 28674 && result.phase == 1 && result.elapsed_ms == 1500 &&
          result.socket_errno == 111 && result.tls_error == 123 && result.tls_flags == 456, "connection error retains safe numeric transport diagnostics");
    check(lyrics::engine.complete(connected_request, result, clock_ms), "current failure diagnostics publish");
    const auto failed = lyrics::snapshot();
    check(failed.phase == 1 && failed.elapsed_ms == 1500 && failed.socket_errno == 111 && failed.tls_error == 123 && failed.tls_flags == 456,
          "snapshot exposes only numeric failure diagnostics");
    lyrics::on_track_invalidated();
    const auto cleared = lyrics::snapshot();
    check(cleared.phase == 0 && cleared.elapsed_ms == 0 && cleared.socket_errno == 0 && cleared.tls_error == 0 && cleared.tls_flags == 0,
          "track invalidation clears previous transport diagnostics");

    reset(); const auto late_stale = ready();
    perform_action = [](FakeHttpClient&) {
        lyrics::on_track_invalidated();
        return ESP_OK;
    };
    result = lyrics::fetch_step(late_stale);
    check(result.error == ESP_ERR_TIMEOUT && !lyrics::engine.complete(late_stale, result, clock_ms) && cleanup_calls == 1,
          "post-perform stale check also rejects results with no data callback");
}

void run_one_worker_iteration()
{
    try { lyrics::worker(nullptr); }
    catch (const StopWorker&) { return; }
    check(false, "worker test exits at scheduled wait");
}

std::string netease_candidates(bool matching = true)
{
    JsonDocument doc;
    doc["code"] = 200;
    auto entry = doc["result"]["songs"].to<JsonArray>().add<JsonObject>();
    entry["id"] = 42;
    entry["name"] = song().title;
    entry["duration"] = song().duration_ms;
    entry["album"]["name"] = song().album;
    entry["artists"].to<JsonArray>().add<JsonObject>()["name"] = matching ? song().artist : "Another artist";
    std::string result; serializeJson(doc, result); return result;
}

std::string netease_lyrics()
{
    return R"({"code":200,"lrc":{"lyric":"[00:01.00]Original window phrase\n[00:04.00]A second original phrase"}})";
}

void reply(FakeHttpClient& client, int status, std::string_view body)
{
    client.status = status;
    emit(client, HTTP_EVENT_ON_CONNECTED);
    emit(client, HTTP_EVENT_ON_DATA, body);
}

std::string catalog_candidates(const lyrics::Track& track)
{
    JsonDocument doc;
    auto entry = doc.to<JsonArray>().add<JsonObject>();
    entry["id"] = 42; entry["trackName"] = track.title;
    entry["artistName"] = track.artist; entry["albumName"] = track.album;
    entry["duration"] = track.duration_ms / 1000.0;
    entry["instrumental"] = false; entry["hasSyncedLyrics"] = true;
    std::string result; serializeJson(doc, result); return result;
}

void fallback_pipeline()
{
    reset(); auto request = ready();
    perform_action = [](FakeHttpClient& client) { reply(client, 200, body()); return ESP_OK; };
    auto result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::Synced && perform_calls == 1 && cleanup_calls == 1,
          "exact match ends the pipeline without querying other catalogs");

    reset(); request = ready();
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1) {
            check(client.url.find("album_name=") != std::string::npos, "first signature retains album");
            reply(client, 404, "");
        } else if (perform_calls == 2) {
            check(client.url.find("https://lrclib.net/api/v2/search?") == 0, "compact catalog precedes alternate signatures");
            reply(client, 200, "[]");
        } else {
            check(perform_calls == 3 && client.url.find("album_name=") == std::string::npos,
                  "third signature omits album after compact catalog miss");
            JsonDocument doc; deserializeJson(doc, body()); doc["albumName"] = "Another publication";
            std::string text; serializeJson(doc, text); reply(client, 200, text);
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::Synced && perform_calls == 3 && result.elapsed_ms >= 500,
          "same recording on a different album recovers after paced retry");

    reset(); request = ready();
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1 || perform_calls == 3) reply(client, 404, "");
        else if (perform_calls == 2) reply(client, 200, "[]");
        else if (perform_calls == 4) {
            check(client.url.find("https://music.163.com/api/search/get?") == 0, "independent NetEase search endpoint");
            reply(client, 200, netease_candidates());
        } else {
            check(perform_calls == 5 && client.url.find("id=42") != std::string::npos,
                  "only the validated candidate ID is downloaded");
            reply(client, 200, netease_lyrics());
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::Synced && result.lines && result.lines->size() == 2 && perform_calls == 5,
          "independent source recovers and prevents further fallback");
    check(std::find(headers.begin(), headers.end(), std::make_pair(std::string("Referer"), std::string("https://music.163.com/"))) != headers.end(),
          "public NetEase client origin without credentials");

    reset(); request = ready();
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1 || perform_calls == 3) reply(client, 404, "");
        else if (perform_calls == 2) reply(client, 200, "[]");
        else if (perform_calls == 4) reply(client, 200, netease_candidates(false));
        else if (perform_calls == 5) {
            check(client.url.find("https://lyrics.kugou.com/search?") == 0,
                  "mismatched artist never causes NetEase lyric download");
            reply(client, 200, R"({"status":200,"candidates":[{"id":"73","song":"Original Test","singer":"Botifier","duration":60000,"accesskey":"0123456789abcdef0123456789abcdef"}]})");
        } else {
            check(perform_calls == 6 && client.url.find("id=73") != std::string::npos,
                  "KuGou download uses validated candidate");
            reply(client, 200, R"({"status":200,"content":"WzAwOjAxLjAwXU9yaWdpbmFsIHdpbmRvdyBwaHJhc2UKWzAwOjA0LjAwXUEgc2Vjb25kIG9yaWdpbmFsIHBocmFzZQ=="})");
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::Synced && result.lines && perform_calls == 6,
          "validated KuGou LRC is the final independent fallback");

    reset();
    const lyrics::Track traditional{"Spotify", reinterpret_cast<const char*>(u8"風雲決"),
        reinterpret_cast<const char*>(u8"任賢齊"), reinterpret_cast<const char*>(u8"齊待"), 287947};
    request = ready(traditional);
    perform_action = [&](FakeHttpClient& client) {
        if (perform_calls == 1 || perform_calls == 3) {
            JsonDocument plain; deserializeJson(plain, body(traditional)); plain["syncedLyrics"] = nullptr;
            std::string encoded; serializeJson(plain, encoded); reply(client, 200, encoded);
        } else if (perform_calls == 2) reply(client, 200, "[]");
        else if (perform_calls == 4) reply(client, 200, R"({"code":200,"result":{"songCount":0}})");
        else if (perform_calls == 5) {
            JsonDocument search; search["status"] = 200;
            auto items = search["candidates"].to<JsonArray>();
            for (unsigned i = 0; i < 3; ++i) {
                auto item = items.add<JsonObject>();
                item["id"] = std::to_string(42 + i); item["duration"] = 287973;
                item["song"] = reinterpret_cast<const char*>(u8"风云决");
                item["singer"] = reinterpret_cast<const char*>(u8"任贤齐");
                item["accesskey"] = "public_test_key"; item["score"] = i == 2 ? 100 : 60 - i * 10;
                item["language"] = reinterpret_cast<const char*>(i == 2 ? u8"伴奏" : u8"国语");
            }
            std::string encoded; serializeJson(search, encoded); reply(client, 200, encoded);
        } else {
            check(perform_calls == 6 && client.url.find("id=42&") != std::string::npos,
                  "traditional phone metadata selects scored vocal lyrics without another search");
            reply(client, 200, R"({"status":200,"content":"WzAwOjAxLjAwXU9yaWdpbmFsIHdpbmRvdyBwaHJhc2UKWzAwOjA0LjAwXUEgc2Vjb25kIG9yaWdpbmFsIHBocmFzZQ=="})");
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::Synced && result.lines && result.lines->size() == 2 && perform_calls == 6,
          "plain-only LRCLIB plus script-different scored fallback recovers through full production pipeline");
    check(lyrics::engine.complete(request, result, clock_ms) && lyrics::snapshot().track.title == traditional.title,
          "recovered lyrics publish without changing original phone metadata");

    reset(); request = ready();
    perform_action = [](FakeHttpClient& client) {
        reply(client, 404, ""); lyrics::on_track_invalidated(); return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.error == ESP_ERR_TIMEOUT && perform_calls == 1 && !lyrics::engine.complete(request, result, clock_ms),
          "track change during first request stops all fallbacks and cannot publish");

    reset(); request = ready();
    perform_action = [](FakeHttpClient& client) {
        clock_ms += 11250;
        reply(client, 404, "");
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.error == ESP_ERR_TIMEOUT && perform_calls == 4 && result.elapsed_ms >= 45000 && result.elapsed_ms < 46000,
          "one overall forty-five-second budget bounds the entire multi-source pipeline");

    reset(); request = ready();
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1) {
            client.status = 429;
            esp_http_client_event_t event{}; event.client = &client; event.event_id = HTTP_EVENT_ON_HEADER;
            event.user_data = client.config.user_data;
            char key[] = "Retry-After", value[] = "120";
            event.header_key = key; event.header_value = value;
            client.config.event_handler(&event);
        } else reply(client, 404, "");
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::NetworkError && result.http_status == 429 && result.retry_after_ms == 120000 && perform_calls == 3,
          "rate limit skips same-source retry and survives misses in independent catalogs");

    for (unsigned limited_at : {2u, 3u}) {
        reset(); request = ready();
        perform_action = [limited_at](FakeHttpClient& client) {
            if (perform_calls == 1) reply(client, 404, "");
            else if (perform_calls == 2 && limited_at == 3) reply(client, 200, catalog_candidates(song()));
            else if (perform_calls == limited_at) {
                client.status = 429;
                esp_http_client_event_t event{}; event.client = &client; event.event_id = HTTP_EVENT_ON_HEADER;
                event.user_data = client.config.user_data;
                char key[] = "Retry-After", value[] = "120";
                event.header_key = key; event.header_value = value;
                client.config.event_handler(&event);
            } else {
                check(client.url.find("https://lrclib.net/") != 0, "catalog 429 forbids alternate same-provider signatures");
                reply(client, 404, "");
            }
            return ESP_OK;
        };
        result = lyrics::fetch(request);
        check(result.state == lyrics::LyricState::NetworkError && result.http_status == 429 &&
              result.retry_after_ms == 120000 && perform_calls == limited_at + 2,
              "catalog search or download rate limit survives independent misses without same-provider retry");
    }

    reset(); auto actual = song(); actual.duration_ms = 293016;
    auto catalog = actual; catalog.duration_ms = 290000;
    request = ready(actual);
    perform_action = [&](FakeHttpClient& client) {
        if (perform_calls == 1) reply(client, 404, "");
        else if (perform_calls == 2) {
            check(client.url.find("https://lrclib.net/api/v2/search?") == 0 &&
                  client.url.find("album_name=Demo") != std::string::npos &&
                  client.url.find("duration=") == std::string::npos,
                  "compact same-album metadata search follows first exact miss");
            reply(client, 200, catalog_candidates(catalog));
        } else {
            check(perform_calls == 3 && client.url == "https://lrclib.net/api/get/42",
                  "compatible lyrics download uses the selected catalog ID");
            reply(client, 200, body(catalog));
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::Synced && result.lines && result.lines->size() == 2 &&
          perform_calls == 3 && result.elapsed_ms >= 500,
          "293-second phone metadata accepts verified catalog lyrics without independent-source roundtrips");

    for (const auto durations : {std::pair<std::uint32_t,std::uint32_t>{288763,311000}, {211452,226000}}) {
        reset(); auto phone = song(); phone.duration_ms = durations.first;
        auto unique = phone; unique.duration_ms = durations.second;
        const auto unique_request = ready(phone);
        perform_action = [&](FakeHttpClient& client) {
            check(client.url.find("https://lrclib.net/") == 0, "unique catalog recovery never starts an independent provider");
            if (perform_calls == 1) {
                check(client.url.find("/api/get?") != std::string::npos, "unique fallback follows the existing exact lookup");
                reply(client, 404, "");
            } else if (perform_calls == 2) {
                check(client.url.find("/api/v2/search?") != std::string::npos, "duration fallback reuses the existing bounded catalog request");
                reply(client, 200, catalog_candidates(unique));
            } else {
                check(perform_calls == 3 && client.url == "https://lrclib.net/api/get/42",
                      "unique identity downloads only its validated catalog ID");
                reply(client, 200, body(unique));
            }
            return ESP_OK;
        };
        auto recovered = lyrics::fetch(unique_request);
        check(recovered.state == lyrics::LyricState::Synced && recovered.lines && recovered.lines->size() == 2 &&
              recovered.lines->front().time_ms == 1000 && recovered.lines->back().time_ms == 4000 &&
              perform_calls == 3 && cleanup_calls == 3 && recovered.elapsed_ms >= 500,
              "both measured duration shapes recover within three paced requests and preserve original cue times");
        check(lyrics::engine.complete(unique_request, std::move(recovered), clock_ms) &&
              lyrics::snapshot().track.duration_ms == phone.duration_ms && lyrics::snapshot().lines,
              "catalog recovery publishes without replacing the phone duration or playback time base");
    }

    reset(); actual.album.clear(); request = ready(actual);
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1) reply(client, 404, "");
        else if (perform_calls == 2) reply(client, 200, R"({"code":200,"result":{"songs":[]}})");
        else {
            check(perform_calls == 3 && client.url.find("https://lyrics.kugou.com/search?") == 0,
                  "unknown album cannot reach the duration compatibility search");
            reply(client, 200, R"({"status":200,"candidates":[]})");
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::NotFound && perform_calls == 3,
          "duration discrepancy is never accepted without an album identity");

    reset(); actual.album = song().album; request = ready(actual);
    perform_action = [&](FakeHttpClient& client) {
        if (perform_calls == 1) reply(client, 404, "");
        else {
            reply(client, 200, catalog_candidates(catalog));
            lyrics::on_track_invalidated();
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.error == ESP_ERR_TIMEOUT && perform_calls == 2 &&
          !lyrics::engine.complete(request, result, clock_ms) && !lyrics::snapshot().lines,
          "track change during catalog search prevents its lyric download and publication");

    reset(); request = ready(actual);
    perform_action = [&](FakeHttpClient& client) {
        switch (perform_calls) {
        case 1: case 4: reply(client, 404, ""); break;
        case 2: reply(client, 200, catalog_candidates(catalog)); break;
        case 3: {
            JsonDocument doc; deserializeJson(doc, body(catalog)); doc["syncedLyrics"] = "";
            std::string text; serializeJson(doc, text); reply(client, 200, text); break;
        }
        case 5: {
            JsonDocument doc; deserializeJson(doc, netease_candidates());
            doc["result"]["songs"][0]["duration"] = actual.duration_ms;
            std::string text; serializeJson(doc, text); reply(client, 200, text); break;
        }
        case 6: reply(client, 200, R"({"code":200,"lrc":{"lyric":""}})"); break;
        case 7:
            reply(client, 200, R"({"status":200,"candidates":[{"id":"73","song":"Original Test","singer":"Botifier","duration":293016,"accesskey":"0123456789abcdef0123456789abcdef"}]})");
            break;
        default:
            check(perform_calls == 8, "all catalog stages remain bounded to eight HTTP requests");
            reply(client, 200, R"({"status":200,"content":"WzAwOjAxLjAwXU9yaWdpbmFsIHdpbmRvdyBwaHJhc2UKWzAwOjA0LjAwXUEgc2Vjb25kIG9yaWdpbmFsIHBocmFzZQ=="})"); break;
        }
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == lyrics::LyricState::Synced && perform_calls == 8 && cleanup_calls == 8 && result.elapsed_ms >= 1750,
          "all eight requests remain available when preferred catalog has no LRC");
}

void unsynced_pipeline()
{
    using lyrics::LyricState;
    for (int outcome = 0; outcome < 4; ++outcome) {
        reset(); auto request = ready();
        perform_action = [outcome](FakeHttpClient& client) {
            if (perform_calls == 1) {
                JsonDocument doc; deserializeJson(doc, body());
                doc["syncedLyrics"] = "[00:01.00]Original first phrase\n[01:20.00]Original final phrase";
                std::string text; serializeJson(doc, text); reply(client, 200, text);
            } else if (perform_calls == 2) {
                check(client.url.find("https://lrclib.net/api/v2/search?") == 0,
                      "text fallback still searches the catalog for a synchronized edition");
                reply(client, 200, "[]");
            } else if (perform_calls == 3) reply(client, 404, "");
            else if (perform_calls == 4) {
                if (outcome == 0) reply(client, 200, netease_candidates());
                else if (outcome == 1) reply(client, 503, "");
                else if (outcome == 2) reply(client, 200, "invalid-json");
                else {
                    reply(client, 429, "");
                    esp_http_client_event_t event{}; event.client = &client; event.event_id = HTTP_EVENT_ON_HEADER;
                    event.user_data = client.config.user_data;
                    char key[] = "Retry-After", value[] = "120";
                    event.header_key = key; event.header_value = value;
                    client.config.event_handler(&event);
                }
            } else if (outcome == 0) reply(client, 200, netease_lyrics());
            else reply(client, 200, R"({"status":200,"candidates":[]})");
            return ESP_OK;
        };
        auto result = lyrics::fetch(request);
        check(perform_calls == 5 && cleanup_calls == 5, "fallback keeps the original bounded source sequence");
        if (outcome == 0) {
            check(result.state == LyricState::Synced && result.lines && result.lines->back().time_ms == 4000,
                  "later synchronized source outranks earlier text fallback");
        } else {
            check(result.state == LyricState::Unsynced && result.lines && result.lines->size() == 2 &&
                  result.lines->back().time_ms == 80000 && result.http_status == 200 && result.error == 0,
                  "transport malformed response or miss cannot erase useful complete text");
            if (outcome == 3) check(result.retry_after_ms == 120000, "text fallback preserves provider rate limit");
        }
    }
    check(std::string(lyrics::state_text(LyricState::Unsynced)) == "unsynced_text", "diagnostics distinguish text from missing lyrics");
}

void retryable_failure_precedence()
{
    for (unsigned outcome = 0; outcome < 4; ++outcome) {
        reset(); const auto request = ready();
        perform_action = [outcome](FakeHttpClient& client) {
            if (perform_calls == 1 || (perform_calls == 3 && outcome == 2)) reply(client, 200, "invalid-json");
            else if ((perform_calls == 2 && outcome) || (perform_calls == 3 && outcome == 3)) {
                reply(client, 429, "");
                esp_http_client_event_t event{}; event.client = &client; event.event_id = HTTP_EVENT_ON_HEADER;
                event.user_data = client.config.user_data;
                char key[] = "Retry-After", short_wait[] = "60", long_wait[] = "120";
                event.header_key = key;
                event.header_value = outcome == 3 && perform_calls == 2 ? short_wait : long_wait;
                client.config.event_handler(&event);
            } else if (perform_calls == 2 || outcome == 1) reply(client, 503, "");
            else reply(client, 200, R"({"status":200,"candidates":[]})");
            return ESP_OK;
        };
        auto result = lyrics::fetch(request);
        check(result.state == lyrics::LyricState::NetworkError && perform_calls == 3 && !result.lines,
              "a retryable provider failure outranks earlier or later invalid responses when no lyrics succeeded");
        const auto retry = outcome ? 120000u : 30000u;
        check(result.retry_after_ms == (outcome ? 120000u : 0u),
              "failure precedence preserves the maximum Retry-After from all attempted providers");
        check(lyrics::engine.complete(request, std::move(result), clock_ms), "retryable combined failure publishes");
        lyrics::protocol::Request retry_request;
        check(!lyrics::engine.take_request(clock_ms + retry - 1, retry_request) &&
              lyrics::engine.take_request(clock_ms + retry, retry_request),
              "combined failure retains one correctly delayed automatic retry");
    }
}

void phone_metadata_pipeline()
{
    using lyrics::LyricState;
    auto phone = song(); phone.player = reinterpret_cast<const char*>(u8"网易云音乐");
    phone.artist += '/' + phone.title; phone.title = "Original window phrase";
    reset(); auto request = ready(phone);
    check(request.phone_metadata && !lyrics::snapshot().phone_lyrics, "composite input is provisional before source proof");
    lyrics::on_attribute(0, 1, "1,1,4.1");
    perform_action = [&](FakeHttpClient& client) {
        if (perform_calls == 1) {
            check(client.url.find("https://music.163.com/api/search/get?") == 0, "phone candidate checks its source first");
            phone.title = "A second original phrase";
            lyrics::on_track_snapshot(phone);
            check(lyrics::engine.current(request), "phone title change keeps provisional lookup alive");
            reply(client, 200, netease_candidates());
        } else {
            check(perform_calls == 2 && client.url.find("/api/song/lyric?id=42") != std::string::npos,
                  "verified composite input downloads only its selected source ID");
            reply(client, 200, netease_lyrics());
        }
        return ESP_OK;
    };
    auto result = lyrics::fetch(request);
    check(result.phone_identity && result.state == LyricState::Synced && perform_calls == 2,
          "source-verified phone identity travels with its successful timeline");
    check(lyrics::engine.complete(request, result, clock_ms), "source proof permits completion across raw title changes");
    const auto published = lyrics::snapshot();
    check(published.phone_lyrics && published.track.title == phone.title && published.position_ms >= 4100,
          "verified result preserves actual raw title and playback anchor");
    phone.title = "Original window phrase"; clock_ms += 500; lyrics::on_track_snapshot(phone);
    check(lyrics::snapshot().generation == published.generation && lyrics::snapshot().lines == published.lines &&
          lyrics::snapshot().position_ms == published.position_ms + 500,
          "subsequent phone lyric titles retain the timeline and continuously advancing clock");
    lyrics::on_link(lyrics::LinkState::Pairing); lyrics::on_link(lyrics::LinkState::Ready);
    lyrics::on_track_snapshot(phone);
    check(!lyrics::snapshot().lines && !lyrics::snapshot().phone_lyrics,
          "compound success is not restored from an unverified raw-title cache key");

    reset(); phone.title = song().title; request = ready(phone);
    perform_action = [](FakeHttpClient& client) {
        reply(client, 200, perform_calls == 1 ? netease_candidates() : netease_lyrics()); return ESP_OK;
    };
    result = lyrics::fetch(request);
    const auto waiting_lines = result.lines;
    check(result.phone_identity && lyrics::engine.complete(request, result, clock_ms) &&
          !lyrics::snapshot().phone_lyrics && !lyrics::snapshot().lines && lyrics::snapshot().state == LyricState::Loading,
          "source metadata alone does not expose intro titles as verified phone lyrics");
    lyrics::on_attribute(0, 1, "1,1,1.2");
    phone.title = "Original window phrase"; lyrics::on_track_snapshot(phone);
    check(lyrics::snapshot().phone_lyrics && lyrics::snapshot().lines == waiting_lines && perform_calls == 2,
          "first real phone lyric verifies the already downloaded timeline without a second lookup");

    reset(); phone.title = "Actual collaborative song"; request = ready(phone);
    const auto probe_started = clock_ms;
    perform_action = [](FakeHttpClient& client) {
        reply(client, 200, perform_calls == 1 ? netease_candidates() : netease_lyrics()); return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(lyrics::engine.complete(request, result, clock_ms) && !lyrics::snapshot().lines,
          "a slash-containing artist collision stays hidden without active-cue title evidence");
    phone.title = "Another collaborative song"; lyrics::on_track_snapshot(phone);
    check(!lyrics::snapshot().lines && !lyrics::snapshot().phone_lyrics,
          "an unrelated real title does not verify the provisional compound recording");
    clock_ms = probe_started + 45000;
    check(!lyrics::engine.take_request(clock_ms, request) && !lyrics::snapshot().lines,
          "pending metadata proof expires at the original probe deadline without publishing stale lyrics");
    clock_ms += lyrics::protocol::kMetadataSettleMs;
    check(lyrics::engine.take_request(clock_ms, request) && !request.phone_metadata && request.track.title == phone.title,
          "expired title proof schedules the current ordinary metadata once with probing rejected");

    reset(); phone.title = song().title; request = ready(phone);
    const auto offline_deadline = clock_ms + 45000;
    lyrics::on_attribute(0, 1, "0,1,0");
    perform_action = [](FakeHttpClient& client) {
        reply(client, 200, perform_calls == 1 ? netease_candidates() : netease_lyrics()); return ESP_OK;
    };
    result = lyrics::fetch(request);
    std::weak_ptr<const std::vector<lyrics::Line>> held_lines = result.lines;
    check(lyrics::engine.complete(request, std::move(result), clock_ms) && !held_lines.expired(),
          "pending title proof retains only the existing downloaded timeline");
    wifi_connected = false;
    const auto last_event = lyrics::snapshot().last_event_ms;
    const auto notifications = notify_calls;
    clock_ms = offline_deadline - 1; run_one_worker_iteration();
    check(lyrics::snapshot().state == LyricState::Loading && perform_calls == 2 && notify_calls == notifications,
          "offline paused worker keeps pending proof only until its original deadline without BLE events or requests");
    clock_ms = offline_deadline; run_one_worker_iteration();
    const auto expired = lyrics::snapshot();
    check(expired.state == LyricState::Waiting && !expired.lines && !expired.phone_lyrics && held_lines.expired() &&
          !expired.playing && expired.position_ms == 0 && expired.last_event_ms == last_event &&
          perform_calls == 2 && notify_calls == notifications,
          "offline worker expires and releases pending proof at forty-five seconds without notifications or playback activity");
    clock_ms += lyrics::protocol::kMetadataSettleMs; wifi_connected = true;
    perform_action = [&](FakeHttpClient& client) {
        check(client.url.find("https://lrclib.net/api/get?") == 0, "network recovery uses ordinary lookup after offline probe rejection");
        reply(client, 200, body(phone)); return ESP_OK;
    };
    run_one_worker_iteration();
    check(perform_calls == 3 && lyrics::snapshot().state == LyricState::Synced && !lyrics::snapshot().phone_lyrics,
          "network recovery performs one normal lookup instead of restarting the expired probe");

    reset(); auto ordinary = song(); ordinary.player = phone.player; ordinary.artist = "AC/DC";
    request = ready(ordinary);
    perform_action = [&](FakeHttpClient& client) {
        if (perform_calls == 1) {
            JsonDocument doc; deserializeJson(doc, netease_candidates());
            doc["result"]["songs"][0]["artists"][0]["name"] = ordinary.artist;
            std::string encoded; serializeJson(doc, encoded);
            ordinary.title = "Another real song"; lyrics::on_track_snapshot(ordinary);
            reply(client, 200, encoded);
        } else reply(client, 200, netease_lyrics());
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(result.state == LyricState::Synced && !result.phone_identity,
          "ordinary slash-containing artist does not become composite-source proof");
    check(!lyrics::engine.complete(request, result, clock_ms) && !lyrics::snapshot().lines,
          "normal response cannot publish for a title changed during provisional lookup");
    clock_ms += lyrics::protocol::kMetadataSettleMs;
    check(lyrics::engine.take_request(clock_ms, request) && !request.phone_metadata && request.track.title == ordinary.title,
          "failed provisional identity retries latest metadata through the normal path");
    const auto before_normal = perform_calls;
    perform_action = [&](FakeHttpClient& client) {
        check(client.url.find("https://lrclib.net/api/get?") == 0, "rejected probe does not repeat source-first probing");
        reply(client, 200, body(ordinary)); return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(perform_calls == before_normal + 1 && lyrics::engine.complete(request, result, clock_ms),
          "latest ordinary song completes one normal lookup");
    ordinary.title = "A third real song"; lyrics::on_track_snapshot(ordinary);
    clock_ms += lyrics::protocol::kMetadataSettleMs;
    check(lyrics::engine.take_request(clock_ms, request) && !request.phone_metadata,
          "same stable key stays rejected across further title changes");
    ordinary.artist = phone.artist; lyrics::on_track_snapshot(ordinary);
    clock_ms += lyrics::protocol::kMetadataSettleMs;
    check(lyrics::engine.take_request(clock_ms, request) && request.phone_metadata,
          "a changed stable identity permits a new bounded source probe");

    reset(); request = ready(phone);
    lyrics::on_attribute(1, 0, "1");
    perform_action = [](FakeHttpClient& client) {
        lyrics::on_attribute(1, 0, "2");
        reply(client, 200, netease_candidates()); return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(perform_calls == 1 && result.error == ESP_ERR_TIMEOUT &&
          !lyrics::engine.complete(request, result, clock_ms) && !lyrics::snapshot().lines,
          "real queue change cancels even a source-verified provisional request before download");

    reset(); request = ready(phone);
    unsigned source_searches = 0;
    perform_action = [&](FakeHttpClient& client) {
        if (client.url.find("music.163.com") != std::string::npos) {
            ++source_searches; reply(client, 200, R"({"code":200,"result":{"songCount":0}})");
        } else if (client.url.find("/api/v2/search?") != std::string::npos) reply(client, 200, "[]");
        else if (client.url.find("lyrics.kugou.com") != std::string::npos)
            reply(client, 200, R"({"status":200,"candidates":[]})");
        else reply(client, 404, "");
        return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(source_searches == 1 && perform_calls == 5 && !result.phone_identity,
          "unsuccessful phone probe resumes remaining providers without repeating NetEase");

    reset(); request = ready(phone);
    perform_action = [](FakeHttpClient& client) {
        clock_ms += 45001; reply(client, 200, netease_candidates()); return ESP_OK;
    };
    result = lyrics::fetch(request);
    check(perform_calls == 1 && result.error == ESP_ERR_TIMEOUT && !result.phone_identity,
          "phone source-first path retains the overall forty-five second deadline");
}

std::string equivalent_ids(bool catalog)
{
    JsonDocument doc; deserializeJson(doc, catalog ? catalog_candidates(song()) : netease_candidates());
    auto items = catalog ? doc.as<JsonArray>() : doc["result"]["songs"].as<JsonArray>();
    items.add(items[0]); items[1]["id"] = 43;
    std::string result; serializeJson(doc, result); return result;
}

void rejected_phone_identity_stays_rejected()
{
    using lyrics::LyricState;
    reset(); auto phone = song(); phone.player = reinterpret_cast<const char*>(u8"网易云音乐");
    phone.artist += '/' + phone.title; phone.title = "Unrelated ordinary title";
    auto request = ready(phone); const auto deadline = clock_ms + 45000;
    perform_action = [](FakeHttpClient& client) {
        reply(client, 200, perform_calls == 1 ? netease_candidates() : netease_lyrics()); return ESP_OK;
    };
    auto response = lyrics::fetch(request);
    check(lyrics::engine.complete(request, response, clock_ms) && !lyrics::snapshot().lines,
          "unrelated title leaves source-proven candidate awaiting title evidence");
    clock_ms = deadline; lyrics::engine.advance(clock_ms);
    clock_ms += lyrics::protocol::kMetadataSettleMs;
    check(lyrics::engine.take_request(clock_ms, request) && !request.phone_metadata,
          "title-proof timeout creates an explicitly ordinary request");
    unsigned compound_searches = 0;
    const auto before = perform_calls;
    perform_action = [&](FakeHttpClient& client) {
        if (client.url.find("/api/search/get?") != std::string::npos) {
            ++compound_searches; reply(client, 200, netease_candidates());
        } else if (client.url.find("/api/v2/search?") != std::string::npos) reply(client, 200, "[]");
        else if (client.url.find("lyrics.kugou.com") != std::string::npos)
            reply(client, 200, R"({"status":200,"candidates":[]})");
        else {
            check(client.url.find("/api/song/lyric?") == std::string::npos,
                  "rejected ordinary request never downloads a compound-only search candidate");
            reply(client, 404, "");
        }
        return ESP_OK;
    };
    auto missing = lyrics::fetch(request);
    check(compound_searches == 1 && perform_calls == before + 5 && missing.state == LyricState::NotFound &&
          !missing.phone_identity && lyrics::engine.complete(request, missing, clock_ms),
          "a repeated compound source candidate cannot restart the rejected lookup mode");
    const auto generation = lyrics::snapshot().generation;
    clock_ms += 45000; lyrics::engine.advance(clock_ms);
    check(lyrics::snapshot().state == LyricState::NotFound && lyrics::snapshot().generation == generation &&
          !lyrics::engine.take_request(clock_ms, request),
          "ordinary miss stays visible without another awaiting-title timeout or fetch");
    response.phone_identity = true;
    check(lyrics::engine.complete(request, std::move(response), clock_ms) &&
          lyrics::snapshot().state == LyricState::Invalid && !lyrics::snapshot().lines && !lyrics::snapshot().phone_lyrics,
          "injected phone proof on an ordinary request is invalid instead of entering awaiting-title");
    clock_ms += 45000; lyrics::engine.advance(clock_ms);
    check(lyrics::snapshot().state == LyricState::Invalid && lyrics::snapshot().generation == generation &&
          !lyrics::engine.take_request(clock_ms, request),
          "unexpected ordinary response proof cannot create another forty-five second loop");
}

void phone_network_retry_budget()
{
    using lyrics::LyricState;
    for (const int failure_status : {503, 429}) {
        reset(); auto phone = song(); phone.player = reinterpret_cast<const char*>(u8"网易云音乐");
        phone.artist += '/' + phone.title; phone.title = "Original window phrase";
        auto request = ready(phone);
        lyrics::on_attribute(0, 1, "0,1,1.2");
        const auto generation = request.generation;
        perform_action = [&](FakeHttpClient& client) {
            if (perform_calls == 1) {
                phone.title = "A second original phrase"; lyrics::on_track_snapshot(phone);
                check(lyrics::engine.current(request), "raw title change does not cancel the first network failure attempt");
            }
            const bool limited = failure_status == 429 && client.url.find("music.163.com") != std::string::npos;
            reply(client, limited ? 429 : 503, "");
            if (limited) {
                esp_http_client_event_t event{}; event.client = &client; event.event_id = HTTP_EVENT_ON_HEADER;
                event.user_data = client.config.user_data; char key[] = "Retry-After", value[] = "120";
                event.header_key = key; event.header_value = value; client.config.event_handler(&event);
            }
            return ESP_OK;
        };
        auto response = lyrics::fetch(request);
        check(response.state == LyricState::NetworkError && perform_calls == 3 &&
              lyrics::engine.complete(request, response, clock_ms) && lyrics::snapshot().generation == generation,
              "503 or 429 keeps the compound probe identity and its attempt count across title changes");
        const auto retry_delay = failure_status == 429 ? 120000u : 30000u;
        const auto retry_at = clock_ms + retry_delay;
        phone.title = "Original window phrase"; lyrics::on_track_snapshot(phone);
        clock_ms = retry_at - 1;
        check(!lyrics::engine.take_request(clock_ms, request), "raw title updates do not bypass Retry-After or minimum retry delay");
        ++clock_ms;
        check(lyrics::engine.take_request(clock_ms, request) && request.phone_metadata && request.generation == generation,
              "retry remains a source-verifiable probe rather than a rejected ordinary request");
        const auto failures = perform_calls;
        perform_action = [failures](FakeHttpClient& client) {
            reply(client, 200, perform_calls == failures + 1 ? netease_candidates() : netease_lyrics()); return ESP_OK;
        };
        response = lyrics::fetch(request);
        check(response.phone_identity && lyrics::engine.complete(request, std::move(response), clock_ms) &&
              lyrics::snapshot().phone_lyrics && lyrics::snapshot().lines && perform_calls == failures + 2,
              "source and active-cue proof recover after a bounded retry without losing phone lyric mode");

        reset(); phone.title = "Original window phrase"; request = ready(phone);
        const auto exhausted_generation = request.generation;
        perform_action = [failure_status](FakeHttpClient& client) {
            reply(client, failure_status, "");
            if (failure_status == 429) {
                esp_http_client_event_t event{}; event.client = &client; event.event_id = HTTP_EVENT_ON_HEADER;
                event.user_data = client.config.user_data; char key[] = "Retry-After", value[] = "120";
                event.header_key = key; event.header_value = value; client.config.event_handler(&event);
            }
            return ESP_OK;
        };
        for (unsigned attempt = 1; attempt <= 3; ++attempt) {
            phone.title = "Changing raw title " + std::to_string(attempt); lyrics::on_track_snapshot(phone);
            response = lyrics::fetch(request);
            check(response.state == LyricState::NetworkError && lyrics::engine.complete(request, response, clock_ms) &&
                  lyrics::snapshot().generation == exhausted_generation,
                  "each failed probe preserves generation despite a changed raw title");
            clock_ms += retry_delay - 1;
            check(!lyrics::engine.take_request(clock_ms, request), "each failed probe observes its full retry delay");
            ++clock_ms;
            const bool taken = lyrics::engine.take_request(clock_ms, request);
            check(taken == (attempt < 3) && (!taken || request.phone_metadata),
                  "only the first two failures allow another source-verifiable request");
        }
        phone.title = "Yet another raw title"; lyrics::on_track_snapshot(phone);
        clock_ms += 180000; lyrics::engine.advance(clock_ms);
        check(!lyrics::engine.take_request(clock_ms, request) && perform_calls == 9 &&
              lyrics::snapshot().generation == exhausted_generation && lyrics::snapshot().state == LyricState::NetworkError,
              "three failed rounds exhaust the shared retry budget and later titles cannot start a fourth");
    }
}

void phone_identity_and_playback_boundaries()
{
    auto phone = song(); phone.player = reinterpret_cast<const char*>(u8"网易云音乐");
    phone.artist += '/' + phone.title;
    for (unsigned mode = 0; mode < 3; ++mode) for (unsigned field = 0; field < 5; ++field) {
        reset(); phone.title = mode == 2 ? "Original window phrase" : "Intro title";
        const auto request = ready(phone);
        lyrics::on_attribute(1, 0, "1");
        auto response = lyrics::protocol::parse_netease_lyrics(200, netease_lyrics(), phone);
        response.phone_identity = true;
        if (mode) {
            lyrics::on_attribute(0, 1, mode == 2 ? "0,1,1.2" : "0,1,0");
            check(lyrics::engine.complete(request, response, clock_ms) &&
                  lyrics::snapshot().phone_lyrics == (mode == 2),
                  "prepare either hidden title proof or verified phone timeline");
        }
        auto changed = phone;
        if (field == 0) changed.player = "Spotify";
        else if (field == 1) changed.artist += " another artist";
        else if (field == 2) changed.album += " another album";
        else if (field == 3) ++changed.duration_ms;
        else lyrics::on_attribute(1, 0, "2");
        if (field < 4) lyrics::on_track_snapshot(changed);
        const auto invalidated = lyrics::snapshot();
        check(invalidated.generation != request.generation && !invalidated.lines && !invalidated.phone_lyrics &&
              !lyrics::engine.current(request) && !lyrics::engine.complete(request, response, clock_ms),
              "player artist album duration or queue change invalidates probe pending and verified identities");
    }

    reset(); phone.title = "Original window phrase"; const auto request = ready(phone);
    lyrics::on_attribute(0, 1, "1,1,1.2");
    auto response = lyrics::protocol::parse_netease_lyrics(200, netease_lyrics(), phone);
    response.phone_identity = true;
    check(lyrics::engine.complete(request, std::move(response), clock_ms) && lyrics::snapshot().phone_lyrics,
          "prepare verified lyric clock for pause and seek");
    const auto initial = lyrics::snapshot();
    clock_ms += 500;
    check(lyrics::snapshot().position_ms == initial.position_ms + 500,
          "verified lyrics continue advancing without a new metadata event");
    lyrics::on_attribute(0, 1, "0,1,2"); clock_ms += 3000;
    check(!lyrics::snapshot().playing && lyrics::snapshot().position_ms == 2000 &&
          lyrics::snapshot().generation == initial.generation && lyrics::snapshot().lines == initial.lines,
          "pause freezes the clock without replacing identity or lyrics");
    lyrics::on_attribute(0, 1, "1,1,4.2"); phone.title = "A second original phrase";
    lyrics::on_track_snapshot(phone); clock_ms += 500;
    check(lyrics::snapshot().position_ms == 4700 && lyrics::snapshot().generation == initial.generation &&
          lyrics::snapshot().lines == initial.lines && lyrics::snapshot().phone_lyrics,
          "forward seek and matching phone title preserve the timeline and new clock anchor");
    lyrics::on_attribute(0, 1, "1,1,1.1"); clock_ms += 500;
    check(lyrics::snapshot().position_ms == 1600 && lyrics::snapshot().generation == initial.generation &&
          lyrics::snapshot().lines == initial.lines,
          "backward seek also keeps the same lyric generation and timeline");
    lyrics::on_attribute(1, 0, "1"); lyrics::on_attribute(1, 0, "1"); lyrics::on_track_snapshot(phone);
    lyrics::protocol::Request duplicate;
    check(lyrics::snapshot().generation == initial.generation && lyrics::snapshot().lines == initial.lines &&
          !lyrics::engine.take_request(clock_ms, duplicate),
          "duplicate queue and metadata events cannot refetch verified lyrics");
}

void equivalent_recording_fallback()
{
    using lyrics::LyricState;
    for (const bool catalog : {false, true}) for (unsigned primary = 0; primary < 3; ++primary) {
        reset(); const auto request = ready();
        perform_action = [catalog, primary](FakeHttpClient& client) {
            if (client.url.find("/api/v2/search?") != std::string::npos)
                reply(client, 200, catalog ? equivalent_ids(true) : "[]");
            else if (client.url.find("/api/search/get?") != std::string::npos) reply(client, 200, equivalent_ids(false));
            else if (client.url.find("/api/get/42") != std::string::npos || client.url.find("lyric?id=42") != std::string::npos) {
                if (primary == 0) reply(client, 404, "");
                else if (primary == 1) reply(client, 200, "invalid-json");
                else {
                    JsonDocument doc; deserializeJson(doc, catalog ? body() : netease_lyrics());
                    const char* text = "[00:01.00]Original first phrase\n[01:20.00]Original final phrase";
                    if (catalog) doc["syncedLyrics"] = text; else doc["lrc"]["lyric"] = text;
                    std::string encoded; serializeJson(doc, encoded); reply(client, 200, encoded);
                }
            }
            else if (client.url.find("/api/get/43") != std::string::npos) {
                JsonDocument doc; deserializeJson(doc, body()); doc["id"] = 43;
                std::string encoded; serializeJson(doc, encoded); reply(client, 200, encoded);
            } else if (client.url.find("lyric?id=43") != std::string::npos) reply(client, 200, netease_lyrics());
            else reply(client, 404, "");
            return ESP_OK;
        };
        const auto result = lyrics::fetch(request);
        check(result.state == LyricState::Synced && result.lines && perform_calls == (catalog ? 4u : 6u),
              "missing invalid or unsynced primary downloads exactly one source-proven equivalent ID");
    }

    reset(); auto phone = song(); phone.player = reinterpret_cast<const char*>(u8"网易云音乐");
    phone.artist += '/' + phone.title; phone.title = "Original window phrase";
    const auto request = ready(phone);
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1) reply(client, 200, equivalent_ids(false));
        else if (perform_calls == 2) reply(client, 200,
            R"({"code":200,"lrc":{"lyric":"[00:01.00]Original first phrase\n[01:20.00]Original final phrase"}})");
        else if (perform_calls == 3) reply(client, 200, netease_lyrics());
        else check(false, "successful equivalent phone recording ends lookup");
        return ESP_OK;
    };
    auto result = lyrics::fetch(request);
    check(result.phone_identity && result.state == LyricState::Synced && perform_calls == 3,
          "equivalent source ID preserves phone proof and synced lyrics outrank the first text fallback");

    reset(); const auto text_only = ready(phone);
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1) reply(client, 200, equivalent_ids(false));
        else if (perform_calls == 2) reply(client, 200,
            R"({"code":200,"lrc":{"lyric":"[00:01.00]Original first phrase\n[01:20.00]Original final phrase"}})");
        else if (perform_calls == 3) reply(client, 200, "invalid-json");
        else if (client.url.find("/api/v2/search?") != std::string::npos) reply(client, 200, "[]");
        else if (client.url.find("lyrics.kugou.com") != std::string::npos)
            reply(client, 200, R"({"status":200,"candidates":[]})");
        else reply(client, 404, "");
        return ESP_OK;
    };
    result = lyrics::fetch(text_only);
    check(result.phone_identity && result.state == LyricState::Unsynced && result.lines &&
          result.lines->back().time_ms == 80000 && perform_calls == 7,
          "failed equivalent and later providers preserve the first complete text and its source proof");

    reset(); const auto stale = ready(phone);
    perform_action = [](FakeHttpClient& client) {
        if (perform_calls == 1) reply(client, 200, equivalent_ids(false));
        else { reply(client, 404, ""); lyrics::on_track_invalidated(); }
        return ESP_OK;
    };
    result = lyrics::fetch(stale);
    check(perform_calls == 2 && result.error == ESP_ERR_TIMEOUT && !lyrics::engine.complete(stale, result, clock_ms),
          "generation change prevents both equivalent-ID download and stale result publication");

    reset(); const auto bounded = ready();
    perform_action = [](FakeHttpClient& client) {
        if (client.url.find("/api/v2/search?") != std::string::npos) reply(client, 200, equivalent_ids(true));
        else if (client.url.find("/api/search/get?") != std::string::npos) reply(client, 200, equivalent_ids(false));
        else reply(client, 404, "");
        return ESP_OK;
    };
    result = lyrics::fetch(bounded);
    check(perform_calls == 8 && cleanup_calls == 8,
          "equivalent downloads share the original eight-request budget without an extra provider retry");
}

void successful_cache_reuse()
{
    reset(); const auto request = ready();
    perform_action = [](FakeHttpClient& client) { reply(client, 200, body()); return ESP_OK; };
    auto response = lyrics::fetch(request);
    check(lyrics::engine.complete(request, std::move(response), clock_ms), "service successful response publishes and caches");
    const auto shared = lyrics::snapshot().lines;
    lyrics::on_track_snapshot(song());
    check(lyrics::snapshot().track_ready && lyrics::snapshot().lines == shared, "same atomic metadata preserves ready cached presentation");
    lyrics::protocol::Request duplicate;
    check(!lyrics::engine.take_request(clock_ms + 1000, duplicate) && perform_calls == 1, "successful metadata does not schedule duplicate HTTP");
    lyrics::on_link(lyrics::LinkState::Pairing);
    check(!lyrics::snapshot().lines, "service disconnect clears current display");
    lyrics::on_link(lyrics::LinkState::Ready);
    lyrics::on_track_snapshot(song());
    check(!wifi_connected && lyrics::snapshot().track_ready && lyrics::snapshot().lines == shared,
          "same full identity restores from RAM even when Wi-Fi is unavailable");
    run_one_worker_iteration();
    check(perform_calls == 1 && lyrics::snapshot().lines == shared, "single worker leaves cached lyrics intact without network I/O");
    auto next = song(); next.title = "Another song";
    lyrics::on_track_snapshot(next);
    check(!lyrics::snapshot().lines, "new song never displays previous cache entry");
    lyrics::on_track_snapshot(song());
    check(lyrics::snapshot().track_ready && lyrics::snapshot().lines == shared &&
          !lyrics::engine.take_request(clock_ms + 1000, duplicate), "returning song restores without a new fetch");
    lyrics::set_enabled(false);
    check(!lyrics::snapshot().lines, "disable hides cached result immediately");
    lyrics::set_enabled(true); lyrics::on_link(lyrics::LinkState::Ready);
    check(!lyrics::snapshot().lines, "re-enable needs current metadata before cache lookup");
    lyrics::on_track_snapshot(song());
    check(lyrics::snapshot().lines == shared && perform_calls == 1, "re-enable exact signature reuses success without stale display");
}

void worker_commands_and_storage()
{
    reset();
    check(lyrics::start() == ESP_OK && lyrics::start() == ESP_OK && task_creates == 1, "one process-wide worker");
    check(task_stack == 8192 && task_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) && task_priority == 1 && task_core == 0, "internal low-priority task stack");
    check(!nvs_calls && !ble_calls && !perform_calls, "start queues task without blocking I/O");
    lyrics::set_enabled(true); lyrics::forget_phone();
    check(!nvs_calls && !ble_calls && !forget_calls && !perform_calls && notify_calls == 2, "UI callbacks only update/notify");
    run_one_worker_iteration();
    check(ble_calls == 1 && forget_calls == 1 && nvs_calls > 0 && stored_enabled, "worker alone performs BLE and persists enable");

    reset(); stored_enabled = true;
    run_one_worker_iteration();
    check(lyrics::snapshot().enabled && ble_calls == 1, "saved enable restored on worker startup");

    reset(); stored_enabled = true; lyrics::set_enabled(false);
    run_one_worker_iteration();
    check(!lyrics::snapshot().enabled && !stored_enabled, "explicit UI command wins over saved enable");

    reset(); lyrics::set_enabled(true); nvs_commit_error = 0x1101;
    run_one_worker_iteration();
    check(lyrics::snapshot().last_error == nvs_commit_error, "NVS commit failure observable in snapshot");

    reset(); nvs_open_error = 0x1101;
    run_one_worker_iteration();
    check(lyrics::snapshot().last_error == nvs_open_error, "NVS load/open failure observable");

    reset(); lyrics::set_enabled(true); ble_error = ESP_FAIL;
    run_one_worker_iteration();
    check(lyrics::snapshot().link == lyrics::LinkState::Error && lyrics::snapshot().last_error == ESP_FAIL, "BLE startup failure observable");

    reset(); ready();
    lyrics::set_enabled(false);
    check(!lyrics::snapshot().enabled && !lyrics::snapshot().lines && !ble_calls && !nvs_calls, "disable invalidates immediately while transport command is queued");
}
} // namespace

std::int64_t esp_timer_get_time() { return static_cast<std::int64_t>(clock_ms * 1000); }
esp_err_t esp_crt_bundle_attach(void*) { return ESP_OK; }
void vTaskDelay(std::uint32_t ticks) { clock_ms += ticks; }
std::uint32_t ulTaskNotifyTake(int, std::uint32_t) { throw StopWorker{}; }
void xTaskNotifyGive(TaskHandle_t) { ++notify_calls; }
int xTaskCreatePinnedToCoreWithCaps(TaskFunction_t task, const char*, unsigned stack, void*, unsigned priority,
                                  TaskHandle_t* handle, int core, unsigned caps)
{
    ++task_creates; created_task = task; task_stack = stack; task_priority = priority; task_core = core; task_caps = caps;
    *handle = reinterpret_cast<void*>(1); return pdPASS;
}
namespace codex_weekly { Snapshot snapshot() { return {wifi_connected}; } }
extern "C" int botifier_ams_set_enabled(bool) { ++ble_calls; return ble_error; }
extern "C" void botifier_ams_forget_phone() { ++forget_calls; }
esp_err_t nvs_open(const char*, int, nvs_handle_t* handle) { ++nvs_calls; *handle = 1; return nvs_open_error; }
esp_err_t nvs_get_u8(nvs_handle_t, const char*, std::uint8_t* value) { ++nvs_calls; *value = stored_enabled; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t, const char*, std::uint8_t value) { ++nvs_calls; stored_enabled = value != 0; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t) { ++nvs_calls; return nvs_commit_error; }
void nvs_close(nvs_handle_t) { ++nvs_calls; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config)
{
    auto* client = new FakeHttpClient; client->config = *config; client->url = config->url; return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char* key, const char* value) { headers.emplace_back(key, value); return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t client) { ++perform_calls; return perform_action ? perform_action(*client) : ESP_FAIL; }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { return client->status; }
int esp_http_client_get_errno(esp_http_client_handle_t client) { return client->socket_error; }
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t client, int* error, int* flags)
{
    *error = client->tls_error; *flags = client->tls_flags;
    client->tls_error = client->tls_flags = 0; return ESP_OK;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
    ++close_calls; client->closed = true;
    // Model the SDK's disconnect callback so recursive cancellation is caught.
    if (close_calls < 10) emit(*client, HTTP_EVENT_DISCONNECTED);
    return ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { ++cleanup_calls; delete client; return ESP_OK; }

int main()
{
    try {
        http_transport(); fallback_pipeline(); unsynced_pipeline(); retryable_failure_precedence(); phone_metadata_pipeline();
        rejected_phone_identity_stays_rejected(); phone_network_retry_budget(); phone_identity_and_playback_boundaries();
        equivalent_recording_fallback(); successful_cache_reuse(); worker_commands_and_storage();
        std::cout << "lyrics service: " << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "lyrics service failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
