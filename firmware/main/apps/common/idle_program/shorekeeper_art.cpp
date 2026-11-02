#include "shorekeeper_art.h"
#include <array>

extern const std::uint8_t shorekeeper_start[] asm("_binary_shorekeeper_bin_start");
extern const std::uint8_t shorekeeper_end[] asm("_binary_shorekeeper_bin_end");

namespace idle_program::art {

const lv_image_dsc_t* shorekeeper_frame(std::size_t index) noexcept
{
    const auto begin = reinterpret_cast<std::uintptr_t>(shorekeeper_start);
    const auto end = reinterpret_cast<std::uintptr_t>(shorekeeper_end);
    if (index >= frame_count || end < begin || end - begin != frame_count * frame_bytes) return nullptr;

    // Static flash-backed BGRA pixels, with no decoder or per-frame allocation.
    static const auto frames = [] {
        std::array<lv_image_dsc_t, frame_count> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            auto& frame = result[i];
            frame.header.magic = LV_IMAGE_HEADER_MAGIC;
            frame.header.cf = LV_COLOR_FORMAT_ARGB8888;
            frame.header.w = width;
            frame.header.h = height;
            frame.header.stride = width * 4;
            frame.data_size = frame_bytes;
            frame.data = shorekeeper_start + i * frame_bytes;
        }
        return result;
    }();
    return &frames[index];
}

} // namespace idle_program::art
