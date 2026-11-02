#include <apps/app_distance/intro_clip.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using Buffer = std::vector<std::uint8_t>;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void put16(Buffer& data, std::size_t at, std::uint16_t value)
{
    data[at] = static_cast<std::uint8_t>(value);
    data[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(Buffer& data, std::size_t at, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

// A complete baseline grayscale JPEG: 1200 8x8 zero-DC/EOB blocks, with
// one-symbol Huffman tables and a quantization table. It decodes to 320x240
// gray pixels; this is a real JPEG payload, not a replacement parser/decoder.
Buffer jpeg()
{
    Buffer result{0xff, 0xd8, 0xff, 0xdb, 0, 67, 0};
    result.insert(result.end(), 64, 1);
    const Buffer frame{0xff, 0xc0, 0, 11, 8, 0, 240, 1, 64, 1, 1, 0x11, 0};
    result.insert(result.end(), frame.begin(), frame.end());
    const Buffer huffman{0xff, 0xc4, 0, 38};
    result.insert(result.end(), huffman.begin(), huffman.end());
    for (const auto table : {0, 0x10}) {
        result.push_back(static_cast<std::uint8_t>(table));
        result.push_back(1);
        result.insert(result.end(), 15, 0);
        result.push_back(0);
    }
    const Buffer scan{0xff, 0xda, 0, 8, 1, 1, 0, 0, 63, 0};
    result.insert(result.end(), scan.begin(), scan.end());
    result.insert(result.end(), 300, 0);
    result.push_back(0xff);
    result.push_back(0xd9);
    return result;
}

Buffer fixture(std::uint32_t frames = 2, std::uint16_t fps = 2, std::uint32_t duration = 1000)
{
    const auto image = jpeg();
    const auto index_end = 24 + frames * 8;
    const auto images_end = index_end + frames * image.size();
    Buffer data(images_end, 0);
    const Buffer magic{'K', 'I', 'N', 'T', 'R', 'O', '1', 0};
    std::copy(magic.begin(), magic.end(), data.begin());
    put16(data, 8, 320);
    put16(data, 10, 240);
    put16(data, 12, fps);
    put32(data, 16, frames);
    put32(data, 20, duration);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto offset = index_end + i * image.size();
        put32(data, 24 + i * 8, static_cast<std::uint32_t>(offset));
        put32(data, 28 + i * 8, static_cast<std::uint32_t>(image.size()));
        std::copy(image.begin(), image.end(), data.begin() + offset);
    }
    return data;
}

void rejected(const Buffer& data, const char* message)
{
    intro_clip::Clip clip;
    check(!clip.parse(data.data(), data.size()), message);
    check(!clip && clip.frame_count() == 0 && clip.duration_ms() == 0 &&
          clip.frame(0).data == nullptr, "rejected parse retained flash pointers");
}

void valid_pack_exposes_flash_views()
{
    auto data = fixture();
    intro_clip::Clip clip;
    check(clip.parse(data.data(), data.size()), "valid baseline pack rejected");
    check(clip && clip.frame_count() == 2 && clip.fps() == 2 &&
          clip.duration_ms() == 1000, "metadata changed");
    const auto first = clip.frame(0);
    const auto second = clip.frame(1);
    check(first.data == data.data() + 40 && first.size == jpeg().size() &&
          second.data == first.data + first.size && second.data + second.size == data.data() + data.size(),
          "frame views copied data or lost ordered boundaries");
    check(!clip.frame(2).data && !clip.frame(std::numeric_limits<std::uint32_t>::max()).data,
          "out-of-range frame returned a pointer");
    data[0] = 0;
    check(!clip.parse(data.data(), data.size()) && !clip && !clip.frame(0).data,
          "failed reparse retained a previous valid view");
}

void headers_and_all_truncated_prefixes_fail_closed()
{
    const auto valid = fixture();
    intro_clip::Clip clip;
    check(!clip.parse(nullptr, valid.size()) && !clip.parse(nullptr, 0), "null pack accepted");
    for (std::size_t length = 0; length < valid.size(); ++length) {
        check(!clip.parse(valid.data(), length), "a truncated prefix was accepted");
    }
    for (std::size_t at = 0; at < 8; ++at) {
        auto bad = valid;
        bad[at] ^= 0x20;
        rejected(bad, "invalid magic accepted");
    }
    for (const auto field : {8u, 10u, 12u, 14u}) {
        auto bad = valid;
        put16(bad, field, 0);
        if (field == 14) put16(bad, field, 1);
        rejected(bad, "unsupported header accepted");
    }
    auto huge = valid;
    huge.resize(intro_clip::Clip::max_pack_bytes + 1);
    rejected(huge, "oversized pack accepted");
    auto trailing = valid;
    trailing.push_back(0);
    rejected(trailing, "trailing bytes after indexed images accepted");
}

void count_fps_and_duration_bounds_are_enforced()
{
    for (const auto value : {0u, 181u, std::numeric_limits<std::uint32_t>::max()}) {
        auto bad = fixture();
        put32(bad, 16, value);
        rejected(bad, "invalid frame count accepted");
    }
    for (const auto value : {0u, 12001u, std::numeric_limits<std::uint32_t>::max()}) {
        auto bad = fixture();
        put32(bad, 20, value);
        rejected(bad, "invalid duration accepted");
    }
    auto bad_fps = fixture();
    put16(bad_fps, 12, 16);
    rejected(bad_fps, "unsupported fps accepted");
    rejected(fixture(4, 2, 1000), "more than one frame of duration mismatch accepted");
    rejected(fixture(1, 2, 1001), "duration beyond one-frame tolerance accepted");
    intro_clip::Clip clip;
    for (auto valid : {fixture(3, 2, 1000), fixture(1, 2, 1000), fixture(180, 15, 12000),
                       fixture(12, 1, 12000), fixture(1, 15, 1)}) {
        check(clip.parse(valid.data(), valid.size()), "valid duration/count boundary rejected");
    }
}

void index_overflow_overlap_and_gaps_are_rejected()
{
    for (const auto offset : {0u, 24u, 39u, 41u, std::numeric_limits<std::uint32_t>::max()}) {
        auto bad = fixture();
        put32(bad, 24, offset);
        rejected(bad, "invalid first offset accepted");
    }
    for (const auto length : {0u, 3u, std::numeric_limits<std::uint32_t>::max()}) {
        auto bad = fixture();
        put32(bad, 28, length);
        rejected(bad, "invalid frame extent accepted");
    }
    auto overlap = fixture();
    put32(overlap, 32, 40);
    rejected(overlap, "overlapping/reordered frames accepted");
    auto gap = fixture();
    put32(gap, 32, static_cast<std::uint32_t>(41 + jpeg().size()));
    rejected(gap, "gap between indexed frames accepted");
}

void jpeg_markers_baseline_dimensions_and_entropy_are_checked()
{
    const auto image = jpeg();
    const auto image_at = std::size_t{40};
    for (const auto relative : {std::size_t{0}, std::size_t{1}, image.size() - 1}) {
        auto bad = fixture();
        bad[image_at + relative] = 0;
        rejected(bad, "invalid JPEG boundary accepted");
    }
    auto oversized_segment = fixture();
    oversized_segment[image_at + 4] = 0xff;
    oversized_segment[image_at + 5] = 0xff;
    rejected(oversized_segment, "out-of-bounds JPEG segment accepted");
    const auto sof = image_at + 71;
    for (const auto mutation : {0u, 1u, 2u, 3u}) {
        auto bad = fixture();
        if (mutation == 0) bad[sof + 1] = 0xc2;  // Progressive JPEG.
        if (mutation == 1) bad[sof + 4] = 12;    // Unsupported precision.
        if (mutation == 2) bad[sof + 6] = 239;   // Height mismatch.
        if (mutation == 3) bad[sof + 8] = 63;    // Width mismatch.
        rejected(bad, "unsupported JPEG frame header accepted");
    }
    auto bad_scan = fixture();
    bad_scan[image_at + 132] = 62;  // Baseline scan spectral end must be 63.
    rejected(bad_scan, "unsupported scan parameters accepted");
    auto hidden_marker = fixture();
    hidden_marker[image_at + 150] = 0xff;
    hidden_marker[image_at + 151] = 0xd8;
    rejected(hidden_marker, "nested JPEG/scan marker in entropy accepted");
    auto stuffed = fixture();
    stuffed[image_at + 150] = 0xff;
    stuffed[image_at + 151] = 0;
    intro_clip::Clip clip;
    check(clip.parse(stuffed.data(), stuffed.size()), "byte-stuffed entropy rejected");
    stuffed[image_at + 151] = 0xd0;
    check(clip.parse(stuffed.data(), stuffed.size()), "restart marker rejected");
}

void missing_extra_or_reordered_frame_data_is_rejected()
{
    auto extra = fixture(1, 1, 1000);
    extra.push_back(0);
    rejected(extra, "unindexed byte after last JPEG accepted");
    auto missing = fixture();
    missing.resize(missing.size() - jpeg().size());
    rejected(missing, "missing final indexed image accepted");
    auto reordered = fixture();
    put32(reordered, 24, static_cast<std::uint32_t>(40 + jpeg().size()));
    put32(reordered, 32, 40);
    rejected(reordered, "reordered frame index accepted");
    auto odd = fixture(1, 1, 1000);
    odd.insert(odd.end() - 2, 0);
    put32(odd, 28, static_cast<std::uint32_t>(jpeg().size() + 1));
    intro_clip::Clip clip;
    check(clip.parse(odd.data(), odd.size()), "odd JPEG size rejected despite byte-wise access");
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        valid_pack_exposes_flash_views();
        headers_and_all_truncated_prefixes_fail_closed();
        count_fps_and_duration_bounds_are_enforced();
        index_overflow_overlap_and_gaps_are_rejected();
        jpeg_markers_baseline_dimensions_and_entropy_are_checked();
        missing_extra_or_reordered_frame_data_is_rejected();
        for (int i = 1; i < argc; ++i) {
            std::ifstream file(argv[i], std::ios::binary | std::ios::ate);
            check(static_cast<bool>(file), "real embedded pack unavailable");
            const auto size = file.tellg();
            check(size > 0 && size <= static_cast<std::streamoff>(intro_clip::Clip::max_pack_bytes),
                  "real embedded pack has invalid length");
            Buffer bytes(static_cast<std::size_t>(size));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            check(static_cast<bool>(file), "real embedded pack truncated on read");
            intro_clip::Clip clip;
            check(clip.parse(bytes.data(), bytes.size()), "real embedded pack rejected by production parser");
            for (std::uint32_t frame = 0; frame < clip.frame_count(); ++frame)
                check(clip.frame(frame).size > 0, "real embedded frame unavailable");
            std::cout << "real clip: " << clip.frame_count() << " frames / " << clip.duration_ms() << "ms\n";
        }
        std::cout << "intro_clip: 6 groups passed\n";
    } catch (const std::exception& error) {
        std::cerr << "intro_clip: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
