#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { BOTIFIER_DIAG_BLE = 0, BOTIFIER_DIAG_OTA = 1, BOTIFIER_DIAG_LANE_COUNT = 2 };

/* Numeric-only, best-effort breadcrumbs; marks never allocate, log, query the
 * clock or call an RTOS API. Concurrent writers do not wait for each other. */
void botifier_diag_boot(void);
void botifier_diag_mark(uint32_t lane, uint32_t stage, int32_t error);
/* Heap inspection is deliberately separate: call only outside critical sections. */
void botifier_diag_sample_memory(uint32_t lane);

typedef struct {
    uint32_t valid;
    uint32_t sequence;
    uint32_t stage;
    int32_t error;
    uint32_t marks;
    uint32_t sample_stage;
    uint32_t sample_mark;
    uint32_t sample_uptime_ms;
    uint32_t free_internal;
    uint32_t largest_internal;
    uint32_t minimum_internal;
} botifier_diag_lane_t;

typedef struct {
    uint32_t valid;
    uint32_t boot_counter;
    uint32_t reset_reason;
    botifier_diag_lane_t lanes[BOTIFIER_DIAG_LANE_COUNT];
} botifier_diag_boot_t;

typedef struct {
    uint32_t version;
    botifier_diag_boot_t current;
    botifier_diag_boot_t previous;
} botifier_diag_snapshot_t;

/* Bounded atomic reads. An interrupted/corrupt lane is returned with valid=0. */
botifier_diag_snapshot_t botifier_diag_snapshot(void);

#ifdef __cplusplus
}
#endif
