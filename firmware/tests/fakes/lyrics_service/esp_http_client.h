#pragma once
#include <esp_err.h>
struct FakeHttpClient;
using esp_http_client_handle_t = FakeHttpClient*;
enum esp_http_client_event_id_t { HTTP_EVENT_ERROR, HTTP_EVENT_ON_CONNECTED, HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA, HTTP_EVENT_ON_FINISH, HTTP_EVENT_DISCONNECTED };
struct esp_http_client_event_t {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t client;
    void* data = nullptr;
    int data_len = 0;
    void* user_data = nullptr;
    char* header_key = nullptr;
    char* header_value = nullptr;
};
enum esp_http_client_method_t { HTTP_METHOD_GET };
struct esp_http_client_config_t {
    const char* url = nullptr;
    esp_http_client_method_t method = HTTP_METHOD_GET;
    esp_err_t (*crt_bundle_attach)(void*) = nullptr;
    bool disable_auto_redirect = false;
    bool is_async = false;
    int timeout_ms = 0;
    esp_err_t (*event_handler)(esp_http_client_event_t*) = nullptr;
    void* user_data = nullptr;
    int buffer_size_tx = 0;
};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_get_errno(esp_http_client_handle_t);
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t, int*, int*);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
