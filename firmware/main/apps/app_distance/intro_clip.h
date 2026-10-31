#pragma once

#include <cstddef>
#include <cstdint>

namespace intro_clip {

struct Bytes {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// A read-only view of the embedded KINTRO1 pack. Parsing never allocates or
// casts potentially unaligned flash bytes to integer pointers.
class Clip {
public:
    static constexpr std::size_t max_pack_bytes = 1300000;

    bool parse(const std::uint8_t* bytes, std::size_t length) noexcept
    {
        *this = {};
        if (!bytes || length < 24 || length > max_pack_bytes) return false;
        constexpr std::uint8_t magic[]{'K', 'I', 'N', 'T', 'R', 'O', '1', 0};
        for (std::size_t i = 0; i < sizeof(magic); ++i) {
            if (bytes[i] != magic[i]) return false;
        }
        const auto width = u16(bytes + 8);
        const auto height = u16(bytes + 10);
        const auto fps = u16(bytes + 12);
        const auto count = u32(bytes + 16);
        const auto duration = u32(bytes + 20);
        if (width != 320 || height != 240 || fps < 1 || fps > 15 || u16(bytes + 14) != 0 ||
            count < 1 || count > 180 || duration < 1 || duration > 12000) return false;

        // Products are bounded above before arithmetic, including on 32-bit.
        const auto index_end = std::size_t{24} + std::size_t{count} * 8;
        if (index_end > length) return false;
        const auto video_ticks = std::uint64_t{count} * 1000;
        const auto duration_ticks = std::uint64_t{duration} * fps;
        const auto duration_difference = video_ticks > duration_ticks
                                             ? video_ticks - duration_ticks : duration_ticks - video_ticks;
        if (duration_difference > 1000) return false;  // At most one video frame.

        std::size_t frame_end = index_end;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto* entry = bytes + 24 + std::size_t{i} * 8;
            const auto offset = u32(entry);
            const auto size = u32(entry + 4);
            if (offset != frame_end || size < 4 || offset > length ||
                size > length - offset || !baseline_jpeg(bytes + offset, size, width, height)) return false;
            frame_end = std::size_t{offset} + size;
        }
        if (frame_end != length) return false;

        bytes_ = bytes;
        fps_ = fps;
        frame_count_ = count;
        duration_ms_ = duration;
        return true;
    }

    explicit operator bool() const noexcept { return bytes_ != nullptr; }
    std::uint16_t fps() const noexcept { return fps_; }
    std::uint32_t frame_count() const noexcept { return frame_count_; }
    std::uint32_t duration_ms() const noexcept { return duration_ms_; }

    Bytes frame(std::uint32_t index) const noexcept
    {
        if (!bytes_ || index >= frame_count_) return {};
        const auto* entry = bytes_ + 24 + std::size_t{index} * 8;
        return {bytes_ + u32(entry), u32(entry + 4)};
    }

private:
    static std::uint16_t u16(const std::uint8_t* p) noexcept
    {
        return static_cast<std::uint16_t>(p[0] | (std::uint16_t{p[1]} << 8));
    }

    static std::uint32_t u32(const std::uint8_t* p) noexcept
    {
        return p[0] | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
               (std::uint32_t{p[3]} << 24);
    }

    static std::uint16_t be16(const std::uint8_t* p) noexcept
    {
        return static_cast<std::uint16_t>((std::uint16_t{p[0]} << 8) | p[1]);
    }

    // Validate marker bounds and the decoder's baseline dimensions before any
    // image allocation. Entropy decoding remains the existing JPEG decoder's job.
    static bool baseline_jpeg(const std::uint8_t* p, std::size_t size,
                              std::uint16_t width, std::uint16_t height) noexcept
    {
        if (size < 4 || p[0] != 0xff || p[1] != 0xd8 || p[size - 2] != 0xff || p[size - 1] != 0xd9) return false;
        bool found_frame = false;
        bool found_scan = false;
        std::uint8_t frame_components = 0;
        std::size_t at = 2;
        while (at < size - 2) {
            if (p[at++] != 0xff) return false;
            while (at < size - 2 && p[at] == 0xff) ++at;
            if (at >= size - 2) return false;
            const auto marker = p[at++];
            if (marker == 0 || marker == 0xd8 || marker == 0xd9 || marker == 0x01 ||
                (marker >= 0xd0 && marker <= 0xd7) || at + 2 > size - 2) return false;
            const bool metadata = (marker >= 0xe0 && marker <= 0xef) || marker == 0xfe;
            if (!metadata && marker != 0xc0 && marker != 0xc4 && marker != 0xdb &&
                marker != 0xdd && marker != 0xda) return false;
            const auto segment = be16(p + at);
            if (segment < 2 || segment > size - 2 - at) return false;
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
                if (marker != 0xc0 || found_frame || segment < 8 || p[at + 2] != 8 ||
                    be16(p + at + 3) != height || be16(p + at + 5) != width) return false;
                const auto components = p[at + 7];
                if ((components != 1 && components != 3) || segment != 8 + 3 * components) return false;
                frame_components = components;
                found_frame = true;
            }
            if (marker == 0xda) {
                if (!found_frame || segment < 6) return false;
                const auto components = p[at + 2];
                if (components != frame_components || segment != 6 + 2 * components ||
                    p[at + segment - 3] != 0 || p[at + segment - 2] != 63 || p[at + segment - 1] != 0) return false;
                found_scan = true;
                at += segment;
                // A baseline image has one interleaved scan in this pack. Permit
                // byte stuffing/restart markers; reject extra frames/scans/EOIs.
                while (at < size - 2) {
                    if (p[at++] != 0xff) continue;
                    while (at < size - 2 && p[at] == 0xff) ++at;
                    if (at >= size - 2) return false;
                    const auto entropy_marker = p[at++];
                    if (entropy_marker != 0 && (entropy_marker < 0xd0 || entropy_marker > 0xd7)) return false;
                }
                break;
            }
            at += segment;
        }
        return found_frame && found_scan && at == size - 2;
    }

    const std::uint8_t* bytes_ = nullptr;
    std::uint16_t fps_ = 0;
    std::uint32_t frame_count_ = 0;
    std::uint32_t duration_ms_ = 0;
};

}  // namespace intro_clip
