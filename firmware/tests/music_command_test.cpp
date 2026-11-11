// Compile the actual bounded handler extracted by CMake; only socket/HTTP input,
// the existing key verifier, and the existing lyrics command sink are faked.
#include <apps/common/codex_weekly/music_command.h>
#include <apps/common/runtime_diagnostics.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace codex_weekly { bool authenticate_debug_key(std::string_view key); }
namespace lyrics { void set_enabled(bool enabled); }
int httpd_req_to_sockfd(httpd_req_t* request);
namespace codex_weekly::network_debug {
namespace {
#include "music_command_handler.inc"
#include "diagnostic_status.inc"
}
} // namespace codex_weekly::network_debug

namespace {
unsigned checks = 0;
void check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)
struct Request {
    httpd_req_t native{};
    std::map<std::string, std::string> headers, response_headers;
    std::string body, response, type, status = "200 OK";
    std::size_t cursor = 0, fragment = 1000;
    unsigned reads = 0, commands = 0, socket_changes = 0;
    bool enabled = false, auth_ok = true, socket_ok = true, set_timeout_ok = true;
    int receive_fault = 1;
    std::int64_t now = 1000000, receive_delay = 0;
    timeval timeout{7, 0};
    std::vector<long long> requested_timeouts;
};
Request* active = nullptr;
Request& context(httpd_req_t* request) { return *static_cast<Request*>(request->test_context); }
Request request(std::string body = "{\"enabled\":true}")
{
    Request result;
    result.body = std::move(body);
    result.native.content_len = result.body.size();
    result.headers["X-Botifier-Key"] = std::string(32, 'a');
    result.headers["Content-Type"] = "application/json";
    result.headers["Content-Length"] = std::to_string(result.body.size());
    return result;
}
esp_err_t invoke(Request& r)
{
    r.native.test_context = &r; active = &r;
    const auto result = codex_weekly::network_debug::music_handler(&r.native);
    active = nullptr; return result;
}
void rejects_without_body(Request& r, const char* expected)
{
    CHECK(invoke(r) == ESP_FAIL); CHECK(r.status.substr(0, 3) == expected);
    CHECK(r.reads == 0 && r.commands == 0 && r.socket_changes == 0);
}

void admission()
{
    auto r = request(); r.headers.erase("X-Botifier-Key"); rejects_without_body(r, "401");
    r = request(); r.headers["X-Botifier-Key"] = std::string(31, 'a'); rejects_without_body(r, "401");
    r = request(); r.auth_ok = false; r.native.content_len = 100000000; rejects_without_body(r, "401");
    r = request(); r.headers["Transfer-Encoding"] = "chunked"; rejects_without_body(r, "400");
    r = request(); r.headers["Content-Type"] = "text/plain"; rejects_without_body(r, "415");
    r = request(); r.headers["Content-Type"] = "application/json; charset=utf-8"; rejects_without_body(r, "415");
    r = request(); r.headers.erase("Content-Type"); rejects_without_body(r, "415");
    r = request(std::string(65, ' ')); rejects_without_body(r, "413");
    r = request(""); rejects_without_body(r, "413");
    for (const auto* length : {"", "-1", "+16", "16x", "17", "999999999999999999999999"}) {
        r = request(); r.headers["Content-Length"] = length; rejects_without_body(r, "400");
    }
    r = request(); r.headers.erase("Content-Length"); rejects_without_body(r, "400");
}

void bodies()
{
    for (const auto* body : {"{\"enabled\":true}", "{\"enabled\":false}", " \r\n{ \"enabled\" : true }\t"}) {
        auto r = request(body); r.fragment = 1;
        CHECK(invoke(r) == ESP_OK && r.status == "202 Accepted");
        CHECK(r.commands == 1 && r.enabled == (std::string(body).find("true") != std::string::npos));
        CHECK(r.response == "{\"accepted\":true}" && r.type == "application/json");
        CHECK(r.response_headers["Cache-Control"] == "no-store");
        CHECK(r.response_headers["Connection"] == "close");
        CHECK(r.timeout.tv_sec == 7 && r.timeout.tv_usec == 0);
    }
    auto padded = std::string("{\"enabled\":false}"); padded.append(64 - padded.size(), ' ');
    auto r = request(padded); CHECK(invoke(r) == ESP_OK && r.commands == 1 && !r.enabled);
    for (const auto* body : {"{}", "[]", "true", "{\"enabled\":1}", "{\"enabled\":null}",
                            "{\"enabled\":\"true\"}", "{\"enabled\":True}", "{\"enabled\":true,}",
                            "{\"enabled\":true,\"enabled\":false}", "{\"enabled\":false,\"extra\":1}",
                            "{\"other\":true}", "{\"enabled\":false}{}", "{\"enabled\":true}x",
                            "{\"enab\\u006ced\":true}", "{\"enabled\":falsee}", "{\"enabled\":true"}) {
        r = request(body); CHECK(invoke(r) == ESP_FAIL && r.status.substr(0, 3) == "400");
        CHECK(r.commands == 0);
    }
    r = request(std::string("{\"enabled\":true}\0", 17));
    CHECK(invoke(r) == ESP_FAIL && r.commands == 0);
}

void deadlines()
{
    auto r = request(); r.fragment = 4; r.receive_delay = 2000000;
    CHECK(invoke(r) == ESP_FAIL && r.status.substr(0, 3) == "408" && r.commands == 0);
    CHECK(r.reads == 3);
    CHECK(r.requested_timeouts.size() == 4); // 5s, 3s, 1s, restore 7s
    CHECK(r.requested_timeouts[0] == 5000000 && r.requested_timeouts[1] == 3000000 &&
          r.requested_timeouts[2] == 1000000 && r.requested_timeouts[3] == 7000000);
    CHECK(r.timeout.tv_sec == 7);
    r = request(); r.receive_fault = HTTPD_SOCK_ERR_TIMEOUT;
    CHECK(invoke(r) == ESP_FAIL && r.status.substr(0, 3) == "408" && r.commands == 0);
    r = request(); r.receive_fault = 0;
    CHECK(invoke(r) == ESP_FAIL && r.status.substr(0, 3) == "400" && r.commands == 0);
    r = request(); r.socket_ok = false;
    CHECK(invoke(r) == ESP_FAIL && r.status.substr(0, 3) == "500" && r.reads == 0 && r.commands == 0);
    r = request(); r.set_timeout_ok = false;
    CHECK(invoke(r) == ESP_FAIL && r.status.substr(0, 3) == "500" && r.reads == 0 && r.commands == 0);
}

void status_shape()
{
    botifier_diag_boot_t boot{};
    boot.valid = boot.boot_counter = boot.reset_reason = std::numeric_limits<std::uint32_t>::max();
    for (auto& lane : boot.lanes) {
        lane = {0xffffffff, 0xffffffff, 0xffffffff, std::numeric_limits<std::int32_t>::min(),
                0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    }
    JsonDocument doc;
    codex_weekly::network_debug::diagnostic_boot(doc["current"].to<JsonObject>(), boot);
    codex_weekly::network_debug::diagnostic_boot(doc["previous"].to<JsonObject>(), boot);
    std::string encoded; serializeJson(doc, encoded);
    CHECK(!doc.overflowed() && encoded.size() < 1024);
    CHECK(doc["current"]["lanes"].as<JsonArray>().size() == 2);
    CHECK(doc["current"]["lanes"][0].as<JsonArray>().size() == 11);
    CHECK(doc["previous"]["lanes"][1][3].as<std::int32_t>() == std::numeric_limits<std::int32_t>::min());
}
} // namespace

namespace codex_weekly {
bool authenticate_debug_key(std::string_view key) { return active->auth_ok && key == std::string(32, 'a'); }
}
namespace lyrics { void set_enabled(bool enabled) { ++active->commands; active->enabled = enabled; } }
std::int64_t esp_timer_get_time() { return active->now; }
int httpd_req_to_sockfd(httpd_req_t*) { return active->socket_ok ? 3 : -1; }
std::size_t httpd_req_get_hdr_value_len(httpd_req_t* r, const char* name)
{
    auto& headers = context(r).headers;
    const auto found = headers.find(name); return found == headers.end() ? 0 : found->second.size();
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* r, const char* name, char* output, std::size_t size)
{
    auto& headers = context(r).headers;
    const auto found = headers.find(name);
    if (found == headers.end() || size <= found->second.size()) return ESP_FAIL;
    std::memcpy(output, found->second.c_str(), found->second.size() + 1); return ESP_OK;
}
int httpd_req_recv(httpd_req_t* r, char* output, std::size_t size)
{
    auto& state = context(r); ++state.reads;
    state.now += state.receive_delay;
    if (state.receive_fault <= 0) return state.receive_fault;
    const auto available = std::min({size, state.fragment, state.body.size() - state.cursor});
    std::memcpy(output, state.body.data() + state.cursor, available); state.cursor += available;
    return static_cast<int>(available);
}
esp_err_t httpd_resp_set_status(httpd_req_t* r, const char* status) { context(r).status = status; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t* r, const char* type) { context(r).type = type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t* r, const char* name, const char* value)
{ context(r).response_headers[name] = value; return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t* r, const char* body, std::ptrdiff_t size)
{ context(r).response.assign(body, size < 0 ? std::strlen(body) : static_cast<std::size_t>(size)); return ESP_OK; }
int botifier_test_getsockopt(int, int, int, void* value, socklen_t* size)
{
    if (!active->socket_ok || *size < sizeof(timeval)) return -1;
    *static_cast<timeval*>(value) = active->timeout; *size = sizeof(timeval); return 0;
}
int botifier_test_setsockopt(int, int, int, const void* value, socklen_t)
{
    ++active->socket_changes;
    if (!active->set_timeout_ok) return -1;
    active->timeout = *static_cast<const timeval*>(value);
    active->requested_timeouts.push_back(active->timeout.tv_sec * 1000000ll + active->timeout.tv_usec); return 0;
}

int main()
{
    try {
        admission(); bodies(); deadlines(); status_shape();
        std::cout << checks << " music HTTP command checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
