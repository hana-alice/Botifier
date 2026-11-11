#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include <esp_heap_caps.h>
// Compile the complete generated production font, including its real callback.
#include "../main/apps/common/lyrics/font_cjk_20.c"
}

namespace {
unsigned checks = 0, inflate_calls = 0, flush_calls = 0, fail_allocations = 0;
bool fail_inflate = false;
struct Allocation { size_t bytes; uint32_t capabilities; };
std::vector<Allocation> allocations;
alignas(FontWorkspace) std::array<uint8_t, sizeof(FontWorkspace)> allocation_storage;

void check(bool result, const char* expression, int line)
{
    ++checks;
    if (!result) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)

lv_font_glyph_dsc_t describe(uint32_t gid)
{
    lv_font_glyph_dsc_t result{};
    result.gid.index = gid;
    result.box_w = glyph_dsc[gid].box_w;
    result.box_h = glyph_dsc[gid].box_h;
    return result;
}

void exercise()
{
    CHECK(sizeof(tinfl_decompressor) == 10992);
    CHECK(botifier_cjk_20_bitmap(0) == nullptr);
    CHECK(botifier_cjk_20_bitmap(6650) == nullptr);
    CHECK(botifier_cjk_20_bitmap(UINT32_MAX) == nullptr);
    for (uint32_t gid = 1; gid < 6650; ++gid) {
        if (!glyph_dsc[gid].box_w || !glyph_dsc[gid].box_h)
            CHECK(botifier_cjk_20_bitmap(gid) == nullptr);
    }
    CHECK(allocations.empty() && inflate_calls == 0);
    auto glyph = describe(37); // 'A', from the generated manifest.
    const auto stride = uint32_t(glyph.box_w + 3);
    std::vector<uint8_t> output(stride * glyph.box_h + 8, 0xa5);
    lv_draw_buf_t buffer{{LV_COLOR_FORMAT_A8, glyph.box_w, glyph.box_h, stride},
                         uint32_t(output.size()), output.data()};
    auto invalid = glyph;
    invalid.gid.index = 6650;
    CHECK(botifier_cjk_20.get_glyph_bitmap(&invalid, &buffer) == nullptr);
    CHECK(botifier_cjk_20.get_glyph_bitmap(&glyph, nullptr) == nullptr);
    CHECK(allocations.empty() && inflate_calls == 0 && flush_calls == 0);

#ifdef ESP_PLATFORM
    // Failure must not borrow internal RAM, initialize a decoder, or touch A8.
    fail_allocations = 2;
    CHECK(botifier_cjk_20_bitmap(37) == nullptr);
    CHECK(allocations.size() == 1 && inflate_calls == 0);
    CHECK(botifier_cjk_20.get_glyph_bitmap(&glyph, &buffer) == nullptr);
    CHECK(allocations.size() == 2 && inflate_calls == 0 && flush_calls == 0);
    CHECK(std::all_of(output.begin(), output.end(), [](auto byte) { return byte == 0xa5; }));
#endif

    const auto* packed = botifier_cjk_20_bitmap(37);
    CHECK(packed != nullptr && inflate_calls == 1);
#ifdef ESP_PLATFORM
    CHECK(allocations.size() == 3); // Two failures, then exactly one success.
    for (const auto& allocation : allocations) {
        CHECK(allocation.bytes == allocation_storage.size());
        CHECK(allocation.capabilities == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    CHECK(packed >= allocation_storage.data() && packed < allocation_storage.data() + allocation_storage.size());
#else
    CHECK(allocations.empty()); // Host previews retain a static workspace.
#endif
    const auto allocation_count = allocations.size();
    CHECK(botifier_cjk_20_bitmap(37) == packed && inflate_calls == 1);
    CHECK(botifier_cjk_20_bitmap(38) == packed + glyph_dsc[38].bitmap_index - glyph_dsc[37].bitmap_index);
    CHECK(inflate_calls == 1 && allocations.size() == allocation_count);

    CHECK(botifier_cjk_20.get_glyph_bitmap(&glyph, &buffer) == &buffer);
    CHECK(inflate_calls == 1 && flush_calls == 1 && allocations.size() == allocation_count);
    for (uint32_t y = 0; y < glyph.box_h; ++y) {
        for (uint32_t x = 0; x < stride; ++x) {
            const auto expected = x < glyph.box_w ? ((y * glyph.box_w + x) % 4) * 85 : 0xa5;
            CHECK(output[y * stride + x] == expected);
        }
    }
    CHECK(std::all_of(output.end() - 8, output.end(), [](auto byte) { return byte == 0xa5; }));
    glyph.req_raw_bitmap = true;
    CHECK(botifier_cjk_20.get_glyph_bitmap(&glyph, nullptr) == packed);
    CHECK(inflate_calls == 1 && flush_calls == 1);

    // A failed different-block decode is retryable using the same allocation.
    fail_inflate = true;
    CHECK(botifier_cjk_20_bitmap(169) == nullptr);
    CHECK(inflate_calls == 2 && allocations.size() == allocation_count);
    fail_inflate = false;
    CHECK(botifier_cjk_20_bitmap(169) != nullptr);
    CHECK(inflate_calls == 3 && allocations.size() == allocation_count);
    CHECK(botifier_cjk_20_bitmap(169) != nullptr && inflate_calls == 3);
}
} // namespace

extern "C" void* heap_caps_malloc(size_t bytes, uint32_t capabilities)
{
    allocations.push_back({bytes, capabilities});
    CHECK(bytes <= allocation_storage.size());
    if (fail_allocations) { --fail_allocations; return nullptr; }
    allocation_storage.fill(0xa5); // Allocation is deliberately not zeroed.
    return allocation_storage.data();
}

extern "C" tinfl_status tinfl_decompress(tinfl_decompressor* decoder, const uint8_t* input,
    size_t* input_size, uint8_t* output_start, uint8_t* output_next, size_t* output_size, uint32_t flags)
{
    ++inflate_calls;
    CHECK(decoder->m_state == 0 && input != nullptr && *input_size > 0);
    CHECK(output_start == output_next && *output_size <= sizeof(FontWorkspace::cache));
    CHECK(flags == (TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF));
    decoder->m_state = 123;
    std::memset(output_start, 0x1b, *output_size); // 2bpp coverage 0, 1, 2, 3.
    return fail_inflate ? TINFL_STATUS_FAILED : TINFL_STATUS_DONE;
}

extern "C" bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t*, lv_font_glyph_dsc_t*, uint32_t, uint32_t)
{ return false; } // This test uses the real generated glyph IDs and descriptors.
extern "C" void lv_draw_buf_flush_cache(lv_draw_buf_t*, const void*) { ++flush_calls; }

int main()
{
    try {
        exercise();
        std::cout << "lyrics font workspace: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
