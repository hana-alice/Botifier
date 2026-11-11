#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
extern bool g_panic_abort;
typedef enum {
    PANIC_EXCEPTION_DEBUG, PANIC_EXCEPTION_IWDT, PANIC_EXCEPTION_TWDT,
    PANIC_EXCEPTION_ABORT, PANIC_EXCEPTION_FAULT
} panic_exception_t;
typedef void (*panic_info_dump_fn_t)(const void* frame);
typedef struct {
    int core;
    panic_exception_t exception;
    const char* reason;
    const char* description;
    panic_info_dump_fn_t details;
    panic_info_dump_fn_t state;
    const void* addr;
    const void* frame;
    bool pseudo_excause;
} panic_info_t;
#ifdef __cplusplus
}
#endif
