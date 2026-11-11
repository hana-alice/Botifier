#!/usr/bin/env python3
"""Generate/check the offline Han table used only for lyrics metadata matching.

Requires Windows and the Python standard library. By default, verify the
checked-in table without writing it; pass --write to regenerate it.
"""

from __future__ import annotations

import argparse
from bisect import bisect_left
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "StackChan/firmware/main/apps/common/lyrics/han_simplified.inc"
API_URL = "https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringex"
LOCALE = "zh-CN"
LCMAP_SIMPLIFIED_CHINESE = 0x02000000
CJK_RANGES = ((0x3400, 0x4DBF), (0x4E00, 0x9FFF))
EXPECTED_PAIRS = 2473
EXPECTED_MAPPING_SHA256 = "da974d21344ad6f624f5f56c4fc15040a8453f7ba976406e96aa0868fe94c440"
SAMPLES = (
    "周傑倫 / 七里香 / 葉惠美",
    "張學友 / 吻別 / 愛與痛的邊緣",
    "陳奕迅 / 愛情轉移 / 富士山下",
    "臺灣與台灣：後來、乾杯、發現、頭髮",
    "簡繁混合 歌詞 lyrics 123 / 音樂 🎵",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def is_cjk(codepoint: int) -> bool:
    return any(start <= codepoint <= end for start, end in CJK_RANGES)


def windows_mapper():
    require(sys.platform == "win32", "LCMapStringEx generation/checking requires Windows")
    require(ctypes.sizeof(ctypes.c_wchar) == 2, "Expected Windows UTF-16 wchar_t")
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    lcmap = kernel32.LCMapStringEx
    lcmap.argtypes = (
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.LPCWSTR, ctypes.c_int,
        wintypes.LPWSTR, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p,
        wintypes.LPARAM,
    )
    lcmap.restype = ctypes.c_int

    def convert(source: str) -> str:
        if not source:
            return ""
        # cchSrc/cchDest count UTF-16 code units, including surrogate pairs.
        units = len(source.encode("utf-16-le")) // 2
        required = lcmap(LOCALE, LCMAP_SIMPLIFIED_CHINESE, source, units,
                         None, 0, None, None, 0)
        if required == 0:
            raise ctypes.WinError(ctypes.get_last_error())
        destination = ctypes.create_unicode_buffer(required)
        written = lcmap(LOCALE, LCMAP_SIMPLIFIED_CHINESE, source, units,
                        destination, required, None, None, 0)
        if written == 0:
            raise ctypes.WinError(ctypes.get_last_error())
        require(written == required, "LCMapStringEx output size changed between calls")
        return ctypes.wstring_at(destination, written)

    return convert


def generate() -> tuple[bytes, dict]:
    convert = windows_mapper()
    sources = [cp for start, end in CJK_RANGES for cp in range(start, end + 1)]
    pairs = []
    for source in sources:
        mapped = convert(chr(source))
        require(len(mapped) == 1, f"U+{source:04X} is not a single-codepoint mapping")
        target = ord(mapped)
        require(is_cjk(target), f"U+{source:04X} maps outside BMP CJK: U+{target:04X}")
        require(convert(mapped) == mapped, f"U+{source:04X} mapping is not idempotent")
        if source != target:
            pairs.append((source, target))

    require(pairs == sorted(pairs), "Mappings are not sorted")
    require(len({source for source, _ in pairs}) == len(pairs), "Duplicate mapping source")
    translation = dict(pairs)
    require(all(translation.get(target, target) == target for _, target in pairs),
            "Table mapping is not idempotent")
    # Compare all scanned characters together to the API's whole-string result.
    inventory = "".join(chr(cp) for cp in sources)
    require(inventory.translate(translation) == convert(inventory),
            "Whole CJK inventory conversion differs from the single-codepoint table")
    samples = []
    for sample in SAMPLES:
        actual = sample.translate(translation)
        require(actual == convert(sample), f"Whole-string sample differs: {sample!r}")
        samples.append({"input": sample, "output": actual})

    payload = b"".join(struct.pack("<HH", source, target) for source, target in pairs)
    digest = hashlib.sha256(payload).hexdigest()
    require(len(pairs) == EXPECTED_PAIRS and digest == EXPECTED_MAPPING_SHA256,
            "Windows mapping differs from the frozen 108 mapping")
    page_starts = [bisect_left(pairs, (page << 8, 0)) for page in range(0x34, 0xA1)]
    entries = [(source & 0xFF, target & 0xFF, target >> 8) for source, target in pairs]
    decoded = [((page << 8) | entries[index][0], entries[index][1] | (entries[index][2] << 8))
               for page in range(0x34, 0xA0)
               for index in range(page_starts[page - 0x34], page_starts[page - 0x34 + 1])]
    require(len(page_starts) == 109 and page_starts[-1] == EXPECTED_PAIRS,
            "Unexpected compressed page index shape")
    require(decoded == pairs, "Compressed table does not decode to the complete original mapping")
    compressed_bytes = 2 * len(page_starts) + 3 * len(entries)
    require(compressed_bytes == 7637, "Unexpected compressed table byte count")
    os_version = ".".join(str(part) for part in sys.getwindowsversion().platform_version)
    header = (
        "// Generated by scripts/Prepare-LyricsHan.py --write; do not edit.\n"
        "// Shared by metadata matching and display preference; preserve raw source text.\n"
        f"// Windows OS version: {os_version}.\n"
        f'// LCMapStringEx("{LOCALE}", LCMAP_SIMPLIFIED_CHINESE=0x02000000).\n'
        f"// API reference: {API_URL}\n"
        "// Source ranges: U+3400..U+4DBF and U+4E00..U+9FFF.\n"
        f"// {len(pairs)} mappings; {compressed_bytes} bytes (109 uint16_t starts + 2473 three-byte entries).\n"
        "// Source pages U+34xx..U+9Fxx; entries are {source_low, target_low, target_high}.\n"
        "// Mapping SHA256 (sorted little-endian source,target uint16_t pairs):\n"
        f"// {digest}\n"
    )
    body = "constexpr std::uint16_t kHanPageStarts[]{\n"
    body += "".join("    " + ",".join(str(value) for value in page_starts[index:index + 12]) + ",\n"
                    for index in range(0, len(page_starts), 12))
    body += "};\nconstexpr std::uint8_t kHanEntries[][3]{\n"
    body += "".join(f"    {{0x{source:02X},0x{low:02X},0x{high:02X}}},\n" for source, low, high in entries)
    body += "};\n"
    report = {
        "windows_os_version": os_version,
        "api_reference": API_URL,
        "locale": LOCALE,
        "flags": "LCMAP_SIMPLIFIED_CHINESE=0x02000000",
        "source_ranges": [[f"U+{start:04X}", f"U+{end:04X}"] for start, end in CJK_RANGES],
        "scanned_codepoints": len(sources),
        "pairs": len(pairs),
        "uint16_pair_bytes": len(payload),
        "compressed_page_starts": len(page_starts),
        "compressed_entries": len(entries),
        "compressed_table_bytes": compressed_bytes,
        "storage_savings_bytes": len(payload) - compressed_bytes,
        "mapping_sha256_le_uint16_pairs": digest,
        "verified": ["BMP CJK targets", "single-codepoint results", "sorted unique sources",
                     "API and table idempotence", "whole CJK inventory", "whole-string samples",
                     "frozen 108 mapping digest", "compressed decode identity"],
        "samples": samples,
    }
    return (header + body).encode("ascii"), report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="Write the generated include file")
    args = parser.parse_args()
    generated, report = generate()
    if args.write:
        OUTPUT.write_bytes(generated)
    else:
        require(OUTPUT.exists(), "Missing include file; run this script with --write")
        require(OUTPUT.read_bytes() == generated,
                "Include file differs from this Windows mapping; review before using --write")
    report["operation"] = "write" if args.write else "check"
    report["output"] = OUTPUT.relative_to(ROOT).as_posix()
    report["status"] = "passed"
    print(json.dumps(report, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
