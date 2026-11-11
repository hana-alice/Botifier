#pragma once
#include <stdint.h>
// The fields and padding used by the pinned S3 exception frame. This fake is
// only a controlled source object; final target ABI/IRAM placement needs its ELF.
typedef struct {
    uint32_t exit, pc, ps, a0, a1;
    uint32_t a2_to_a15[14];
    uint32_t sar, exccause, excvaddr;
    uint32_t lbeg, lend, lcount, tmp0, tmp1, tmp2;
} XtExcFrame;
