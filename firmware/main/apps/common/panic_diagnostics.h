#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed numeric state only. Register addresses are values, never stack dumps.
 * valid verifies the retained record; info_valid/frame_valid describe the
 * independently bounded source structures. A null panic addr is legitimate. */
typedef struct {
    uint32_t valid;
    uint32_t info_valid;
    int32_t core;
    int32_t exception;
    uint32_t addr;
    uint32_t pseudo_excause;
    uint32_t aborted;
    uint32_t frame_valid;
    uint32_t pc;
    uint32_t exccause;
    uint32_t excvaddr;
    uint32_t a0;
    uint32_t a1;
} botifier_panic_record_t;

typedef struct {
    uint32_t version;
    botifier_panic_record_t current;
    botifier_panic_record_t previous;
} botifier_panic_snapshot_t;

void botifier_panic_boot(void);
botifier_panic_snapshot_t botifier_panic_snapshot(void);

#ifdef __cplusplus
}
#endif
