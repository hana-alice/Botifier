#include "service.h"
#include "policy.h"
#include <apps/common/codex_weekly/service.h>
#include <ArduinoJson.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>

namespace firmware_update {
namespace {
static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)
              == detail::kPrefixBytes, "Review the OTA prefix when the SDK image layout changes");
std::mutex upload_mutex;

esp_err_t response_headers(httpd_req_t* request)
{
    auto error = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (error == ESP_OK) error = httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    if (error == ESP_OK) error = httpd_resp_set_hdr(request, "Connection", "close");
    if (error == ESP_OK) error = httpd_resp_set_type(request, "application/json");
    return error;
}
esp_err_t reject(httpd_req_t* request, const char* status, const char* error)
{
    httpd_resp_set_status(request, status);
    std::array<char, 96> body{};
    const int length = std::snprintf(body.data(), body.size(), "{\"error\":\"%s\"}", error);
    if (length > 0 && static_cast<std::size_t>(length) < body.size())
        httpd_resp_send(request, body.data(), length);
    // A failed handler closes its session. Do not let HTTPD drain an unchecked
    // or incomplete body after replying to a rejected upload.
    return ESP_FAIL;
}
template<std::size_t N>
bool header(httpd_req_t* request, const char* name, std::array<char, N>& output, std::size_t required = 0)
{
    const auto length = httpd_req_get_hdr_value_len(request, name);
    return length && length < N && (!required || length == required) &&
           httpd_req_get_hdr_value_str(request, name, output.data(), output.size()) == ESP_OK;
}
bool authenticated(httpd_req_t* request)
{
    std::array<char, 33> key{};
    return header(request, "X-Botifier-Key", key, 32) &&
           codex_weekly::authenticate_debug_key(std::string_view(key.data(), 32));
}
std::string_view bounded_text(const char* value, std::size_t capacity)
{
    const auto* end = static_cast<const char*>(std::memchr(value, 0, capacity));
    return {value, end ? static_cast<std::size_t>(end - value) : capacity};
}
std::array<char, 18> mac_text(const std::array<std::uint8_t, 6>& value)
{
    std::array<char, 18> result{};
    std::snprintf(result.data(), result.size(), "%02x:%02x:%02x:%02x:%02x:%02x",
                  value[0], value[1], value[2], value[3], value[4], value[5]);
    return result;
}
bool same_partition(const esp_partition_t* left, const esp_partition_t* right)
{
    return left && right && left->address == right->address && left->type == right->type &&
           left->subtype == right->subtype;
}
bool inactive_slot(const esp_partition_t* target, const esp_partition_t* running)
{
    return target && running && target->type == ESP_PARTITION_TYPE_APP &&
           target->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
           target->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX && !target->readonly &&
           target->size >= detail::kPrefixBytes && !same_partition(target, running);
}
bool content_length(httpd_req_t* request, std::size_t& length)
{
    std::array<char, 24> value{};
    if (!header(request, "Content-Length", value)) return false;
    std::size_t number = 0;
    for (const char c : std::string_view(value.data(), httpd_req_get_hdr_value_len(request, "Content-Length"))) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::size_t>(c - '0');
        if (number > SIZE_MAX / 10 || (number == SIZE_MAX / 10 && digit > SIZE_MAX % 10)) return false;
        number = number * 10 + digit;
    }
    if (number != request->content_len) return false;
    length = number; return true;
}

struct OtaUpload {
    esp_ota_handle_t handle{};
    bool active = false;
    ~OtaUpload() { if (active) esp_ota_abort(handle); }
    esp_err_t finish()
    {
        active = false;
        // IDF consumes/frees the handle on every esp_ota_end result, including
        // verification failure. An ended handle must not be aborted again.
        return esp_ota_end(handle);
    }
};
struct Hash {
    mbedtls_sha256_context context{};
    Hash() { mbedtls_sha256_init(&context); }
    ~Hash() { mbedtls_sha256_free(&context); }
};
enum class Receive { Ok, Timeout, Closed, Excess };
Receive read_exact(httpd_req_t* request, std::uint8_t* buffer, std::size_t length, std::int64_t deadline)
{
    std::size_t read = 0;
    while (read < length) {
        if (esp_timer_get_time() >= deadline) return Receive::Timeout;
        const auto wanted = length - read;
        const int count = httpd_req_recv(request, reinterpret_cast<char*>(buffer + read), wanted);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) return Receive::Timeout;
        if (count <= 0) return Receive::Closed;
        if (static_cast<std::size_t>(count) > wanted) return Receive::Excess;
        if (esp_timer_get_time() >= deadline) return Receive::Timeout;
        read += static_cast<std::size_t>(count);
    }
    return Receive::Ok;
}
esp_err_t reject_receive(httpd_req_t* request, Receive result)
{
    return reject(request, result == Receive::Timeout ? "408 Request Timeout" : "400 Bad Request",
                  result == Receive::Timeout ? "body_timeout" : result == Receive::Closed ? "body_closed" : "body_length");
}

esp_err_t get_handler(httpd_req_t* request)
{
    if (response_headers(request) != ESP_OK) return ESP_FAIL;
    if (!authenticated(request)) return reject(request, "401 Unauthorized", "device_key");
    std::array<std::uint8_t, 6> mac{};
    const auto* description = esp_app_get_description();
    if (!description || esp_read_mac(mac.data(), ESP_MAC_WIFI_STA) != ESP_OK)
        return reject(request, "500 Internal Server Error", "device_info");
    const auto* running = esp_ota_get_running_partition();
    const auto* boot = esp_ota_get_boot_partition();
    const auto* target = running ? esp_ota_get_next_update_partition(running) : nullptr;
    esp_ota_img_states_t state{};
    const int ota_state = running && esp_ota_get_state_partition(running, &state) == ESP_OK ? static_cast<int>(state) : -1;
    const auto identity = mac_text(mac);
    const auto elf = detail::hex_sha256(description->app_elf_sha256);
    const auto project = bounded_text(description->project_name, sizeof(description->project_name));
    const auto version = bounded_text(description->version, sizeof(description->version));
    JsonDocument doc;
    doc["v"] = 1;
    doc["mac"] = identity.data();
    doc["project_name"] = std::string(project);
    doc["version"] = std::string(version);
    doc["elf_sha256"] = elf.data();
    doc["running_slot"] = running ? running->label : "";
    doc["boot_slot"] = boot ? boot->label : "";
    doc["ota_state"] = ota_state;
    doc["max_image_bytes"] = inactive_slot(target, running) ? target->size : 0;
    doc["upload_supported"] = true;
    std::string body; body.reserve(512); serializeJson(doc, body);
    if (doc.overflowed() || body.size() > 2048)
        return reject(request, "500 Internal Server Error", "device_info");
    return httpd_resp_send(request, body.data(), body.size());
}

esp_err_t post_handler(httpd_req_t* request)
{
    if (response_headers(request) != ESP_OK) return ESP_FAIL;
    if (!authenticated(request)) return reject(request, "401 Unauthorized", "device_key");
    std::unique_lock<std::mutex> exclusive(upload_mutex, std::try_to_lock);
    if (!exclusive.owns_lock()) return reject(request, "409 Conflict", "ota_busy");
    std::array<char, 40> media{};
    if (!header(request, "Content-Type", media, 24) ||
        std::string_view(media.data(), 24) != "application/octet-stream")
        return reject(request, "415 Unsupported Media Type", "content_type");
    if (httpd_req_get_hdr_value_len(request, "Transfer-Encoding"))
        return reject(request, "400 Bad Request", "transfer_encoding");
    std::size_t length = 0;
    if (!content_length(request, length)) return reject(request, "400 Bad Request", "content_length");
    const auto* running = esp_ota_get_running_partition();
    const auto* boot = esp_ota_get_boot_partition();
    const auto* target = running ? esp_ota_get_next_update_partition(running) : nullptr;
    if (!inactive_slot(target, running)) return reject(request, "409 Conflict", "ota_slot");
    if (length < detail::kPrefixBytes || length > target->size)
        return reject(request, "413 Payload Too Large", "image_size");
    esp_ota_img_states_t state{};
    if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_VALID)
        return reject(request, "409 Conflict", "running_unconfirmed");
    if (!same_partition(boot, running)) return reject(request, "409 Conflict", "boot_pending");
    const auto* description = esp_app_get_description();
    std::array<std::uint8_t, 6> mac{};
    if (!description || esp_read_mac(mac.data(), ESP_MAC_WIFI_STA) != ESP_OK)
        return reject(request, "500 Internal Server Error", "device_info");
    std::array<char, 65> digest_header{}, elf_header{};
    std::array<char, 18> expected_mac{};
    std::array<std::uint8_t, 32> expected_digest{}, expected_elf{};
    if (!header(request, "X-Firmware-SHA256", digest_header, 64) ||
        !detail::parse_sha256(std::string_view(digest_header.data(), 64), expected_digest))
        return reject(request, "400 Bad Request", "image_sha256");
    if (!header(request, "X-Expected-ELF-SHA256", elf_header, 64) ||
        !detail::parse_sha256(std::string_view(elf_header.data(), 64), expected_elf))
        return reject(request, "400 Bad Request", "expected_elf");
    if (!detail::equal_sha256(expected_elf, description->app_elf_sha256))
        return reject(request, "409 Conflict", "expected_elf");
    if (!header(request, "X-Expected-MAC", expected_mac, 17) ||
        !detail::matches_mac(std::string_view(expected_mac.data(), 17), mac))
        return reject(request, "409 Conflict", "expected_mac");

    std::unique_ptr<std::uint8_t[]> buffer{new (std::nothrow) std::uint8_t[detail::kChunkBytes]};
    if (!buffer) return reject(request, "500 Internal Server Error", "buffer");
    const auto deadline = esp_timer_get_time() + static_cast<std::int64_t>(detail::kUploadTimeoutMs) * 1000;
    auto received = read_exact(request, buffer.get(), detail::kPrefixBytes, deadline);
    if (received != Receive::Ok) return reject_receive(request, received);
    esp_image_header_t image{};
    esp_image_segment_header_t segment{};
    esp_app_desc_t incoming{};
    std::memcpy(&image, buffer.get(), sizeof(image));
    std::memcpy(&segment, buffer.get() + sizeof(image), sizeof(segment));
    std::memcpy(&incoming, buffer.get() + sizeof(image) + sizeof(segment), sizeof(incoming));
    if (image.magic != ESP_IMAGE_HEADER_MAGIC || image.segment_count == 0 || image.segment_count > ESP_IMAGE_MAX_SEGMENTS ||
        segment.data_len < sizeof(incoming) || segment.data_len > length - sizeof(image) - sizeof(segment))
        return reject(request, "400 Bad Request", "image_header");
    if (image.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID)
        return reject(request, "400 Bad Request", "image_chip");
    if (incoming.magic_word != ESP_APP_DESC_MAGIC_WORD)
        return reject(request, "400 Bad Request", "image_descriptor");
    const auto project = bounded_text(incoming.project_name, sizeof(incoming.project_name));
    if (project.size() == sizeof(incoming.project_name) ||
        project != bounded_text(description->project_name, sizeof(description->project_name)))
        return reject(request, "400 Bad Request", "image_project");

    Hash hash;
    if (mbedtls_sha256_starts(&hash.context, 0) != 0)
        return reject(request, "500 Internal Server Error", "hash_init");
    OtaUpload upload;
    const auto begun = esp_ota_begin(target, length, &upload.handle);
    // IDF can assign the handle before a later erase fails. Own that partially
    // initialized handle too, so every such failure releases its allocation.
    upload.active = upload.handle != 0;
    if (begun != ESP_OK)
        return reject(request, "500 Internal Server Error", "ota_begin");
    upload.active = true;
    std::size_t written = 0, count = detail::kPrefixBytes;
    while (true) {
        if (esp_timer_get_time() >= deadline) return reject_receive(request, Receive::Timeout);
        if (mbedtls_sha256_update(&hash.context, buffer.get(), count) != 0)
            return reject(request, "500 Internal Server Error", "hash_update");
        if (esp_ota_write(upload.handle, buffer.get(), count) != ESP_OK)
            return reject(request, "500 Internal Server Error", "ota_write");
        if (esp_timer_get_time() >= deadline) return reject_receive(request, Receive::Timeout);
        written += count;
        if (written == length) break;
        count = std::min(detail::kChunkBytes, length - written);
        received = read_exact(request, buffer.get(), count, deadline);
        if (received != Receive::Ok) return reject_receive(request, received);
    }
    std::array<std::uint8_t, 32> digest{};
    if (mbedtls_sha256_finish(&hash.context, digest.data()) != 0)
        return reject(request, "500 Internal Server Error", "hash_finish");
    if (!detail::equal_sha256(expected_digest, digest.data()))
        return reject(request, "400 Bad Request", "image_hash");
    const auto sha = detail::hex_sha256(digest.data());
    JsonDocument doc;
    doc["accepted"] = true; doc["bytes"] = written; doc["sha256"] = sha.data();
    doc["target_slot"] = target->label; doc["rebooting"] = true;
    std::string body; body.reserve(256); serializeJson(doc, body);
    if (doc.overflowed() || body.empty() || body.size() > 512)
        return reject(request, "500 Internal Server Error", "response");
    if (esp_timer_get_time() >= deadline) return reject_receive(request, Receive::Timeout);
    if (upload.finish() != ESP_OK)
        return reject(request, "500 Internal Server Error", "ota_end");
    if (esp_timer_get_time() >= deadline) return reject_receive(request, Receive::Timeout);
    if (esp_ota_set_boot_partition(target) != ESP_OK)
        return reject(request, "500 Internal Server Error", "ota_boot");
    const auto sent = httpd_resp_send(request, body.data(), body.size());
    // Boot selection is already committed. Reboot even if the peer drops the
    // response, otherwise this process would remain in boot!=running state.
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return sent;
}
} // namespace

esp_err_t register_handlers(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    httpd_uri_t route{};
    route.uri = "/api/firmware"; route.method = HTTP_GET; route.handler = get_handler;
    auto error = httpd_register_uri_handler(server, &route);
    if (error != ESP_OK) return error;
    route.method = HTTP_POST; route.handler = post_handler;
    error = httpd_register_uri_handler(server, &route);
    if (error != ESP_OK) httpd_unregister_uri_handler(server, "/api/firmware", HTTP_GET);
    return error;
}

} // namespace firmware_update
