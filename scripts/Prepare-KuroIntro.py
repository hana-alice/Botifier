#!/usr/bin/env python3
"""Pack an existing official KURO logo clip as baseline JPEG frames.

Uses only the Python standard library and an existing ffmpeg executable.
The input is local; this script never downloads media or generates audio.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


MAGIC = b"KINTRO1\0"
WIDTH, HEIGHT, FPS, DURATION_MS = 320, 240, 12, 3400
MAX_PACK_BYTES = 1_300_000
HEADER = struct.Struct("<8sHHHHII")
ENTRY = struct.Struct("<II")
FILTER = "crop=iw*0.3:ih*0.4:(iw-ow)/2:(ih-oh)/2,scale=320:240:flags=lanczos,fps=12"
SOURCE_URL = (
    "https://media-cdn-zspms.kurogame.net/pnswebsite/website2.0/video/"
    "1730044800000/arlj6ajjy1im8k9ptd-1730086806991.mp4"
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_jpeg(data):
    """Reject truncated frames, progressive JPEG, wrong size or chroma."""
    if not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
        raise ValueError("JPEG frame lacks complete SOI/EOI markers")
    cursor = 2
    while cursor < len(data) - 1:
        if data[cursor] != 0xFF:
            raise ValueError("Malformed JPEG marker")
        while data[cursor] == 0xFF:
            cursor += 1
        marker = data[cursor]
        cursor += 1
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            continue
        length = int.from_bytes(data[cursor:cursor + 2], "big")
        if length < 2 or cursor + length > len(data):
            raise ValueError("Truncated JPEG segment")
        if marker in (0xC0, 0xC1, 0xC2):
            segment = data[cursor + 2:cursor + length]
            if marker != 0xC0 or len(segment) != 15:
                raise ValueError("Expected an 8-bit baseline three-component JPEG")
            precision, height, width, components = struct.unpack(">BHHB", segment[:6])
            sampling = (segment[7], segment[10], segment[13])
            if (precision, width, height, components) != (8, WIDTH, HEIGHT, 3):
                raise ValueError("JPEG dimensions or precision do not match the pack")
            if sampling != (0x22, 0x11, 0x11):
                raise ValueError("Expected JPEG 4:2:0 sampling")
            return
        if marker == 0xDA:
            break
        cursor += length
    raise ValueError("JPEG baseline frame header was not found")


def make_pack(frames):
    expected_frames = (DURATION_MS * FPS + 999) // 1000
    if len(frames) != expected_frames:
        raise ValueError(f"Expected {expected_frames} frames, got {len(frames)}")
    offset = HEADER.size + ENTRY.size * len(frames)
    table = bytearray()
    for frame in frames:
        validate_jpeg(frame)
        table.extend(ENTRY.pack(offset, len(frame)))
        offset += len(frame)
    if offset > MAX_PACK_BYTES:
        raise ValueError(f"Pack size {offset} exceeds {MAX_PACK_BYTES} bytes")
    return b"".join((
        HEADER.pack(MAGIC, WIDTH, HEIGHT, FPS, 0, len(frames), DURATION_MS),
        bytes(table),
        *frames,
    ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Local native 16:9 MP4")
    parser.add_argument("--output", required=True, type=Path, help="Destination kuro_intro.bin")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="Existing ffmpeg executable")
    args = parser.parse_args()
    source = args.input.resolve(strict=True)
    output = args.output.resolve()
    if output == source:
        parser.error("Input and output must be different files")
    executable = shutil.which(args.ffmpeg)
    if executable is None:
        parser.error(f"ffmpeg executable not found: {args.ffmpeg}")
    version = subprocess.run(
        [executable, "-version"], check=True, capture_output=True, text=True
    ).stdout.splitlines()[0]
    with tempfile.TemporaryDirectory(prefix="stackchan-kuro-intro-") as temporary:
        pattern = str(Path(temporary) / "frame-%05d.jpg")
        command = [
            executable, "-hide_banner", "-loglevel", "error", "-nostdin",
            "-i", str(source), "-t", "3.4", "-an", "-vf", FILTER,
            "-c:v", "mjpeg", "-q:v", "3", "-pix_fmt", "yuvj420p",
            "-threads", "1", "-y", pattern,
        ]
        subprocess.run(command, check=True)
        frames = [path.read_bytes() for path in sorted(Path(temporary).glob("frame-*.jpg"))]
        packed = make_pack(frames)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(packed)
    metadata = {
        "format": "KINTRO1",
        "rights_holder": "KURO GAMES",
        "attribution": "Official KURO GAMES mark from the PGR Aeon Reforged promotional video",
        "source_page": "https://pgr.kurogame.net/",
        "source_index": "https://media-cdn-zspms.kurogame.net/pnswebsite/website2.0/json/G167/MainMenu.json",
        "original_video_url": SOURCE_URL,
        "original_video_sha256": None,
        "original_video_sha256_note": "Full 298692483-byte PV was not downloaded; the input is its first 6 seconds, copied without re-encoding by ffmpeg.",
        "input_file": source.name,
        "input_bytes": source.stat().st_size,
        "input_sha256": sha256(source),
        "input_acquisition": "ffmpeg -i ORIGINAL_VIDEO_URL -t 6 -c copy aeon-first6.mp4",
        "trim_start_seconds": 0,
        "duration_ms": DURATION_MS,
        "crop": "Central 30% of source width and 40% of source height; 576x432 for the native 1920x1080 source",
        "filter": FILTER,
        "width": WIDTH,
        "height": HEIGHT,
        "fps": FPS,
        "frames": len(frames),
        "frame_codec": "8-bit baseline JPEG, yuvj420p, ffmpeg -q:v 3",
        "ffmpeg_version": version,
        "silent": True,
        "audio": "No audio stored or synthesized. The original logo interval is silent.",
        "silence_evidence": "ffmpeg silencedetect=noise=-45dB:d=0.08 on the first 6 seconds: silence_start=0, silence_end=3.897167. The logo occupies 0..3.4 seconds.",
        "transformations": "Trim, center crop, scale and JPEG transcode only; the logo is not redrawn.",
        "pack_bytes": len(packed),
        "pack_sha256": hashlib.sha256(packed).hexdigest(),
        "usage": "Third-party mark and video remain property of KURO GAMES; this record does not grant a redistribution license.",
    }
    output.with_name("source.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Packed {len(frames)} baseline JPEG frames: {output} ({len(packed)} bytes)")
    print(f"SHA-256: {metadata['pack_sha256']}")


if __name__ == "__main__":
    main()
