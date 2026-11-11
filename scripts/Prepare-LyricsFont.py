#!/usr/bin/env python3
"""Preserve 4bpp source antialiasing as block-compressed 2bpp LVGL glyphs.

No font download or font rasterizer is used. Run with --check to verify the
checked-in font and manifest against the current input without writing files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import zlib


ROOT = Path(__file__).resolve().parents[1]
INPUT = ROOT / "StackChan/firmware/managed_components/78__xiaozhi-fonts/src/font_puhui_20_4.c"
OUTPUT = ROOT / "StackChan/firmware/main/apps/common/lyrics/font_cjk_20.c"
MANIFEST = OUTPUT.with_name("font_manifest.json")
BLOCK_GLYPHS = 48
GLYPH_FIELDS = ("bitmap_index", "adv_w", "box_w", "box_h", "ofs_x", "ofs_y")
CMAP_FIELDS = (
    "range_start", "range_length", "glyph_id_start", "unicode_list",
    "glyph_id_ofs_list", "list_length", "type",
)
SAMPLES = "Aé歌詞词爱你我音乐播放暂停蓝牙"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def uncomment(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def array(text: str, name: str) -> re.Match[str]:
    matches = list(re.finditer(r"\b" + re.escape(name) + r"\[\]\s*=\s*\{(.*?)\};", text, re.S))
    require(len(matches) == 1, f"Expected exactly one {name} array")
    return matches[0]


def numbers(body: str, limit: int) -> list[int]:
    values = []
    for token in uncomment(body).split(","):
        token = token.strip()
        if not token:
            continue
        require(re.fullmatch(r"(?:0[xX][0-9a-fA-F]+|\d+)", token) is not None,
                f"Unsupported integer expression: {token[:60]}")
        value = int(token, 16 if token.lower().startswith("0x") else 10)
        require(0 <= value <= limit, f"Array value exceeds {limit}")
        values.append(value)
    return values


def records(body: str, expected: tuple[str, ...]) -> list[dict[str, str]]:
    body = uncomment(body)
    result = []
    for match in re.finditer(r"\{([^{}]*)\}", body):
        fields = re.findall(r"\.(\w+)\s*=\s*([^,]+)", match[1])
        require(tuple(key for key, _ in fields) == expected, "Unexpected record fields/order")
        remainder = re.sub(r"\.\w+\s*=\s*[^,]+,?", "", match[1]).strip()
        require(not remainder, "Unsupported record expression")
        result.append({key: value.strip() for key, value in fields})
    require(result, "No records found")
    require(not re.sub(r"\{[^{}]*\}|[\s,]", "", body), "Unsupported array structure")
    return result


def glyphs(text: str) -> list[dict[str, int]]:
    result = []
    for record in records(array(text, "glyph_dsc")[1], GLYPH_FIELDS):
        require(all(re.fullmatch(r"-?\d+", value) for value in record.values()),
                "Unsupported glyph integer")
        result.append({key: int(value) for key, value in record.items()})
    return result


def character_map(text: str, glyph_count: int) -> dict[int, int]:
    result = {}
    previous_end = 0
    cmaps = records(array(text, "cmaps")[1], CMAP_FIELDS)
    require(len(cmaps) == 29, "Input cmap count changed; review the converter")
    for cmap in cmaps:
        start, length, first, count = (int(cmap[key]) for key in
                                      ("range_start", "range_length", "glyph_id_start", "list_length"))
        require(start >= previous_end and length > 0, "Overlapping/empty cmap range")
        previous_end = start + length
        kind = cmap["type"]
        offsets = None
        if cmap["glyph_id_ofs_list"] != "NULL":
            offsets = numbers(array(text, cmap["glyph_id_ofs_list"])[1], 65535)
        if kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            require(cmap["unicode_list"] == "NULL" and offsets is None and count == 0,
                    "Unexpected tiny cmap data")
            entries = [(relative, relative) for relative in range(length)]
        elif kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL":
            require(cmap["unicode_list"] == "NULL" and offsets is not None
                    and len(offsets) == count == length and offsets[0] == 0,
                    "Unexpected full cmap data")
            # LVGL treats zero offsets after the first entry as missing glyphs.
            entries = [(relative, offset) for relative, offset in enumerate(offsets)
                       if relative == 0 or offset != 0]
        elif kind == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            require(cmap["unicode_list"] != "NULL" and offsets is None,
                    "Unexpected sparse cmap data")
            unicodes = numbers(array(text, cmap["unicode_list"])[1], 65535)
            require(len(unicodes) == count and unicodes == sorted(set(unicodes)),
                    "Sparse cmap is not sorted/unique or its length differs")
            entries = [(relative, index) for index, relative in enumerate(unicodes)]
        else:
            raise ValueError(f"Unsupported cmap type: {kind}")
        for relative, offset in entries:
            gid = first + offset
            codepoint = start + relative
            require(0 <= relative < length and 0 < gid < glyph_count,
                    "Cmap entry exceeds the range or glyph table")
            require(codepoint <= 0x10FFFF and not 0xD800 <= codepoint <= 0xDFFF,
                    "Cmap contains invalid Unicode")
            require(codepoint not in result, "Duplicate mapped codepoint")
            result[codepoint] = gid
    return result


def c_array(name: str, values: list[int] | bytes, kind: str = "uint8_t") -> str:
    per_line = 20 if kind == "uint8_t" else 12
    lines = [f"\nstatic const {kind} {name}[] = {{\n"]
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        lines.append("    " + ", ".join(f"0x{value:02x}" if kind == "uint8_t" else str(value) for value in chunk) + ",\n")
    return "".join(lines) + "};\n"


def runtime_source(cache_bytes: int, glyph_count: int) -> str:
    return r'''
/* All access is serialized by the caller's LVGL lock. Keep the CPU-only
 * decoder/cache in PSRAM for the firmware lifetime, preserving internal DMA
 * memory for the Bluetooth controller. An allocation failure has no internal
 * fallback and is retried on the next valid request. Host previews use static
 * storage. A packed pointer survives until the next successful or failed
 * attempt to decode a different block. No shared A8 bitmap is used. */
typedef struct {
    tinfl_decompressor decoder;
    uint8_t cache[CACHE_BYTES];
    uint16_t cached_block;
} FontWorkspace;

static FontWorkspace* font_get_workspace(void)
{
#ifdef ESP_PLATFORM
    static FontWorkspace* workspace;
    if(!workspace) {
        workspace = (FontWorkspace*)heap_caps_malloc(sizeof(FontWorkspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if(workspace) workspace->cached_block = UINT16_MAX;
    }
    return workspace;
#else
    static FontWorkspace workspace = {.cached_block = UINT16_MAX};
    return &workspace;
#endif
}

const uint8_t* botifier_cjk_20_bitmap(uint32_t glyph_id)
{
    if(glyph_id == 0 || glyph_id >= GLYPH_COUNT) return NULL;
    const lv_font_fmt_txt_glyph_dsc_t* glyph = &glyph_dsc[glyph_id];
    const uint32_t pixels = (uint32_t)glyph->box_w * glyph->box_h;
    if(pixels == 0) return NULL;
    const uint32_t block = glyph_id / BLOCK_GLYPHS;
    const uint32_t blocks = sizeof(font_block_sizes) / sizeof(font_block_sizes[0]);
    if(block >= blocks) return NULL;
    const uint32_t start = font_block_offsets[block];
    const uint32_t end = font_block_offsets[block + 1];
    const uint32_t decoded_size = font_block_sizes[block];
    const uint32_t offset = glyph->bitmap_index;
    const uint32_t bytes = (pixels + 3) / 4;
    if(start >= end || end > sizeof(glyph_bitmap) || decoded_size > CACHE_BYTES ||
       offset > decoded_size || bytes > decoded_size - offset) return NULL;
    FontWorkspace* workspace = font_get_workspace();
    if(!workspace) return NULL;
    if(workspace->cached_block != block) {
        workspace->cached_block = UINT16_MAX;
        tinfl_init(&workspace->decoder);
        size_t input_size = end - start, output_size = decoded_size;
        const tinfl_status status = tinfl_decompress(&workspace->decoder, glyph_bitmap + start,
            &input_size, workspace->cache, workspace->cache, &output_size,
            TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if(status != TINFL_STATUS_DONE || input_size != end - start || output_size != decoded_size)
            return NULL;
        workspace->cached_block = (uint16_t)block;
    }
    return workspace->cache + offset;
}

static const void* botifier_cjk_20_get_bitmap(lv_font_glyph_dsc_t* glyph, lv_draw_buf_t* buffer)
{
    if(!glyph || glyph->gid.index == 0 || glyph->gid.index >= GLYPH_COUNT) return NULL;
    const lv_font_fmt_txt_glyph_dsc_t* original = &glyph_dsc[glyph->gid.index];
    if(!original->box_w || !original->box_h || glyph->box_w != original->box_w || glyph->box_h != original->box_h)
        return NULL;
    if(glyph->req_raw_bitmap) return botifier_cjk_20_bitmap(glyph->gid.index);
    if(!buffer || !buffer->data || buffer->header.cf != LV_COLOR_FORMAT_A8 ||
       buffer->header.w < original->box_w || buffer->header.h < original->box_h ||
       buffer->header.stride < original->box_w ||
       (uint64_t)(original->box_h - 1) * buffer->header.stride + original->box_w > buffer->data_size)
        return NULL;
    const uint8_t* pixels = botifier_cjk_20_bitmap(glyph->gid.index);
    if(!pixels) return NULL;
    uint32_t pixel = 0;
    for(uint32_t y = 0; y < original->box_h; ++y) {
        for(uint32_t x = 0; x < original->box_w; ++x, ++pixel) {
            const uint8_t alpha = (pixels[pixel / 4] >> (6 - 2 * (pixel % 4))) & 3;
            buffer->data[y * buffer->header.stride + x] = alpha * 85;
        }
    }
    lv_draw_buf_flush_cache(buffer, NULL);
    return buffer;
}
'''.replace("CACHE_BYTES", str(cache_bytes)).replace("GLYPH_COUNT", str(glyph_count)).replace("BLOCK_GLYPHS", str(BLOCK_GLYPHS))


def convert() -> tuple[bytes, bytes, dict]:
    input_bytes = INPUT.read_bytes()
    source = input_bytes.decode("utf-8").replace("\r\n", "\n")
    require(len(re.findall(r"\.bpp\s*=\s*4\s*,", source)) == 1, "Input must be 4 bpp")
    require(len(re.findall(r"\.bitmap_format\s*=\s*0\s*,", source)) == 1, "Input must be uncompressed")
    require(not re.search(r"\.stride\s*=", source), "Explicit row stride needs separate conversion")
    bitmap_match = array(source, "glyph_bitmap")
    bitmap = bytes(numbers(bitmap_match[1], 255))
    descriptors = glyphs(source)
    require(len(descriptors) == 6650, "Expected 6649 glyphs plus reserved glyph 0")
    mapping = character_map(source, len(descriptors))
    packed_glyphs = []
    source_cursor = 0
    blank_count = intermediate_pixels = total_pixels = quant_error = max_error = 0
    for gid, glyph in enumerate(descriptors):
        pixels = glyph["box_w"] * glyph["box_h"]
        start, end = glyph["bitmap_index"], glyph["bitmap_index"] + (pixels + 1) // 2
        require(start == source_cursor and 0 <= start <= end <= len(bitmap), f"Invalid source range: {gid}")
        source_cursor = end
        if pixels % 2:
            require(bitmap[end - 1] & 15 == 0, f"Nonzero input padding: {gid}")
        packed = bytearray((pixels + 3) // 4)
        for pixel in range(pixels):
            original = (bitmap[start + pixel // 2] >> (4 if pixel % 2 == 0 else 0)) & 15
            alpha = (original + 2) // 5
            packed[pixel // 4] |= alpha << (6 - 2 * (pixel % 4))
            intermediate_pixels += 0 < alpha < 3
            error = abs(original * 17 - alpha * 85)
            quant_error += error
            max_error = max(max_error, error)
        total_pixels += pixels
        blank_count += pixels == 0
        packed_glyphs.append(bytes(packed))
    require(source_cursor == len(bitmap), "Unreferenced source bitmap data")
    compressed, block_offsets, block_sizes, glyph_offsets = bytearray(), [0], [], []
    for first in range(0, len(descriptors), BLOCK_GLYPHS):
        block = bytearray()
        for packed in packed_glyphs[first:first + BLOCK_GLYPHS]:
            glyph_offsets.append(len(block))
            block.extend(packed)
        require(len(block) <= 65535, "Block exceeds the uint16 decoded-size index")
        encoded = zlib.compress(bytes(block), level=9)
        require(zlib.decompress(encoded) == block, "Block compression roundtrip failed")
        compressed.extend(encoded)
        block_offsets.append(len(compressed))
        block_sizes.append(len(block))
    output = source[:bitmap_match.start(1)] + "\n" + c_array("unused", compressed).split("{\n", 1)[1].rsplit("};", 1)[0] + source[bitmap_match.end(1):]
    bitmap_end = array(output, "glyph_bitmap").end()
    output = output[:bitmap_end] + "\n" + c_array("font_block_offsets", block_offsets, "uint32_t") + c_array("font_block_sizes", block_sizes, "uint16_t") + output[bitmap_end:]
    descriptor_match = array(output, "glyph_dsc")
    iterator = iter(glyph_offsets)
    changed, replaced = re.subn(r"(\.bitmap_index\s*=\s*)\d+", lambda match: match[1] + str(next(iterator)), descriptor_match[1])
    require(replaced == len(descriptors), "Not all glyph indices were rewritten")
    output = output[:descriptor_match.start(1)] + changed + output[descriptor_match.end(1):]
    output = output.replace("FONT_PUHUI_20_4", "BOTIFIER_CJK_20")
    output, names = re.subn(r"(?m)^((?:const )?lv_font_t )font_puhui_20_4( = \{)$", r"\1botifier_cjk_20\2", output)
    require(names == 2, "Expected version-dependent public font declarations")
    output = re.sub(r"(\.bpp\s*=\s*)4(\s*,)", r"\g<1>2\2", output)
    output, callbacks = re.subn(r"(\.get_glyph_bitmap\s*=\s*)lv_font_get_bitmap_fmt_txt", r"\1botifier_cjk_20_get_bitmap", output)
    require(callbacks == 1, "Bitmap callback substitution failed")
    font_marker = output.index("/*Initialize a public general font descriptor*/")
    output = output[:font_marker] + runtime_source(max(block_sizes), len(descriptors)) + "\n" + output[font_marker:]
    output = (
        "/* Generated by scripts/Prepare-LyricsFont.py. Do not edit by hand.\n"
        " * Original 4bpp coverage -> nearest alpha 0/85/170/255. This quantization is lossy;\n"
        " * independent zlib blocks preserve every resulting 2bpp pixel exactly.\n"
        f" * {BLOCK_GLYPHS} glyphs/block; packed source is continuous MSB-first and glyph-byte-aligned.\n"
        " * Descriptor bitmap_index is an offset inside its block, NOT glyph_bitmap[].\n"
        " * ESP32-S3 ROM tinfl performs bounded decompression; no additional library.\n"
        " * Source component license declaration: MIT. Original 4bpp header follows.\n"
        " */\n#include \"font_cjk_20.h\"\n#include <miniz.h>\n"
        "#ifdef ESP_PLATFORM\n#include <esp_heap_caps.h>\n#endif\n" + output
    )
    emitted_bitmap = bytes(numbers(array(output, "glyph_bitmap")[1], 255))
    emitted_offsets = numbers(array(output, "font_block_offsets")[1], 0xFFFFFFFF)
    emitted_sizes = numbers(array(output, "font_block_sizes")[1], 65535)
    emitted_glyphs = glyphs(output)
    require(emitted_bitmap == compressed and emitted_offsets == block_offsets and emitted_sizes == block_sizes,
            "Emitted compressed data/index differs")
    checked_pixels = 0
    for block, first in enumerate(range(0, len(descriptors), BLOCK_GLYPHS)):
        decoded = zlib.decompress(emitted_bitmap[emitted_offsets[block]:emitted_offsets[block + 1]])
        require(len(decoded) == emitted_sizes[block], "Emitted block length differs")
        for gid in range(first, min(first + BLOCK_GLYPHS, len(descriptors))):
            before, after = descriptors[gid], emitted_glyphs[gid]
            require(after == {**before, "bitmap_index": glyph_offsets[gid]}, f"Glyph metrics changed: {gid}")
            count, offset = before["box_w"] * before["box_h"], after["bitmap_index"]
            actual = decoded[offset:offset + (count + 3) // 4]
            require(actual == packed_glyphs[gid], f"Emitted glyph differs: {gid}")
            if count % 4:
                require(actual[-1] & ((1 << (2 * (4 - count % 4))) - 1) == 0, f"Nonzero output padding: {gid}")
            for pixel in range(count):
                original = (bitmap[before["bitmap_index"] + pixel // 2] >> (4 if pixel % 2 == 0 else 0)) & 15
                quantized = (actual[pixel // 4] >> (6 - 2 * (pixel % 4))) & 3
                require(quantized == (original + 2) // 5, f"Pixel conversion differs: {gid}/{pixel}")
                checked_pixels += 1
    require(character_map(output, len(descriptors)) == mapping, "Character mapping changed")
    samples = {}
    for character in SAMPLES:
        gid = mapping.get(ord(character), 0)
        require(gid and descriptors[gid]["box_w"] * descriptors[gid]["box_h"], f"Representative glyph missing: {character}")
        samples[character] = {"codepoint": f"U+{ord(character):04X}", "glyph_id": gid}
    output_bytes = output.encode("utf-8")
    manifest = {
        "schema_version": 2, "source": INPUT.relative_to(ROOT).as_posix(),
        "source_sha256": hashlib.sha256(input_bytes).hexdigest(),
        "source_component": "78/xiaozhi-fonts 1.6.0", "source_component_declared_license": "MIT",
        "output": OUTPUT.relative_to(ROOT).as_posix(), "output_sha256": hashlib.sha256(output_bytes).hexdigest(),
        "symbol": "botifier_cjk_20", "algorithm": "Nearest four-level alpha from original 4bpp; independently zlib-compressed glyph blocks",
        "quantization": "(original_4bit_alpha + 2) // 5", "alpha_levels": [0, 85, 170, 255],
        "compression_lossless_relative_to_quantized_pixels": True, "quantization_lossless": False,
        "input_bpp": 4, "output_bpp": 2, "glyph_count": len(mapping),
        "glyph_descriptor_count_including_reserved": len(descriptors), "blank_glyph_count_including_reserved": blank_count,
        "mapped_codepoint_count": len(mapping), "cmap_count": 29,
        "input_bitmap_bytes": len(bitmap), "plain_output_bitmap_bytes": sum(map(len, packed_glyphs)),
        "output_bitmap_bytes": len(compressed), "block_glyphs": BLOCK_GLYPHS, "block_count": len(block_sizes),
        "block_index_bytes": 4 * len(block_offsets) + 2 * len(block_sizes),
        "cache_bytes": max(block_sizes), "rom_decoder_workspace_bytes_esp32s3": 10992,
        "workspace_storage": "ESP_PLATFORM: lazy SPIRAM|8BIT allocation, no internal fallback; host: static",
        "rom_decoder": "ESP-IDF miniz.h tinfl_decompress; PARSE_ZLIB_HEADER | USING_NON_WRAPPING_OUTPUT_BUF",
        "intermediate_alpha_pixels": intermediate_pixels, "validated_pixels": checked_pixels,
        "maximum_alpha_error": max_error, "mean_absolute_alpha_error": quant_error / total_pixels,
        "representative_utf8_mappings": samples,
        "validation": ["all source ranges contiguous and in bounds", "all source/output padding zero",
            "every emitted block independently inflates to its exact expected size", "every pixel equals nearest 2bpp quantization of original 4bpp coverage",
            "all glyph metrics and cmaps preserved", "all compressed indices and glyph offsets bounded"],
    }
    return output_bytes, (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"), manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify outputs; do not modify files")
    args = parser.parse_args()
    try:
        font, manifest_bytes, manifest = convert()
        for path, contents in ((OUTPUT, font), (MANIFEST, manifest_bytes)):
            if args.check:
                require(path.is_file() and path.read_bytes() == contents,
                        f"Stale/missing generated file: {path.relative_to(ROOT)}")
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(contents)
        print(f"{'Verified' if args.check else 'Generated'} {manifest['glyph_count']} glyphs: "
              f"{manifest['input_bitmap_bytes']} -> {manifest['output_bitmap_bytes']} bitmap bytes; "
              f"{manifest['mapped_codepoint_count']} mapped codepoints; "
              f"{len(manifest['representative_utf8_mappings'])} UTF-8 samples")
        print(f"Font SHA256: {manifest['output_sha256']}")
        return 0
    except (OSError, ValueError, UnicodeError) as error:
        print(f"Font conversion failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
