// Actual HTTP handlers, with deterministic platform I/O. The SHA backend checks
// the complete byte stream and error handling, not the cryptographic algorithm.
#include <apps/common/firmware_update/service.h>
#include <apps/common/firmware_update/policy.h>
#include <apps/common/codex_weekly/service.h>
#include <ArduinoJson.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* expression, int line)
{
    if (!value) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t kNoFault = std::numeric_limits<std::size_t>::max();
constexpr int kExcessRead = 1000000;
const std::string kSyntheticKey(32, 'k');
template<std::size_t N, std::size_t M>
void text(char (&destination)[N], const char (&source)[M])
{
    static_assert(M <= N); std::memcpy(destination, source, M);
}
std::string lower(std::string value)
{
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
struct Request {
    httpd_req_t native{};
    std::map<std::string, std::string> headers, response_headers;
    Bytes body;
    std::size_t cursor = 0, fragment = kNoFault, fault_at = kNoFault;
    int fault = 0, receives = 0;
    std::string status = "200 OK", type, response;
    bool send_fails = false;
    void set(const std::string& name, std::string value) { headers[lower(name)] = std::move(value); }
    void erase(const std::string& name) { headers.erase(lower(name)); }
};
struct Fixture;
Fixture* active = nullptr;
struct Fixture {
    esp_partition_t running{}, target{}, alias{};
    const esp_partition_t* running_ptr = &running;
    const esp_partition_t* target_ptr = &target;
    const esp_partition_t* boot_ptr = &running;
    esp_app_desc_t app{};
    std::array<std::uint8_t, 6> mac{{2, 0, 0, 0, 0, 1}};
    std::vector<httpd_uri_t> routes;
    std::vector<std::string> events;
    std::vector<std::size_t> chunks;
    Bytes written, hashed;
    std::array<std::uint8_t, 32> hash{};
    esp_ota_img_states_t state = ESP_OTA_IMG_VALID;
    esp_err_t state_error = ESP_OK, mac_error = ESP_OK;
    esp_err_t begin_error = ESP_OK, end_error = ESP_OK, boot_error = ESP_OK;
    bool missing_app = false, handle_active = false, begin_assigns_on_error = false;
    std::int64_t now_us = 0, receive_advance_us = 0, begin_advance_us = 0;
    std::int64_t write_advance_us = 0, hash_finish_advance_us = 0, end_advance_us = 0;
    int register_calls = 0, register_fail_on = 0, unregister_calls = 0;
    int begins = 0, writes = 0, ends = 0, aborts = 0, boots = 0, restarts = 0;
    int sha_starts = 0, sha_updates = 0, sha_finishes = 0, sha_frees = 0;
    int fail_write_on = 0, fail_update_on = 0;
    bool fail_sha_start = false, fail_sha_finish = false;
    std::size_t begin_size = 0;
    const esp_partition_t* begin_partition = nullptr;
    const esp_partition_t* selected_partition = nullptr;
    esp_ota_handle_t handle = 73;
    TickType_t delayed = 0;
    Fixture()
    {
        CHECK(active == nullptr);
        active = this;
        running.type = target.type = ESP_PARTITION_TYPE_APP;
        running.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
        target.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
        running.address = 0x20000; target.address = 0x510000;
        running.size = target.size = 0x4f0000;
        text(running.label, "ota_0"); text(target.label, "ota_1");
        app.magic_word = ESP_APP_DESC_MAGIC_WORD;
        text(app.project_name, "StackChan"); text(app.version, "synthetic-old");
        std::fill(std::begin(app.app_elf_sha256), std::end(app.app_elf_sha256), std::uint8_t{0x11});
        hash.fill(0xaa);
        CHECK(firmware_update::register_handlers(this) == ESP_OK);
    }
    ~Fixture() { active = nullptr; }
    Request request(std::size_t size = 9000)
    {
        Request r;
        r.body.assign(size, 0xa5);
        r.native.content_len = size;
        r.set("X-Botifier-Key", kSyntheticKey);
        r.set("Content-Type", "application/octet-stream");
        r.set("Content-Length", std::to_string(size));
        r.set("X-Firmware-SHA256", std::string(64, 'a'));
        r.set("X-Expected-ELF-SHA256", std::string(64, '1'));
        r.set("X-Expected-MAC", "02:00:00:00:00:01");
        if (size >= firmware_update::detail::kPrefixBytes) {
            esp_image_header_t image{};
            image.magic = ESP_IMAGE_HEADER_MAGIC; image.segment_count = 1; image.chip_id = ESP_CHIP_ID_ESP32S3;
            esp_image_segment_header_t segment{};
            segment.data_len = static_cast<std::uint32_t>(size - sizeof(image) - sizeof(segment));
            auto incoming = app;
            text(incoming.version, "synthetic-new");
            std::fill(std::begin(incoming.app_elf_sha256), std::end(incoming.app_elf_sha256), std::uint8_t{0x22});
            std::memcpy(r.body.data(), &image, sizeof(image));
            std::memcpy(r.body.data() + sizeof(image), &segment, sizeof(segment));
            std::memcpy(r.body.data() + sizeof(image) + sizeof(segment), &incoming, sizeof(incoming));
        }
        return r;
    }
    esp_err_t invoke(Request& request, httpd_method_t method = HTTP_POST)
    {
        request.native.test_context = &request;
        const auto route = std::find_if(routes.begin(), routes.end(), [method](const auto& r) { return r.method == method; });
        CHECK(route != routes.end());
        CHECK(std::string(route->uri) == "/api/firmware");
        return route->handler(&request.native);
    }
};
Request& req(httpd_req_t* r) { return *static_cast<Request*>(r->test_context); }
const std::string* header_value(Request& r, const char* name)
{
    auto item = r.headers.find(lower(name));
    return item == r.headers.end() ? nullptr : &item->second;
}
void rejected(Fixture& f, Request& r, const char* status, const char* error, bool before_body = true)
{
    CHECK(f.invoke(r) == ESP_FAIL);
    CHECK(r.status.rfind(status, 0) == 0);
    CHECK(r.response == std::string("{\"error\":\"") + error + "\"}");
    CHECK(r.response_headers["connection"] == "close");
    CHECK(r.type == "application/json");
    CHECK(f.begins == 0 && f.boots == 0 && f.restarts == 0 && f.aborts == 0);
    if (before_body) CHECK(r.receives == 0);
}
template<class T, class F>
void mutate_prefix(Request& r, std::size_t offset, F change)
{
    T value{}; std::memcpy(&value, r.body.data() + offset, sizeof(value));
    change(value); std::memcpy(r.body.data() + offset, &value, sizeof(value));
}
void registration_is_atomic()
{
    Fixture f;
    CHECK(firmware_update::register_handlers(nullptr) == ESP_ERR_INVALID_ARG);
    CHECK(f.routes.size() == 2 && f.routes[0].method == HTTP_GET && f.routes[1].method == HTTP_POST);
    f.routes.clear(); f.register_calls = 0; f.register_fail_on = 2;
    CHECK(firmware_update::register_handlers(&f) == ESP_FAIL);
    CHECK(f.routes.empty() && f.unregister_calls == 1);
    f.register_calls = 0; f.register_fail_on = 1;
    CHECK(firmware_update::register_handlers(&f) == ESP_FAIL);
    CHECK(f.routes.empty() && f.unregister_calls == 1);
}
void get_has_a_fixed_public_allowlist()
{
    Fixture f; auto r = f.request();
    CHECK(f.invoke(r, HTTP_GET) == ESP_OK);
    JsonDocument doc; CHECK(!deserializeJson(doc, r.response));
    const std::array<const char*, 10> fields{{"v", "mac", "project_name", "version", "elf_sha256", "running_slot", "boot_slot", "ota_state", "max_image_bytes", "upload_supported"}};
    CHECK(doc.as<JsonObject>().size() == fields.size());
    for (const auto* name : fields) CHECK(doc[name].is<JsonVariant>() && !doc[name].isNull());
    CHECK(doc["project_name"] == "StackChan" && doc["mac"] == "02:00:00:00:00:01");
    CHECK(doc["elf_sha256"] == std::string(64, '1'));
    CHECK(doc["max_image_bytes"].as<std::uint32_t>() == f.target.size);
    CHECK(doc["upload_supported"] == true && r.receives == 0 && f.begins == 0);
    CHECK(r.response.find(kSyntheticKey) == std::string::npos);
    CHECK(r.response.find("token") == std::string::npos && r.response.find("debug_key") == std::string::npos);
    r = f.request(); r.erase("X-Botifier-Key");
    CHECK(f.invoke(r, HTTP_GET) == ESP_FAIL && r.status.rfind("401", 0) == 0);
    CHECK(r.receives == 0 && f.begins == 0);
}
void authentication_and_target_guards_precede_erase()
{
    for (const auto& key : {std::string(), std::string(31, 'k'), std::string(32, 'x'), std::string(33, 'k')}) {
        Fixture f; auto r = f.request(); r.set("X-Botifier-Key", key); rejected(f, r, "401", "device_key");
    }
    const std::vector<std::pair<std::string, std::string>> invalid{
        {"X-Expected-MAC", "02:00:00:00:00:02"}, {"X-Expected-MAC", "020000000001"},
        {"X-Expected-ELF-SHA256", std::string(64, '2')}, {"X-Expected-ELF-SHA256", std::string(64, 'z')},
        {"X-Firmware-SHA256", std::string(63, 'a')}, {"X-Firmware-SHA256", std::string(64, 'z')}};
    for (const auto& item : invalid) {
        Fixture f; auto r = f.request(); r.set(item.first, item.second);
        const auto* error = item.first == "X-Expected-MAC" ? "expected_mac" : item.first == "X-Expected-ELF-SHA256" ? "expected_elf" : "image_sha256";
        const auto* status = item.first == "X-Expected-MAC" || (item.first == "X-Expected-ELF-SHA256" && item.second[0] == '2') ? "409" : "400";
        rejected(f, r, status, error);
    }
    for (const auto* field : {"X-Expected-MAC", "X-Expected-ELF-SHA256", "X-Firmware-SHA256"}) {
        Fixture f; auto r = f.request(); r.erase(field);
        rejected(f, r, std::string(field) == "X-Expected-MAC" ? "409" : "400",
                 std::string(field) == "X-Expected-MAC" ? "expected_mac" : std::string(field) == "X-Expected-ELF-SHA256" ? "expected_elf" : "image_sha256");
    }
}
void length_and_media_are_strict()
{
    for (const auto& value : {std::string(), std::string("9001"), std::string("-9000"), std::string("+9000"), std::string("9000.0"), std::string(" 9000"), std::string("9000\r\n"), std::string("184467440737095516160")}) {
        Fixture f; auto r = f.request(); r.set("Content-Length", value); rejected(f, r, "400", "content_length");
    }
    for (const auto& media : {std::string(), std::string("application/json"), std::string("application/octet-stream; charset=utf-8")}) {
        Fixture f; auto r = f.request(); r.set("Content-Type", media); rejected(f, r, "415", "content_type");
    }
    { Fixture f; auto r = f.request(); r.erase("Content-Length"); rejected(f, r, "400", "content_length"); }
    { Fixture f; auto r = f.request(); r.set("Transfer-Encoding", "chunked"); rejected(f, r, "400", "transfer_encoding"); }
    { Fixture f; auto r = f.request(287); rejected(f, r, "413", "image_size"); }
    { Fixture f; auto r = f.request(); r.native.content_len = f.target.size + 1; r.set("Content-Length", std::to_string(r.native.content_len)); rejected(f, r, "413", "image_size"); }
}
void pending_and_unsafe_partitions_do_not_erase()
{
    for (const auto state : {ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED}) {
        Fixture f; auto r = f.request(); f.state = state; rejected(f, r, "409", "running_unconfirmed");
    }
    { Fixture f; auto r = f.request(); f.state_error = ESP_FAIL; rejected(f, r, "409", "running_unconfirmed"); }
    { Fixture f; auto r = f.request(); f.boot_ptr = &f.target; rejected(f, r, "409", "boot_pending"); }
    { Fixture f; auto r = f.request(); f.boot_ptr = nullptr; rejected(f, r, "409", "boot_pending"); }
    const std::vector<std::function<void(Fixture&)>> cases{
        [](auto& f) { f.running_ptr = nullptr; }, [](auto& f) { f.target_ptr = nullptr; },
        [](auto& f) { f.target_ptr = &f.running; }, [](auto& f) { f.alias = f.running; f.target_ptr = &f.alias; },
        [](auto& f) { f.target.readonly = true; }, [](auto& f) { f.target.type = ESP_PARTITION_TYPE_DATA; },
        [](auto& f) { f.target.subtype = ESP_PARTITION_SUBTYPE_APP_FACTORY; },
        [](auto& f) { f.target.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_MAX; }, [](auto& f) { f.target.size = 287; }};
    for (const auto& setup : cases) { Fixture f; auto r = f.request(); setup(f); rejected(f, r, "409", "ota_slot"); }
    { Fixture f; auto r = f.request(); f.missing_app = true; rejected(f, r, "500", "device_info"); }
    { Fixture f; auto r = f.request(); f.mac_error = ESP_FAIL; rejected(f, r, "500", "device_info"); }
}
void image_identity_is_checked_before_erase()
{
    using Change = std::function<void(Request&)>;
    const std::vector<std::pair<Change, const char*>> cases{
        {[](auto& r) { mutate_prefix<esp_image_header_t>(r, 0, [](auto& v) { v.magic = 0; }); }, "image_header"},
        {[](auto& r) { mutate_prefix<esp_image_header_t>(r, 0, [](auto& v) { v.segment_count = 0; }); }, "image_header"},
        {[](auto& r) { mutate_prefix<esp_image_header_t>(r, 0, [](auto& v) { v.segment_count = 17; }); }, "image_header"},
        {[](auto& r) { mutate_prefix<esp_image_segment_header_t>(r, 24, [](auto& v) { v.data_len = 255; }); }, "image_header"},
        {[](auto& r) { mutate_prefix<esp_image_segment_header_t>(r, 24, [](auto& v) { v.data_len = 9000; }); }, "image_header"},
        {[](auto& r) { mutate_prefix<esp_image_header_t>(r, 0, [](auto& v) { v.chip_id = ESP_CHIP_ID_ESP32; }); }, "image_chip"},
        {[](auto& r) { mutate_prefix<esp_app_desc_t>(r, 32, [](auto& v) { v.magic_word = 0; }); }, "image_descriptor"},
        {[](auto& r) { mutate_prefix<esp_app_desc_t>(r, 32, [](auto& v) { text(v.project_name, "different-project"); }); }, "image_project"},
        {[](auto& r) { mutate_prefix<esp_app_desc_t>(r, 32, [](auto& v) { std::memset(v.project_name, 'X', sizeof(v.project_name)); }); }, "image_project"}};
    for (const auto& item : cases) {
        Fixture f; auto r = f.request(); item.first(r); rejected(f, r, "400", item.second, false);
        CHECK(r.cursor == 288 && f.sha_starts == 0);
    }
}
void receive_failures_abort_only_active_handles()
{
    for (const auto at : {std::size_t(0), std::size_t(120), std::size_t(288), std::size_t(1100)}) {
        for (const auto error : {0, HTTPD_SOCK_ERR_FAIL, HTTPD_SOCK_ERR_TIMEOUT, kExcessRead}) {
            Fixture f; auto r = f.request(); r.fault_at = at; r.fault = error; r.fragment = 43;
            CHECK(f.invoke(r) == ESP_FAIL);
            CHECK(r.status.rfind(error == HTTPD_SOCK_ERR_TIMEOUT ? "408" : "400", 0) == 0);
            CHECK(r.response == std::string("{\"error\":\"") + (error == HTTPD_SOCK_ERR_TIMEOUT ? "body_timeout" : error == kExcessRead ? "body_length" : "body_closed") + "\"}");
            CHECK(f.begins == (at >= 288 ? 1 : 0));
            CHECK(f.aborts == f.begins && !f.handle_active);
            CHECK(f.ends == 0 && f.boots == 0 && f.restarts == 0);
        }
    }
}
void platform_and_hash_errors_cannot_select_a_boot_slot()
{
    struct Case { const char* error; std::function<void(Fixture&)> setup; int begins, aborts, ends; };
    const std::vector<Case> cases{
        {"hash_init", [](auto& f) { f.fail_sha_start = true; }, 0, 0, 0},
        {"ota_begin", [](auto& f) { f.begin_error = ESP_FAIL; }, 1, 0, 0},
        {"ota_begin", [](auto& f) { f.begin_error = ESP_FAIL; f.begin_assigns_on_error = true; }, 1, 1, 0},
        {"hash_update", [](auto& f) { f.fail_update_on = 1; }, 1, 1, 0},
        {"hash_update", [](auto& f) { f.fail_update_on = 2; }, 1, 1, 0},
        {"ota_write", [](auto& f) { f.fail_write_on = 1; }, 1, 1, 0},
        {"ota_write", [](auto& f) { f.fail_write_on = 3; }, 1, 1, 0},
        {"hash_finish", [](auto& f) { f.fail_sha_finish = true; }, 1, 1, 0},
        {"image_hash", [](auto& f) { f.hash[31] ^= 1; }, 1, 1, 0},
        {"ota_end", [](auto& f) { f.end_error = ESP_FAIL; }, 1, 0, 1}};
    for (const auto& item : cases) {
        Fixture f; auto r = f.request(); item.setup(f);
        CHECK(f.invoke(r) == ESP_FAIL);
        CHECK(r.response == std::string("{\"error\":\"") + item.error + "\"}");
        CHECK(f.begins == item.begins && f.aborts == item.aborts && f.ends == item.ends);
        CHECK(!f.handle_active && f.boots == 0 && f.restarts == 0);
        CHECK(f.sha_frees == 1);
    }
    Fixture f; auto r = f.request(); f.boot_error = ESP_FAIL;
    CHECK(f.invoke(r) == ESP_FAIL && r.response == "{\"error\":\"ota_boot\"}");
    CHECK(f.ends == 1 && f.aborts == 0 && f.boots == 1 && f.restarts == 0);
}
void upload_deadline_covers_receives_writes_and_finalization()
{
    constexpr std::int64_t deadline_us = 120000000;
    CHECK(firmware_update::detail::kUploadTimeoutMs == 120000);
    struct Case { std::function<void(Fixture&, Request&)> setup; int begins, ends, aborts; };
    const std::vector<Case> cases{
        {[=](auto& f, auto&) { f.receive_advance_us = deadline_us; }, 0, 0, 0},
        {[=](auto& f, auto&) { f.receive_advance_us = deadline_us / 2; }, 1, 0, 1},
        {[](auto& f, auto& r) { r.fragment = 100; f.receive_advance_us = 3000000; }, 1, 0, 1},
        {[=](auto& f, auto&) { f.begin_advance_us = deadline_us; }, 1, 0, 1},
        {[=](auto& f, auto&) { f.write_advance_us = deadline_us; }, 1, 0, 1},
        {[=](auto& f, auto&) { f.hash_finish_advance_us = deadline_us; }, 1, 0, 1},
        {[=](auto& f, auto&) { f.end_advance_us = deadline_us; }, 1, 1, 0}};
    for (const auto& item : cases) {
        Fixture f; auto r = f.request(); item.setup(f, r);
        CHECK(f.invoke(r) == ESP_FAIL && r.status.rfind("408", 0) == 0);
        CHECK(r.response == "{\"error\":\"body_timeout\"}");
        CHECK(f.begins == item.begins && f.ends == item.ends && f.aborts == item.aborts);
        CHECK(!f.handle_active && f.boots == 0 && f.restarts == 0);
    }
    // The exact deadline is rejected; finishing one microsecond before it is
    // valid. A late esp_ota_end has consumed its handle and must not be aborted.
    Fixture f; auto r = f.request(288); f.write_advance_us = deadline_us - 1;
    CHECK(f.invoke(r) == ESP_OK && f.boots == 1 && f.restarts == 1);
}
std::size_t event_index(const Fixture& f, const char* event)
{
    const auto item = std::find(f.events.begin(), f.events.end(), event);
    CHECK(item != f.events.end());
    return static_cast<std::size_t>(item - f.events.begin());
}
void success_verifies_inactive_image_before_reply_and_restart()
{
    for (const auto fragment : {std::size_t(19), kNoFault}) {
        Fixture f; auto r = f.request(); r.fragment = fragment;
        CHECK(f.invoke(r) == ESP_OK);
        CHECK(f.begins == 1 && f.ends == 1 && f.boots == 1 && f.aborts == 0 && f.restarts == 1);
        CHECK(f.begin_partition == &f.target && f.selected_partition == &f.target && f.begin_size == r.body.size());
        CHECK(f.written == r.body && f.hashed == r.body && r.cursor == r.body.size());
        CHECK(f.chunks == std::vector<std::size_t>({288, 4096, 4096, 520}));
        CHECK(!f.handle_active && f.delayed == pdMS_TO_TICKS(700) && f.sha_frees == 1);
        CHECK(event_index(f, "hash_finish") < event_index(f, "end"));
        CHECK(event_index(f, "end") < event_index(f, "set_boot"));
        CHECK(event_index(f, "set_boot") < event_index(f, "reply"));
        CHECK(event_index(f, "reply") < event_index(f, "delay") && event_index(f, "delay") < event_index(f, "restart"));
        JsonDocument doc; CHECK(!deserializeJson(doc, r.response));
        CHECK(doc.as<JsonObject>().size() == 5 && doc["accepted"] == true && doc["rebooting"] == true);
        CHECK(doc["bytes"].as<std::size_t>() == r.body.size() && doc["target_slot"] == "ota_1");
        CHECK(doc["sha256"] == std::string(64, 'a'));
    }
    // Once boot selection is committed, a lost response still reboots so the
    // running process cannot remain stuck behind the boot!=running guard.
    Fixture f; auto r = f.request(); r.send_fails = true;
    CHECK(f.invoke(r) == ESP_FAIL && f.ends == 1 && f.boots == 1);
    CHECK(f.restarts == 1 && f.delayed == pdMS_TO_TICKS(700) && f.aborts == 0);
    CHECK(event_index(f, "reply") < event_index(f, "delay") && event_index(f, "delay") < event_index(f, "restart"));
}
void policy_handles_hex_without_partial_output()
{
    std::array<std::uint8_t, 32> parsed{}; parsed.fill(0x77);
    CHECK(!firmware_update::detail::parse_sha256(std::string(63, 'a') + "!", parsed));
    CHECK(std::all_of(parsed.begin(), parsed.end(), [](auto byte) { return byte == 0x77; }));
    CHECK(firmware_update::detail::parse_sha256(std::string(64, 'A'), parsed));
    CHECK(std::all_of(parsed.begin(), parsed.end(), [](auto byte) { return byte == 0xaa; }));
    const std::array<std::uint8_t, 6> mac{{0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}};
    CHECK(firmware_update::detail::matches_mac("AA:bb:CC:dd:EE:ff", mac));
    CHECK(!firmware_update::detail::matches_mac("AA-bb-CC-dd-EE-ff", mac));
    CHECK(!firmware_update::detail::matches_mac("AA:bb:CC:dd:EE:fG", mac));
}
} // namespace

namespace codex_weekly {
bool authenticate_debug_key(std::string_view key) { return key == kSyntheticKey; }
}
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t* route)
{
    CHECK(server == active); auto& f = *active;
    if (++f.register_calls == f.register_fail_on) return ESP_FAIL;
    f.routes.push_back(*route); return ESP_OK;
}
esp_err_t httpd_unregister_uri_handler(httpd_handle_t server, const char* uri, httpd_method_t method)
{
    CHECK(server == active); auto& f = *active; ++f.unregister_calls;
    f.routes.erase(std::remove_if(f.routes.begin(), f.routes.end(), [uri, method](const auto& route) {
        return route.method == method && std::string(route.uri) == uri;
    }), f.routes.end()); return ESP_OK;
}
std::size_t httpd_req_get_hdr_value_len(httpd_req_t* r, const char* name)
{
    const auto* value = header_value(req(r), name); return value ? value->size() : 0;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* r, const char* name, char* output, std::size_t size)
{
    const auto* value = header_value(req(r), name);
    if (!value || value->size() >= size) return ESP_ERR_INVALID_SIZE;
    std::memcpy(output, value->c_str(), value->size() + 1); return ESP_OK;
}
int httpd_req_recv(httpd_req_t* r, char* output, std::size_t wanted)
{
    auto& request = req(r); ++request.receives;
    active->now_us += active->receive_advance_us;
    if (request.cursor >= request.fault_at) return request.fault == kExcessRead ? static_cast<int>(wanted + 1) : request.fault;
    const auto count = std::min({wanted, request.fragment, request.body.size() - request.cursor, request.fault_at - request.cursor});
    std::memcpy(output, request.body.data() + request.cursor, count); request.cursor += count;
    return static_cast<int>(count);
}
esp_err_t httpd_resp_set_status(httpd_req_t* r, const char* status) { req(r).status = status; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t* r, const char* type) { req(r).type = type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t* r, const char* name, const char* value) { req(r).response_headers[lower(name)] = value; return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t* r, const char* body, std::ptrdiff_t size)
{
    auto& request = req(r); active->events.push_back("reply");
    request.response.assign(body, size < 0 ? std::strlen(body) : static_cast<std::size_t>(size));
    return request.send_fails ? ESP_FAIL : ESP_OK;
}
const esp_app_desc_t* esp_app_get_description() { return active->missing_app ? nullptr : &active->app; }
esp_err_t esp_read_mac(std::uint8_t* output, esp_mac_type_t type)
{
    CHECK(type == ESP_MAC_WIFI_STA); std::copy(active->mac.begin(), active->mac.end(), output); return active->mac_error;
}
const esp_partition_t* esp_ota_get_running_partition() { return active->running_ptr; }
const esp_partition_t* esp_ota_get_boot_partition() { return active->boot_ptr; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* running)
{
    CHECK(running == active->running_ptr); return active->target_ptr;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t* partition, esp_ota_img_states_t* state)
{
    CHECK(partition == active->running_ptr); *state = active->state; return active->state_error;
}
esp_err_t esp_ota_begin(const esp_partition_t* partition, std::size_t size, esp_ota_handle_t* handle)
{
    auto& f = *active; ++f.begins; f.events.push_back("begin");
    CHECK(partition == f.target_ptr && partition != f.running_ptr && !f.handle_active);
    f.begin_partition = partition; f.begin_size = size;
    f.now_us += f.begin_advance_us;
    if (f.begin_error != ESP_OK) {
        if (f.begin_assigns_on_error) { f.handle_active = true; *handle = f.handle; }
        return f.begin_error;
    }
    f.handle_active = true; *handle = f.handle; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, std::size_t size)
{
    auto& f = *active; CHECK(f.handle_active && handle == f.handle && size <= 4096);
    ++f.writes; f.events.push_back("write");
    f.now_us += f.write_advance_us;
    if (f.writes == f.fail_write_on) return ESP_FAIL;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    f.written.insert(f.written.end(), bytes, bytes + size); f.chunks.push_back(size); return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
    auto& f = *active; CHECK(f.handle_active && handle == f.handle);
    CHECK(f.sha_finishes == 1); f.handle_active = false; ++f.ends; f.events.push_back("end");
    f.now_us += f.end_advance_us; return f.end_error;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
    auto& f = *active; CHECK(f.handle_active && handle == f.handle);
    f.handle_active = false; ++f.aborts; f.events.push_back("abort"); return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition)
{
    auto& f = *active; CHECK(!f.handle_active && f.ends == 1 && f.end_error == ESP_OK && partition == f.target_ptr);
    ++f.boots; f.events.push_back("set_boot"); f.selected_partition = partition; return f.boot_error;
}
void vTaskDelay(TickType_t ticks) { active->delayed = ticks; active->events.push_back("delay"); }
void esp_restart() { ++active->restarts; active->events.push_back("restart"); }
std::int64_t esp_timer_get_time() { return active->now_us; }
void mbedtls_sha256_init(mbedtls_sha256_context* context) { context->started = false; }
void mbedtls_sha256_free(mbedtls_sha256_context*) { ++active->sha_frees; }
int mbedtls_sha256_starts(mbedtls_sha256_context* context, int is224)
{
    CHECK(is224 == 0); ++active->sha_starts; context->started = !active->fail_sha_start; return context->started ? 0 : -1;
}
int mbedtls_sha256_update(mbedtls_sha256_context* context, const unsigned char* data, std::size_t size)
{
    CHECK(context->started); auto& f = *active; ++f.sha_updates;
    if (f.sha_updates == f.fail_update_on) return -1;
    f.hashed.insert(f.hashed.end(), data, data + size); return 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context* context, unsigned char* output)
{
    CHECK(context->started); auto& f = *active; ++f.sha_finishes; f.events.push_back("hash_finish");
    f.now_us += f.hash_finish_advance_us;
    if (f.fail_sha_finish) return -1;
    std::copy(f.hash.begin(), f.hash.end(), output); return 0;
}
int main()
{
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"registration is atomic", registration_is_atomic},
        {"GET public allowlist", get_has_a_fixed_public_allowlist},
        {"auth and target guards before erase", authentication_and_target_guards_precede_erase},
        {"strict length and media", length_and_media_are_strict},
        {"pending and unsafe partitions", pending_and_unsafe_partitions_do_not_erase},
        {"image identity before erase", image_identity_is_checked_before_erase},
        {"stream failures abort active handles", receive_failures_abort_only_active_handles},
        {"OTA and hash errors cannot select boot", platform_and_hash_errors_cannot_select_a_boot_slot},
        {"total upload deadline", upload_deadline_covers_receives_writes_and_finalization},
        {"verified success ordering", success_verifies_inactive_image_before_reply_and_restart},
        {"atomic hex and MAC policy", policy_handles_hex_without_partial_output}};
    int passed = 0;
    for (const auto& test : tests) {
        try { test.second(); ++passed; }
        catch (const std::exception& error) { std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << "firmware_update_test: " << passed << " groups passed (actual handlers; fake platform/SHA backend)\n";
    return 0;
}
