#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace camera_rgb {

enum class Yuv422Layout { Yuyv, Uyvy, Planar };

inline bool Rgb888Size(size_t width, size_t height, size_t& bytes)
{
    if (width == 0 || height == 0 || (width & 1) != 0 ||
        width > std::numeric_limits<size_t>::max() / 3 / height) {
        return false;
    }
    bytes = width * height * 3;
    return true;
}

inline uint8_t ClampRgb(int value)
{
    return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

// BT.601 limited-range YCbCr -> RGB, in R, G, B byte order.
inline void WriteRgb(uint8_t y, int u, int v, uint8_t* rgb)
{
    const int c = static_cast<int>(y) - 16;
    rgb[0] = ClampRgb((298 * c + 409 * v + 128) / 256);
    rgb[1] = ClampRgb((298 * c - 100 * u - 208 * v + 128) / 256);
    rgb[2] = ClampRgb((298 * c + 516 * u + 128) / 256);
}

// Planar means contiguous Y, U, V planes, with U/V stride = Y stride / 2.
// A zero stride is accepted only when bytesused is an exact tight frame.
// The caller owns separate source/destination storage. Validation completes
// before any destination byte is written; no allocation or SDK calls occur.
inline bool ConvertYuv422ToRgb888(const uint8_t* source, size_t bytesused, size_t width, size_t height,
                                size_t stride, Yuv422Layout layout, uint8_t* rgb, size_t capacity)
{
    size_t rgb_bytes = 0;
    if (!source || !rgb || !Rgb888Size(width, height, rgb_bytes) || capacity < rgb_bytes ||
        (layout != Yuv422Layout::Yuyv && layout != Yuv422Layout::Uyvy && layout != Yuv422Layout::Planar)) {
        return false;
    }
    const size_t tight_bytes = rgb_bytes / 3 * 2;
    const bool planar = layout == Yuv422Layout::Planar;
    const size_t row_bytes = planar ? width : width * 2;
    if (stride == 0) {
        if (bytesused != tight_bytes) return false;
        stride = row_bytes;
    }
    if (stride < row_bytes || stride > std::numeric_limits<size_t>::max() / height) return false;

    size_t u_offset = 0;
    size_t v_offset = 0;
    size_t chroma_stride = 0;
    size_t required = 0;
    if (planar) {
        if ((stride & 1) != 0) return false;
        chroma_stride = stride / 2;
        u_offset = stride * height;
        const size_t chroma_plane = chroma_stride * height;
        if (chroma_plane > (std::numeric_limits<size_t>::max() - u_offset) / 2) return false;
        v_offset = u_offset + chroma_plane;
        required = v_offset + chroma_stride * (height - 1) + width / 2;
    } else {
        required = stride * (height - 1) + row_bytes;
    }
    if (required > bytesused) return false;

    for (size_t row = 0; row < height; ++row) {
        const uint8_t* y_row = source + row * stride;
        uint8_t* output = rgb + row * width * 3;
        for (size_t x = 0; x < width; x += 2) {
            uint8_t y0, y1, u, v;
            if (planar) {
                y0 = y_row[x];
                y1 = y_row[x + 1];
                u = source[u_offset + row * chroma_stride + x / 2];
                v = source[v_offset + row * chroma_stride + x / 2];
            } else {
                const uint8_t* pair = y_row + x * 2;
                const bool uyvy = layout == Yuv422Layout::Uyvy;
                y0 = pair[uyvy ? 1 : 0];
                y1 = pair[uyvy ? 3 : 2];
                u = pair[uyvy ? 0 : 1];
                v = pair[uyvy ? 2 : 3];
            }
            WriteRgb(y0, static_cast<int>(u) - 128, static_cast<int>(v) - 128, output + x * 3);
            WriteRgb(y1, static_cast<int>(u) - 128, static_cast<int>(v) - 128, output + (x + 1) * 3);
        }
    }
    return true;
}

}  // namespace camera_rgb
