#pragma once

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(botifier_cjk_20);

/* Original 20 px metrics and nearest four-level coverage from the 4bpp source.
 * Pixels are continuous MSB-first 2bpp, with each glyph beginning on a byte.
 * Caller must hold the LVGL lock. This packed pointer is valid only until a
 * different glyph block is decoded, including a failed decode attempt.
 * Invalid/empty glyphs or decompression errors return NULL. No shared A8 bitmap
 * is exposed; the native font callback fills LVGL's caller-owned A8 buffer. */
const uint8_t* botifier_cjk_20_bitmap(uint32_t glyph_id);

#ifdef __cplusplus
}
#endif
