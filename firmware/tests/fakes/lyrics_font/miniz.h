#pragma once
#include <stddef.h>
#include <stdint.h>
/* The ROM is replaced by a bounded output spy, not a second font decoder.
 * Pixel/data parity is verified by Prepare-LyricsFont.py --check. */
typedef struct {
    uint32_t m_state;
    uint8_t workspace[10988];
} tinfl_decompressor;
typedef enum { TINFL_STATUS_FAILED = -1, TINFL_STATUS_DONE = 0 } tinfl_status;
enum { TINFL_FLAG_PARSE_ZLIB_HEADER = 1, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4 };
#define tinfl_init(decoder) do { (decoder)->m_state = 0; } while(0)
tinfl_status tinfl_decompress(tinfl_decompressor*, const uint8_t*, size_t*, uint8_t*, uint8_t*, size_t*, uint32_t);
