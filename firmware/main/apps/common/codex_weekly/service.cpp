#include "service.h"
#include "protocol.h"
#include "control.h"
#include <apps/common/idle_program/service.h>
#include <apps/common/idle_program/policy.h>
#include <hal/hal.h>
#include <wifi_manager.h>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cinttypes>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace codex_weekly {
namespace {
constexpr char kClientId[] = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr char kUsercodeUrl[] = "https://auth.openai.com/api/accounts/deviceauth/usercode";
constexpr char kDeviceTokenUrl[] = "https://auth.openai.com/api/accounts/deviceauth/token";
constexpr char kTokenUrl[] = "https://auth.openai.com/oauth/token";
constexpr char kUsageUrl[] = "https://chatgpt.com/backend-api/wham/usage";
constexpr std::uint64_t kLoginMs = 15u * 60u * 1000u;
constexpr std::uint64_t kPollMs = 300000;
constexpr std::uint64_t kUnknownExpiryMs = 8ull * 24 * 60 * 60 * 1000;
std::mutex state_mutex;
detail::CommandGate commands;
Snapshot latest;
TaskHandle_t worker_task = nullptr;
std::uint64_t next_poll_at = 0, login_expires_at = 0, retry_not_before = 0;
std::uint32_t sequence = 0;

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }
std::uint32_t unix_now()
{
    const auto value = std::time(nullptr);
    return value >= 1577836800 && static_cast<std::uint64_t>(value) <= UINT32_MAX
        ? static_cast<std::uint32_t>(value) : 0;
}
void copy_text(char* output, std::size_t capacity, std::string_view value)
{
    const auto count = std::min(capacity - 1, value.size());
    std::memcpy(output, value.data(), count); output[count] = 0;
}
// Caller holds state_mutex. The idle seam takes only its own short policy lock;
// no code under that lock calls this service, HTTP, NVS or LVGL.
void publish_locked(const protocol::Usage* usage = nullptr)
{
    sequence = std::max(unix_now(), sequence + 1);
    idle_program::WeeklyUpdate update;
    update.sequence = sequence;
    if (usage) {
        const auto observed = unix_now();
        const auto ttl = usage->reset_at > observed ? std::min<std::uint64_t>(900000,
            static_cast<std::uint64_t>(usage->reset_at - observed) * 1000) : 0;
        // Existing policy requires at least a minute of valid lifetime.
        if (observed && ttl >= 60000) {
            update.available = true;
            update.remaining_percent = usage->remaining_percent;
            update.reset_at = usage->reset_at;
            update.observed_at = observed;
            update.ttl_ms = static_cast<std::uint32_t>(ttl);
        }
    }
    idle_program::publish_weekly(update);
}
bool current(std::uint32_t generation)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return commands.current(generation);
}

// A single blob prevents a refresh rotation from mixing tokens/accounts.
struct StoredHeader { std::uint32_t version; std::uint16_t refresh_size, account_size; };
bool safe_field(std::string_view value, std::size_t maximum)
{
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char c : value) if (c < 0x21 || c > 0x7e) return false;
    return true;
}
esp_err_t store_tokens(const protocol::Tokens* value)
{
    nvs_handle_t handle;
    auto error = nvs_open("sc_codex", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    if (!value) {
        error = nvs_erase_key(handle, "auth");
        if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    } else if (!safe_field(value->refresh_token, 3500) || !safe_field(value->account_id, 512) ||
               sizeof(StoredHeader) + value->refresh_token.size() + value->account_id.size() > 4096) {
        error = ESP_ERR_INVALID_SIZE;
    } else {
        StoredHeader header{1, static_cast<std::uint16_t>(value->refresh_token.size()),
                              static_cast<std::uint16_t>(value->account_id.size())};
        std::vector<std::uint8_t> blob(sizeof(header) + header.refresh_size + header.account_size);
        std::memcpy(blob.data(), &header, sizeof(header));
        std::memcpy(blob.data() + sizeof(header), value->refresh_token.data(), header.refresh_size);
        std::memcpy(blob.data() + sizeof(header) + header.refresh_size, value->account_id.data(), header.account_size);
        error = nvs_set_blob(handle, "auth", blob.data(), blob.size());
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}
esp_err_t load_tokens(protocol::Tokens& value)
{
    nvs_handle_t handle;
    auto error = nvs_open("sc_codex", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    std::size_t size = 0;
    error = nvs_get_blob(handle, "auth", nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return ESP_OK; }
    if (error != ESP_OK || size < sizeof(StoredHeader) || size > 4096) {
        nvs_close(handle); return ESP_ERR_INVALID_SIZE;
    }
    std::vector<std::uint8_t> blob(size);
    error = nvs_get_blob(handle, "auth", blob.data(), &size);
    nvs_close(handle);
    if (error != ESP_OK) return error;
    StoredHeader header{}; std::memcpy(&header, blob.data(), sizeof(header));
    if (header.version != 1 || sizeof(header) + header.refresh_size + header.account_size != size)
        return ESP_ERR_INVALID_SIZE;
    protocol::Tokens loaded;
    loaded.refresh_token.assign(reinterpret_cast<const char*>(blob.data() + sizeof(header)), header.refresh_size);
    loaded.account_id.assign(reinterpret_cast<const char*>(blob.data() + sizeof(header) + header.refresh_size), header.account_size);
    if (!safe_field(loaded.refresh_token, 3500) || !safe_field(loaded.account_id, 512)) return ESP_ERR_INVALID_RESPONSE;
    value = std::move(loaded);
    return ESP_OK;
}
bool load_debug_key()
{
    nvs_handle_t handle;
    if (nvs_open("sc_codex", NVS_READWRITE, &handle) != ESP_OK) return false;
    std::array<char, 33> key{}; std::size_t size = key.size();
    auto error = nvs_get_str(handle, "debug_key", key.data(), &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        std::array<std::uint8_t, 16> bytes{}; esp_fill_random(bytes.data(), bytes.size());
        constexpr char hex[] = "0123456789abcdef";
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            key[i * 2] = hex[bytes[i] >> 4]; key[i * 2 + 1] = hex[bytes[i] & 15];
        }
        error = nvs_set_str(handle, "debug_key", key.data());
        if (error == ESP_OK) error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error != ESP_OK || std::strlen(key.data()) != 32) return false;
    for (std::size_t i = 0; i < 32; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
    std::lock_guard<std::mutex> lock(state_mutex); latest.debug_key = key;
    return true;
}

struct Response {
    std::unique_ptr<char[]> data{new (std::nothrow) char[protocol::kMaxResponseBytes + 1]};
    std::size_t size = 0;
    bool overflow = false;
    std::uint64_t deadline_ms = now_ms() + 10000;
    int status = 0;
    Error error = Error::None;
    std::string_view body() const { return data ? std::string_view(data.get(), size) : std::string_view{}; }
};
enum class Endpoint { Usercode, LoginPoll, TokenExchange, Refresh, Usage };
const char* endpoint_text(Endpoint value)
{
    switch (value) {
    case Endpoint::Usercode: return "usercode";
    case Endpoint::LoginPoll: return "login_poll";
    case Endpoint::TokenExchange: return "token_exchange";
    case Endpoint::Refresh: return "refresh";
    case Endpoint::Usage: return "usage";
    }
    return "usage";
}
void emit_http(Endpoint endpoint, esp_err_t error, int status, int tls_error,
               int tls_flags, int socket_error, std::uint64_t started)
{
    // Only fixed endpoint names and numbers: never URLs, headers or bodies.
    std::printf("SC_CODEX_HTTP {\"v\":1,\"endpoint\":\"%s\",\"esp_error\":%d,"
                "\"http_status\":%d,\"tls_error\":%d,\"tls_flags\":%d,"
                "\"socket_errno\":%d,\"elapsed_ms\":%" PRIu32 "}\n",
                endpoint_text(endpoint), error, status >= 100 && status <= 599 ? status : 0,
                tls_error, tls_flags, socket_error,
                static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX, now_ms() - started)));
}
esp_err_t receive(esp_http_client_event_t* event)
{
    auto& response = *static_cast<Response*>(event->user_data);
    // DNS retries and TLS setup must not consume the response-read budget.
    if (event->event_id == HTTP_EVENT_ON_CONNECTED) response.deadline_ms = now_ms() + 10000;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        const auto count = static_cast<std::size_t>(event->data_len);
        if (!response.data || count > protocol::kMaxResponseBytes - response.size || now_ms() >= response.deadline_ms) {
            response.overflow = true;
            // IDF 5.5 ignores the ON_DATA callback's return value. Close the
            // transport as well, so an oversized/slow stream cannot occupy it.
            esp_http_client_close(event->client);
            return ESP_FAIL;
        }
        std::memcpy(response.data.get() + response.size, event->data, count);
        response.size += count; response.data[response.size] = 0;
    }
    return ESP_OK;
}
Response request(Endpoint endpoint, const char* url, const char* content_type, std::string_view body,
                 const protocol::Tokens* tokens = nullptr)
{
    const auto started = now_ms();
    Response response;
    if (!response.data) {
        response.error = Error::Internal;
        emit_http(endpoint, ESP_ERR_NO_MEM, 0, 0, 0, 0, started); return response;
    }
    esp_http_client_config_t config{};
    config.url = url; config.method = content_type ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    // IDF cannot split one oversized header: allow the complete Bearer/account
    // headers plus the request line, default headers and terminating CRLF.
    if (tokens) config.buffer_size_tx = static_cast<int>(tokens->access_token.size() + tokens->account_id.size() + 512);
    config.timeout_ms = 10000; config.disable_auto_redirect = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = receive; config.user_data = &response;
    auto client = esp_http_client_init(&config);
    if (!client) {
        response.error = Error::Internal;
        emit_http(endpoint, ESP_FAIL, 0, 0, 0, 0, started); return response;
    }
    auto setup_error = esp_http_client_set_header(client, "User-Agent", "Botifier/0.1");
    if (content_type) {
        if (setup_error == ESP_OK) setup_error = esp_http_client_set_header(client, "Content-Type", content_type);
        if (setup_error == ESP_OK) setup_error = esp_http_client_set_post_field(client, body.data(), static_cast<int>(body.size()));
    }
    const std::string authorization = tokens ? "Bearer " + tokens->access_token : std::string{};
    if (tokens) {
        if (setup_error == ESP_OK) setup_error = esp_http_client_set_header(client, "Authorization", authorization.c_str());
        if (setup_error == ESP_OK) setup_error = esp_http_client_set_header(client, "ChatGPT-Account-Id", tokens->account_id.c_str());
    }
    if (setup_error != ESP_OK) {
        emit_http(endpoint, setup_error, 0, 0, 0, 0, started);
        response.error = Error::Internal; esp_http_client_cleanup(client); return response;
    }
    const auto error = esp_http_client_perform(client);
    response.status = esp_http_client_get_status_code(client);
    const auto socket_error = esp_http_client_get_errno(client);
    int tls_error = 0, flags = 0;
    esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &flags);
    if (response.overflow) response.error = Error::InvalidResponse;
    else if (error != ESP_OK) {
        response.error = tls_error || flags ? Error::Tls : Error::Network;
    } else if (response.status == 429) response.error = Error::RateLimited;
    else if (response.status < 200 || response.status >= 300) response.error = Error::Http;
    emit_http(endpoint, error, response.status, tls_error, flags, socket_error, started);
    esp_http_client_cleanup(client);
    return response;
}
std::string json(std::initializer_list<std::pair<const char*, std::string_view>> values)
{
    auto* object = cJSON_CreateObject();
    if (!object) return {};
    for (const auto& item : values) {
        const std::string value(item.second);
        if (!cJSON_AddStringToObject(object, item.first, value.c_str())) { cJSON_Delete(object); return {}; }
    }
    auto* encoded = cJSON_PrintUnformatted(object);
    const std::string result = encoded ? encoded : "";
    cJSON_free(encoded); cJSON_Delete(object);
    return result;
}

void prepare_dns(esp_netif_t* netif)
{
    constexpr esp_netif_dns_type_t fill_types[]{ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK};
    constexpr std::uint32_t fill_addresses[]{ESP_IP4TOADDR(1, 1, 1, 1), ESP_IP4TOADDR(8, 8, 8, 8)};
    for (std::size_t i = 0; i < 2; ++i) {
        esp_netif_dns_info_t existing{};
        if (esp_netif_get_dns_info(netif, fill_types[i], &existing) != ESP_OK) continue;
        bool empty = existing.ip.type == ESP_IPADDR_TYPE_V4 && existing.ip.u_addr.ip4.addr == 0;
        if (existing.ip.type == ESP_IPADDR_TYPE_V6) {
            empty = true;
            for (const auto word : existing.ip.u_addr.ip6.addr) empty = empty && word == 0;
        }
        if (empty) {
            // Current IDF/lwIP config shares DNS globally, not per interface.
            // Fill only empty slots, preserving MAIN and every existing server.
            // A negative reply tries the adjacent slot; do not leave BACKUP empty.
            esp_netif_dns_info_t added{};
            added.ip.type = ESP_IPADDR_TYPE_V4;
            added.ip.u_addr.ip4.addr = fill_addresses[i];
            esp_netif_set_dns_info(netif, fill_types[i], &added);
        }
    }
    std::array<std::array<char, 16>, 3> addresses{};
    constexpr esp_netif_dns_type_t types[]{ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK};
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        esp_netif_dns_info_t info{};
        if (esp_netif_get_dns_info(netif, types[i], &info) == ESP_OK && info.ip.type == ESP_IPADDR_TYPE_V4)
            std::snprintf(addresses[i].data(), addresses[i].size(), IPSTR, IP2STR(&info.ip.u_addr.ip4));
    }
    std::printf("SC_CODEX_DNS {\"main\":\"%s\",\"backup\":\"%s\",\"fallback\":\"%s\"}\n",
                addresses[0].data(), addresses[1].data(), addresses[2].data());
}

void emit_diagnostics()
{
    const auto state = snapshot();
    // Explicit safe allowlist. Device authorization codes, debug keys, account
    // identifiers and all OAuth responses are deliberately absent.
    std::printf("SC_CODEX_STATE {\"v\":1,\"uptime_ms\":%" PRIu32
                ",\"source\":\"device_wifi\",\"state\":\"%s\",\"error\":\"%s\","
                "\"wifi_connected\":%s,\"signed_in\":%s,\"login_pending\":%s,"
                "\"http_status\":%d,\"remaining_percent\":%d,\"observed_at\":%" PRIu32
                ",\"reset_at\":%" PRIu32 ",\"next_poll_in_ms\":%" PRIu32 ",\"ip\":\"%s\"}\n",
                static_cast<std::uint32_t>(now_ms()), state_text(state.state), error_text(state.error),
                state.wifi_connected ? "true" : "false", state.signed_in ? "true" : "false",
                state.login_pending ? "true" : "false", state.last_http_status, state.remaining_percent,
                state.observed_at, state.reset_at, state.next_poll_in_ms, state.ip_address.data());
}

class Worker {
public:
    void run()
    {
        key_ready_ = load_debug_key();
        const auto loaded = load_tokens(tokens_);
        storage_fault_ = !key_ready_ || loaded != ESP_OK;
        if (storage_fault_) fail(Error::Storage, 0);
        else status(tokens_.refresh_token.empty() ? State::SignedOut : State::Refreshing);
        while (true) {
            detail::Request command;
            { std::lock_guard<std::mutex> lock(state_mutex); command = commands.take(); }
            if (command.command != detail::Command::None) {
                generation_ = command.generation;
                tokens_ = {}; device_ = {}; starting_login_ = false; polling_login_ = false;
                last_refresh_ms_ = 0; next_work_ms_ = 0; force_refresh_ = false; retries_.reset();
                if (!key_ready_) key_ready_ = load_debug_key();
                const bool erased = store_tokens(nullptr) == ESP_OK;
                storage_fault_ = !erased || !key_ready_;
                if (storage_fault_) fail(Error::Storage, 0);
                else if (command.command == detail::Command::Login) {
                    starting_login_ = true; status(State::StartingLogin);
                } else status(State::SignedOut);
            }
            if (command.refresh && !tokens_.refresh_token.empty()) {
                force_refresh_ = true; next_work_ms_ = 0;
            }
            const auto now = now_ms();
            if (polling_login_ && now >= login_deadline_ms_) {
                polling_login_ = false; device_ = {}; status(State::LoginExpired, Error::AuthExpired);
            }
            if (key_ready_ && !storage_fault_ && network_ready()) {
                if (starting_login_) create_login();
                else if (polling_login_ && now >= next_work_ms_) poll_login();
                else if (!tokens_.refresh_token.empty() && now >= next_work_ms_) fetch_usage();
                else if (tokens_.refresh_token.empty() && !polling_login_) {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    if (commands.current(generation_) && (latest.state == State::WaitingWifi || latest.state == State::WaitingClock)) {
                        latest.state = State::SignedOut; latest.error = Error::None;
                    }
                }
            }
            if (now_ms() - previous_log_ms_ >= 1000) {
                previous_log_ms_ = now_ms();
                emit_diagnostics();
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
        }
    }
private:
    protocol::Tokens tokens_;
    protocol::DeviceCode device_;
    detail::Backoff retries_;
    std::uint32_t generation_ = 1;
    std::uint64_t next_work_ms_ = 0, login_deadline_ms_ = 0, last_refresh_ms_ = 0;
    std::uint64_t next_wifi_init_ms_ = 0;
    std::uint64_t previous_log_ms_ = 0;
    bool key_ready_ = false, storage_fault_ = false, station_started_ = false, sntp_started_ = false;
    bool dns_prepared_ = false;
    bool starting_login_ = false, polling_login_ = false, force_refresh_ = false;

    void status(State state, Error error = Error::None, int http = 0)
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!commands.current(generation_)) return;
        latest.state = state; latest.error = error; latest.last_http_status = http;
        latest.signed_in = !tokens_.refresh_token.empty();
        latest.login_pending = starting_login_ || polling_login_;
        if (!latest.login_pending) { latest.user_code.fill(0); login_expires_at = 0; }
        next_poll_at = next_work_ms_;
    }
    void fail(Error error, int http)
    {
        next_work_ms_ = now_ms() + retries_.fail(error == Error::RateLimited);
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!commands.current(generation_)) return;
        latest.state = State::Error; latest.error = error; latest.last_http_status = http;
        latest.remaining_percent = -1; latest.observed_at = latest.reset_at = 0;
        latest.signed_in = !tokens_.refresh_token.empty();
        latest.login_pending = starting_login_ || polling_login_;
        if (!latest.login_pending) { latest.user_code.fill(0); login_expires_at = 0; }
        next_poll_at = retry_not_before = next_work_ms_;
        publish_locked();
    }
    bool network_ready()
    {
        auto& wifi = WifiManager::GetInstance();
        if (!wifi.IsInitialized() && now_ms() >= next_wifi_init_ms_) {
            next_wifi_init_ms_ = now_ms() + 15000; wifi.Initialize();
        }
        if (wifi.IsInitialized() && !wifi.IsConfigMode() && !station_started_) {
            wifi.StartStation(); station_started_ = true;
        }
        const bool connected = wifi.IsConnected();
        std::array<char, 16> address{};
        if (connected) {
            auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif && !dns_prepared_) { prepare_dns(netif); dns_prepared_ = true; }
            esp_netif_ip_info_t info{};
            if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK)
                std::snprintf(address.data(), address.size(), IPSTR, IP2STR(&info.ip));
        }
        { std::lock_guard<std::mutex> lock(state_mutex); latest.wifi_connected = connected; latest.ip_address = address; }
        if (!connected) {
            sntp_started_ = false; dns_prepared_ = false;
            wait_state(State::WaitingWifi, Error::Network); return false;
        }
        if (!sntp_started_) { GetHAL().startSntp(); sntp_started_ = true; }
        if (!unix_now()) { wait_state(State::WaitingClock, Error::Clock); return false; }
        return true;
    }
    void wait_state(State state, Error error)
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!commands.current(generation_)) return;
        if (latest.state != state) {
            latest.remaining_percent = -1; latest.observed_at = latest.reset_at = 0; publish_locked();
        }
        latest.state = state; latest.error = error;
    }
    bool accept(protocol::Tokens replacement)
    {
        const auto action = detail::rotate([&] { return current(generation_); },
            [&] { return store_tokens(&replacement) == ESP_OK; },
            [&] { tokens_ = std::move(replacement); });
        if (action == detail::RotationAction::IgnoreStale) return false;
        if (action == detail::RotationAction::StorageFailure) {
            tokens_ = {}; storage_fault_ = true; polling_login_ = starting_login_ = false;
            fail(Error::Storage, 0); return false;
        }
        last_refresh_ms_ = now_ms(); retries_.reset(); force_refresh_ = false;
        return true;
    }
    void create_login()
    {
        const auto body = json({{"client_id", kClientId}});
        if (body.empty()) { starting_login_ = false; fail(Error::Internal, 0); return; }
        auto response = request(Endpoint::Usercode, kUsercodeUrl, "application/json", body);
        if (!current(generation_)) return;
        starting_login_ = false;
        if (response.error != Error::None || !protocol::parse_device_code(response.body(), device_)) {
            fail(response.error != Error::None ? response.error : Error::InvalidResponse, response.status); return;
        }
        polling_login_ = true;
        login_deadline_ms_ = now_ms() + kLoginMs;
        next_work_ms_ = now_ms() + static_cast<std::uint64_t>(device_.interval_seconds) * 1000;
        { std::lock_guard<std::mutex> lock(state_mutex);
          if (!commands.current(generation_)) return;
          copy_text(latest.user_code.data(), latest.user_code.size(), device_.user_code);
          login_expires_at = login_deadline_ms_; }
        status(State::AwaitingLogin);
    }
    void poll_login()
    {
        const auto body = json({{"device_auth_id", device_.device_auth_id}, {"user_code", device_.user_code}});
        if (body.empty()) { polling_login_ = false; fail(Error::Internal, 0); return; }
        auto response = request(Endpoint::LoginPoll, kDeviceTokenUrl, "application/json", body);
        if (!current(generation_)) return;
        if (now_ms() >= login_deadline_ms_) {
            polling_login_ = false; device_ = {}; status(State::LoginExpired, Error::AuthExpired); return;
        }
        if ((response.status == 403 || response.status == 404) && response.error == Error::Http) {
            next_work_ms_ = now_ms() + static_cast<std::uint64_t>(device_.interval_seconds) * 1000;
            status(State::AwaitingLogin); return;
        }
        protocol::Authorization authorization;
        polling_login_ = false;
        if (response.error != Error::None || !protocol::parse_authorization(response.body(), authorization)) {
            fail(response.error != Error::None ? response.error : Error::InvalidResponse, response.status); return;
        }
        const auto form = "grant_type=authorization_code&client_id=" + protocol::form_encode(kClientId) +
            "&code=" + protocol::form_encode(authorization.code) +
            "&redirect_uri=" + protocol::form_encode("https://auth.openai.com/deviceauth/callback") +
            "&code_verifier=" + protocol::form_encode(authorization.verifier);
        response = request(Endpoint::TokenExchange, kTokenUrl, "application/x-www-form-urlencoded", form);
        if (!current(generation_)) return;
        if (now_ms() >= login_deadline_ms_) {
            device_ = {}; status(State::LoginExpired, Error::AuthExpired); return;
        }
        protocol::Tokens replacement;
        if (response.error != Error::None || !protocol::parse_tokens(response.body(), unix_now(), {}, {}, replacement)) {
            fail(response.error != Error::None ? response.error : Error::InvalidResponse, response.status); return;
        }
        device_ = {};
        if (accept(std::move(replacement))) { next_work_ms_ = 0; fetch_usage(); }
    }
    bool refresh(const char* reason)
    {
        status(State::Refreshing);
        const auto body = json({{"grant_type", "refresh_token"}, {"client_id", kClientId},
                                {"refresh_token", tokens_.refresh_token}, {"refresh_reason", reason}});
        if (body.empty()) { fail(Error::Internal, 0); return false; }
        auto response = request(Endpoint::Refresh, kTokenUrl, "application/json", body);
        if (!current(generation_)) return false;
        if (protocol::refresh_rejected_permanently(response.status, response.body())) {
            tokens_ = {}; const bool erased = store_tokens(nullptr) == ESP_OK;
            storage_fault_ = !erased;
            fail(erased ? Error::AuthExpired : Error::Storage, response.status); return false;
        }
        protocol::Tokens replacement;
        if (response.error != Error::None || !protocol::parse_refresh_tokens(response.body(), unix_now(), tokens_, replacement)) {
            fail(response.error != Error::None ? response.error : Error::InvalidResponse, response.status); return false;
        }
        return accept(std::move(replacement));
    }
    void fetch_usage()
    {
        const auto now = unix_now();
        const bool expiring = tokens_.expires_at && tokens_.expires_at <= static_cast<std::uint64_t>(now) + 300;
        const bool scheduled = !tokens_.expires_at && now_ms() - last_refresh_ms_ >= kUnknownExpiryMs;
        if ((force_refresh_ || tokens_.access_token.empty() || expiring || scheduled) &&
            !refresh(force_refresh_ ? "forced" : scheduled ? "scheduled" : "proactive_expiry")) return;
        status(State::Fetching);
        auto response = request(Endpoint::Usage, kUsageUrl, nullptr, {}, &tokens_);
        if (!current(generation_)) return;
        if (detail::usage_action(response.status, false) == detail::UsageAction::RefreshOnce && response.error == Error::Http) {
            if (!refresh("unauthorized_recovery")) return;
            response = request(Endpoint::Usage, kUsageUrl, nullptr, {}, &tokens_);
            if (!current(generation_)) return;
        }
        protocol::Usage usage;
        if (response.error != Error::None || !protocol::parse_usage(response.body(), unix_now(), usage)) {
            fail(response.status == 401 ? Error::AuthExpired : response.error != Error::None ? response.error : Error::InvalidResponse,
                 response.status); return;
        }
        next_work_ms_ = now_ms() + kPollMs; retries_.reset();
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!commands.current(generation_)) return;
        const auto observed = unix_now();
        if (usage.reset_at <= observed || usage.reset_at - observed < 60) {
            latest.state = State::Unavailable; latest.error = Error::InvalidResponse;
            latest.remaining_percent = -1; latest.observed_at = latest.reset_at = 0; publish_locked();
        } else {
            latest.state = State::Ready; latest.error = Error::None;
            latest.remaining_percent = usage.remaining_percent; latest.observed_at = observed; latest.reset_at = usage.reset_at;
            latest.last_success_ms = static_cast<std::uint32_t>(now_ms()); publish_locked(&usage);
        }
        latest.signed_in = true; latest.login_pending = false; latest.user_code.fill(0);
        login_expires_at = 0; next_poll_at = next_work_ms_; retry_not_before = 0;
        latest.last_http_status = response.status;
    }
};
void worker(void*) { Worker instance; instance.run(); }
void notify(TaskHandle_t task) { if (task) xTaskNotifyGive(task); }
} // namespace

esp_err_t start()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    if (worker_task) return ESP_OK;
    if (xTaskCreatePinnedToCore(worker, "codex-weekly", 10240, nullptr, 1, &worker_task, 0) != pdPASS) {
        worker_task = nullptr; latest.state = State::Error; latest.error = Error::Internal; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    auto result = latest; const auto now = now_ms();
    result.next_poll_in_ms = static_cast<std::uint32_t>(next_poll_at > now ? std::min<std::uint64_t>(UINT32_MAX, next_poll_at - now) : 0);
    result.login_expires_in_ms = static_cast<std::uint32_t>(login_expires_at > now ? login_expires_at - now : 0);
    return result;
}
void begin_login()
{
    TaskHandle_t task;
    { std::lock_guard<std::mutex> lock(state_mutex);
      if (latest.login_pending || now_ms() < retry_not_before) return;
      commands.login(); latest.state = State::StartingLogin; latest.error = Error::None;
      latest.signed_in = false; latest.login_pending = true; latest.remaining_percent = -1;
      latest.observed_at = latest.reset_at = 0; latest.user_code.fill(0); login_expires_at = next_poll_at = 0;
      publish_locked(); task = worker_task; }
    notify(task);
}
void request_refresh()
{
    TaskHandle_t task;
    { std::lock_guard<std::mutex> lock(state_mutex);
      if (!latest.signed_in || latest.login_pending || now_ms() < retry_not_before ||
          latest.state == State::Refreshing || latest.state == State::Fetching) return;
      commands.refresh(); task = worker_task; }
    notify(task);
}
void logout()
{
    TaskHandle_t task;
    { std::lock_guard<std::mutex> lock(state_mutex);
      commands.logout(); latest.state = State::SignedOut; latest.error = Error::None;
      latest.signed_in = latest.login_pending = false; latest.remaining_percent = -1;
      latest.observed_at = latest.reset_at = 0; latest.user_code.fill(0);
      login_expires_at = next_poll_at = retry_not_before = 0;
      publish_locked(); task = worker_task; }
    notify(task);
}
bool authenticate_debug_key(std::string_view key)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    std::size_t difference = key.size() ^ 32u;
    for (std::size_t i = 0; i < 32; ++i)
        difference |= static_cast<unsigned char>(latest.debug_key[i]) ^
                      static_cast<unsigned char>(i < key.size() ? key[i] : 0);
    return latest.debug_key[0] != 0 && difference == 0;
}
const char* state_text(State value)
{
    switch (value) {
    case State::Starting: return "starting"; case State::WaitingWifi: return "waiting_wifi";
    case State::WaitingClock: return "waiting_clock"; case State::SignedOut: return "signed_out";
    case State::StartingLogin: return "starting_login"; case State::AwaitingLogin: return "awaiting_login";
    case State::Refreshing: return "refreshing"; case State::Fetching: return "fetching";
    case State::Ready: return "ready"; case State::Unavailable: return "unavailable";
    case State::LoginExpired: return "login_expired"; case State::Error: return "error";
    }
    return "error";
}
const char* error_text(Error value)
{
    switch (value) {
    case Error::None: return "none"; case Error::Network: return "network";
    case Error::Clock: return "clock"; case Error::Tls: return "tls"; case Error::Http: return "http";
    case Error::RateLimited: return "rate_limited"; case Error::InvalidResponse: return "invalid_response";
    case Error::AuthExpired: return "auth_expired"; case Error::LoginDisabled: return "login_disabled";
    case Error::Storage: return "storage"; case Error::Internal: return "internal";
    }
    return "internal";
}
} // namespace codex_weekly
