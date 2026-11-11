#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 3
#define LV_VERSION_CHECK(major, minor, patch) 1
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_FONT_DECLARE(name) extern const lv_font_t name
enum { LV_FONT_SUBPX_NONE = 0, LV_COLOR_FORMAT_A8 = 8 };
enum { LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL, LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
       LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY, LV_FONT_FMT_TXT_CMAP_SPARSE_TINY };
typedef struct {
    uint32_t bitmap_index, adv_w;
    uint16_t box_w, box_h;
    int16_t ofs_x, ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;
typedef struct {
    uint32_t range_start;
    uint16_t range_length, glyph_id_start;
    const uint16_t* unicode_list;
    const void* glyph_id_ofs_list;
    uint16_t list_length;
    int type;
} lv_font_fmt_txt_cmap_t;
typedef struct {
    const int8_t* class_pair_values;
    const uint8_t* left_class_mapping;
    const uint8_t* right_class_mapping;
    uint8_t left_class_cnt, right_class_cnt;
} lv_font_fmt_txt_kern_classes_t;
typedef struct {
    const uint8_t* glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t* glyph_dsc;
    const lv_font_fmt_txt_cmap_t* cmaps;
    const void* kern_dsc;
    uint16_t kern_scale, cmap_num, bpp, kern_classes, bitmap_format;
} lv_font_fmt_txt_dsc_t;
typedef struct {
    uint16_t box_w, box_h;
    bool req_raw_bitmap;
    union { uint32_t index; const void* src; } gid;
} lv_font_glyph_dsc_t;
typedef struct {
    struct { uint32_t cf, w, h, stride; } header;
    uint32_t data_size;
    uint8_t* data;
} lv_draw_buf_t;
typedef struct lv_font_t {
    bool (*get_glyph_dsc)(const struct lv_font_t*, lv_font_glyph_dsc_t*, uint32_t, uint32_t);
    const void* (*get_glyph_bitmap)(lv_font_glyph_dsc_t*, lv_draw_buf_t*);
    int32_t line_height, base_line;
    uint8_t subpx;
    int8_t underline_position, underline_thickness;
    const void* dsc;
    const struct lv_font_t* fallback;
    void* user_data;
} lv_font_t;
bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t*, lv_font_glyph_dsc_t*, uint32_t, uint32_t);
void lv_draw_buf_flush_cache(lv_draw_buf_t*, const void*);
