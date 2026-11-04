#pragma once
#include <esp_err.h>
#include <esp_partition.h>
#include <esp_app_desc.h>
#include <cstddef>
#include <cstdint>
using esp_ota_handle_t = std::uint32_t;
enum esp_ota_img_states_t {
    ESP_OTA_IMG_UNDEFINED = -1, ESP_OTA_IMG_NEW = 0, ESP_OTA_IMG_PENDING_VERIFY = 1,
    ESP_OTA_IMG_VALID = 2, ESP_OTA_IMG_INVALID = 3, ESP_OTA_IMG_ABORTED = 4
};
const esp_partition_t* esp_ota_get_running_partition();
const esp_partition_t* esp_ota_get_boot_partition();
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* start);
esp_err_t esp_ota_get_state_partition(const esp_partition_t* partition, esp_ota_img_states_t* state);
esp_err_t esp_ota_begin(const esp_partition_t* partition, std::size_t size, esp_ota_handle_t* handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* bytes, std::size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition);

