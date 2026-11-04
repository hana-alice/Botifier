#pragma once
#include <esp_err.h>
#include <cstddef>

using httpd_handle_t = void*;
enum httpd_method_t { HTTP_GET, HTTP_POST };
struct httpd_req_t {
    std::size_t content_len = 0;
    void* user_ctx = nullptr;
    void* test_context = nullptr;
};
using httpd_uri_func = esp_err_t (*)(httpd_req_t*);
struct httpd_uri_t {
    const char* uri = nullptr;
    httpd_method_t method = HTTP_GET;
    httpd_uri_func handler = nullptr;
    void* user_ctx = nullptr;
};
inline constexpr int HTTPD_SOCK_ERR_FAIL = -1;
inline constexpr int HTTPD_SOCK_ERR_TIMEOUT = -3;
inline constexpr int HTTPD_RESP_USE_STRLEN = -1;
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t* route);
esp_err_t httpd_unregister_uri_handler(httpd_handle_t server, const char* uri, httpd_method_t method);
std::size_t httpd_req_get_hdr_value_len(httpd_req_t* request, const char* name);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* request, const char* name, char* output, std::size_t size);
int httpd_req_recv(httpd_req_t* request, char* output, std::size_t size);
esp_err_t httpd_resp_set_status(httpd_req_t* request, const char* status);
esp_err_t httpd_resp_set_type(httpd_req_t* request, const char* type);
esp_err_t httpd_resp_set_hdr(httpd_req_t* request, const char* name, const char* value);
esp_err_t httpd_resp_send(httpd_req_t* request, const char* body, std::ptrdiff_t size);

