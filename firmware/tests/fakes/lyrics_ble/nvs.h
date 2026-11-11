#pragma once
#include <cstddef>
using nvs_handle_t = unsigned;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 100
extern "C" {
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_get_blob(nvs_handle_t,const char*,void*,std::size_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,std::size_t);
int nvs_erase_key(nvs_handle_t,const char*);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
}
