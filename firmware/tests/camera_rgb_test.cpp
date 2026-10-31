#include <hal/board/camera_rgb.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using camera_rgb::ConvertYuv422ToRgb888;
using camera_rgb::Yuv422Layout;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void check_pixel(const uint8_t* rgb, int r, int g, int b, const char* message)
{
    check(rgb[0] == r && rgb[1] == g && rgb[2] == b, message);
}

void black_white_and_luma_order()
{
    size_t frame_capacity = 0;
    check(camera_rgb::Rgb888Size(320, 240, frame_capacity) && frame_capacity == 230400,
          "board RGB frame capacity");
    const std::array<uint8_t, 8> yuyv{16, 128, 235, 128, 81, 128, 145, 128};
    std::array<uint8_t, 12> rgb{};
    check(ConvertYuv422ToRgb888(yuyv.data(), yuyv.size(), 4, 1, 0, Yuv422Layout::Yuyv,
                               rgb.data(), rgb.size()), "tight YUYV rejected");
    check_pixel(rgb.data(), 0, 0, 0, "limited-range black");
    check_pixel(rgb.data() + 3, 255, 255, 255, "limited-range white / second Y position");
    check_pixel(rgb.data() + 6, 76, 76, 76, "first gray luma");
    check_pixel(rgb.data() + 9, 150, 150, 150, "second gray luma");
}

void color_and_byte_order()
{
    // Independent BT.601 limited-range reference points for R, G, B primaries.
    const std::array<uint8_t, 12> yuyv{81, 90, 81, 240, 145, 54, 145, 34, 41, 240, 41, 110};
    const std::array<uint8_t, 12> uyvy{90, 81, 240, 81, 54, 145, 34, 145, 240, 41, 110, 41};
    std::array<uint8_t, 18> packed_rgb{}, swapped_rgb{};
    check(ConvertYuv422ToRgb888(yuyv.data(), yuyv.size(), 6, 1, 0, Yuv422Layout::Yuyv,
                               packed_rgb.data(), packed_rgb.size()), "color YUYV rejected");
    check(ConvertYuv422ToRgb888(uyvy.data(), uyvy.size(), 6, 1, 0, Yuv422Layout::Uyvy,
                               swapped_rgb.data(), swapped_rgb.size()), "color UYVY rejected");
    check(packed_rgb == swapped_rgb, "YUYV / UYVY byte order mismatch");
    for (size_t pixel = 0; pixel < 2; ++pixel) {
        check_pixel(packed_rgb.data() + pixel * 3, 255, 0, 0, "red must be RGB, not BGR");
        check_pixel(packed_rgb.data() + (pixel + 2) * 3, 0, 255, 1, "green chroma order");
        check_pixel(packed_rgb.data() + (pixel + 4) * 3, 0, 0, 255, "blue chroma order");
    }
}

void packed_stride_and_guards()
{
    // Odd height is valid for 4:2:2. Last-row padding is not required.
    const std::array<uint8_t, 16> source{16, 128, 235, 128, 0xee, 0xee,
                                         81, 90, 81, 240, 0xee, 0xee,
                                         41, 240, 41, 110};
    const auto source_copy = source;
    std::array<uint8_t, 20> guarded;
    guarded.fill(0xa5);
    check(ConvertYuv422ToRgb888(source.data(), source.size(), 2, 3, 6, Yuv422Layout::Yuyv,
                               guarded.data() + 1, 18), "padded packed stride rejected");
    check(guarded.front() == 0xa5 && guarded.back() == 0xa5, "packed output crossed capacity");
    check(source == source_copy, "source modified");
    check_pixel(guarded.data() + 1, 0, 0, 0, "packed first row");
    check_pixel(guarded.data() + 7, 255, 0, 0, "packed second row / padding skipped");
    check_pixel(guarded.data() + 13, 0, 0, 255, "packed third row");
    guarded.fill(0xa5);
    check(!ConvertYuv422ToRgb888(source.data(), source.size() - 1, 2, 3, 6, Yuv422Layout::Yuyv,
                                guarded.data() + 1, 18), "short packed last row accepted");
    check(std::all_of(guarded.begin(), guarded.end(), [](uint8_t byte) { return byte == 0xa5; }),
          "invalid packed frame partially wrote output");
}

void planar_offsets_and_stride()
{
    // Y stride 6, U/V stride 3, width 4, height 3. Each plane starts after
    // the preceding plane's full padded span; V needs no final padding byte.
    const std::array<uint8_t, 35> planar{
        16, 235, 81, 81, 0xee, 0xee, 145, 145, 41, 41, 0xee, 0xee, 16, 235, 16, 235, 0xee, 0xee,
        128, 90, 0xee, 54, 240, 0xee, 128, 128, 0xee,
        128, 240, 0xee, 34, 110, 0xee, 128, 128};
    std::array<uint8_t, 38> guarded;
    guarded.fill(0xa5);
    check(ConvertYuv422ToRgb888(planar.data(), planar.size(), 4, 3, 6, Yuv422Layout::Planar,
                               guarded.data() + 1, 36), "padded planar frame rejected");
    check(guarded.front() == 0xa5 && guarded.back() == 0xa5, "planar output crossed capacity");
    const auto* rgb = guarded.data() + 1;
    check_pixel(rgb, 0, 0, 0, "planar black Y");
    check_pixel(rgb + 3, 255, 255, 255, "planar white Y");
    check_pixel(rgb + 6, 255, 0, 0, "planar U/V start offsets");
    check_pixel(rgb + 12, 0, 255, 1, "planar chroma row stride");
    check_pixel(rgb + 18, 0, 0, 255, "planar next chroma cell");
    check_pixel(rgb + 33, 255, 255, 255, "planar final cell");
    guarded.fill(0xa5);
    check(!ConvertYuv422ToRgb888(planar.data(), planar.size() - 1, 4, 3, 6, Yuv422Layout::Planar,
                                guarded.data() + 1, 36), "truncated V plane accepted");
    check(std::all_of(guarded.begin(), guarded.end(), [](uint8_t byte) { return byte == 0xa5; }),
          "truncated planar frame partially wrote output");

    const std::array<uint8_t, 8> tight{16, 235, 81, 81, 128, 90, 128, 240};
    std::array<uint8_t, 12> tight_rgb{};
    check(ConvertYuv422ToRgb888(tight.data(), tight.size(), 4, 1, 0, Yuv422Layout::Planar,
                               tight_rgb.data(), tight_rgb.size()), "tight planar frame rejected");
    check_pixel(tight_rgb.data() + 6, 255, 0, 0, "tight planar plane offsets");
}

void malformed_frames_preserve_destination()
{
    const std::array<uint8_t, 16> source{};
    std::array<uint8_t, 24> rgb;
    rgb.fill(0xa5);
    auto rejected = [&](size_t used, size_t width, size_t height, size_t stride,
                        Yuv422Layout layout, size_t capacity) {
        check(!ConvertYuv422ToRgb888(source.data(), used, width, height, stride, layout,
                                    rgb.data(), capacity), "malformed frame accepted");
        check(std::all_of(rgb.begin(), rgb.end(), [](uint8_t byte) { return byte == 0xa5; }),
              "validation failure changed destination");
    };
    rejected(8, 3, 1, 0, Yuv422Layout::Yuyv, 24);
    rejected(8, 0, 1, 0, Yuv422Layout::Yuyv, 24);
    rejected(8, 4, 0, 0, Yuv422Layout::Yuyv, 24);
    rejected(8, 4, 1, 7, Yuv422Layout::Yuyv, 24);
    rejected(7, 4, 1, 0, Yuv422Layout::Yuyv, 24);
    rejected(9, 4, 1, 0, Yuv422Layout::Yuyv, 24);
    rejected(8, 4, 1, 0, Yuv422Layout::Yuyv, 11);
    rejected(16, 4, 2, 3, Yuv422Layout::Planar, 24);
    rejected(16, 4, 2, 5, Yuv422Layout::Planar, 24);
    rejected(15, 4, 2, 4, Yuv422Layout::Planar, 24);
    rejected(16, 4, 2, 0, static_cast<Yuv422Layout>(99), 24);
    rejected(16, std::numeric_limits<size_t>::max() - 1, 2, 0, Yuv422Layout::Yuyv, 24);
    rejected(16, 2, 1, std::numeric_limits<size_t>::max(), Yuv422Layout::Planar, 24);
    rejected(16, 2, 1, std::numeric_limits<size_t>::max() - 1, Yuv422Layout::Planar, 24);
    rejected(16, 2, 2, std::numeric_limits<size_t>::max() - 1, Yuv422Layout::Yuyv, 24);
    check(!ConvertYuv422ToRgb888(nullptr, 8, 4, 1, 0, Yuv422Layout::Yuyv, rgb.data(), rgb.size()),
          "null source accepted");
    check(!ConvertYuv422ToRgb888(source.data(), 8, 4, 1, 0, Yuv422Layout::Yuyv, nullptr, rgb.size()),
          "null output accepted");
}

}  // namespace

int main()
{
    try {
        black_white_and_luma_order();
        color_and_byte_order();
        packed_stride_and_guards();
        planar_offsets_and_stride();
        malformed_frames_preserve_destination();
        std::cout << "camera_rgb_test: 5 groups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "camera_rgb_test: " << error.what() << '\n';
        return 1;
    }
}
