#!/usr/bin/env python3
"""Pack the verified local official Shorekeeper GIF into a silent 3.8-second clip.

Requires Python's standard library and an existing ffmpeg executable. No media
is downloaded. This converts prerecorded 3D footage, not on-device 3D rendering.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


SOURCE_SHA256 = "43a9d89d15d8c5e8b9b65287c02cf3cfad146e7a0c196d5bde1de2505691df2b"
FILTER = "crop=500:374:150:208,scale=320:240:flags=lanczos,fps=10"
SOURCE_URL = (
    "https://hw-media-cdn-mingchao.kurogame.com/object/1727366400000/"
    "i3h37xv61wxlu49nxy-1727430153729.gif"
)


def run(command):
    return subprocess.run(
        command, check=True, capture_output=True, timeout=45, shell=False,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    ).stdout


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Verified local official GIF")
    parser.add_argument("--output", required=True, type=Path, help="Destination KINTRO1 .bin")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="Existing ffmpeg executable name or path")
    parser.add_argument("--force", action="store_true", help="Allow replacing an existing pack or metadata")
    args = parser.parse_args(argv)
    try:
        source = args.input.resolve(strict=True)
        output = args.output.resolve()
        metadata_path = output.with_name(output.name + ".json")
        if source in (output, metadata_path):
            raise ValueError("Input, output and metadata paths must be distinct")
        if not args.force and (output.exists() or metadata_path.exists()):
            raise ValueError("Output or metadata already exists; use --force to replace intentionally")
        if not source.is_file() or source.stat().st_size != 7_597_976:
            raise ValueError("Input must be the verified 7597976-byte official GIF")
        source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
        if source_sha != SOURCE_SHA256:
            raise ValueError("Input SHA-256 does not match the verified official GIF")
        executable = shutil.which(args.ffmpeg)
        if executable is None:
            raise ValueError("Existing ffmpeg executable was not found")
        version = run([executable, "-version"]).decode("utf-8", errors="replace").splitlines()[0]
        packer_path = Path(__file__).resolve().with_name("Prepare-KuroIntro.py")
        spec = importlib.util.spec_from_file_location("stackchan_kuro_packer", packer_path)
        if spec is None or spec.loader is None:
            raise ValueError("Existing Prepare-KuroIntro.py packer could not be loaded")
        packer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packer)
        packer.FPS, packer.DURATION_MS, packer.MAX_PACK_BYTES = 10, 3800, 500_000
        with tempfile.TemporaryDirectory(prefix="stackchan-shorekeeper3d-") as temporary:
            pattern = str(Path(temporary) / "frame-%03d.jpg")
            command = [
                executable, "-hide_banner", "-loglevel", "error", "-nostdin",
                "-threads", "1", "-filter_threads", "1",
                "-protocol_whitelist", "file,pipe", "-ss", "0", "-i", str(source),
                "-t", "3.8", "-an", "-sn", "-dn", "-vf", FILTER,
                "-c:v", "mjpeg", "-q:v", "5", "-pix_fmt", "yuvj420p",
                "-threads", "1", "-y", pattern,
            ]
            run(command)
            frames = [path.read_bytes() for path in sorted(Path(temporary).glob("frame-*.jpg"))]
            packed = packer.make_pack(frames)
        metadata = {
            "format": "KINTRO1", "width": 320, "height": 240,
            "fps": 10, "duration_ms": 3800, "frames": len(frames),
            "bytes": len(packed), "sha256": hashlib.sha256(packed).hexdigest(),
            "max_frame_bytes": max(map(len, frames)),
            "source": source.name, "source_bytes": source.stat().st_size,
            "source_sha256": source_sha, "source_url": SOURCE_URL,
            "source_article": "https://wutheringwaves.kurogames.com/en/main/news/detail/1419",
            "time_range_seconds": [0, 3.8],
            "crop": {"x": 150, "y": 208, "width": 500, "height": 374},
            "scaled_video": {"width": 320, "height": 240},
            "pad": None, "presentation": "Full-screen video; no menu area or black padding",
            "filter": FILTER, "jpeg_quality_scale": 5,
            "jpeg_sampling": "yuvj420p", "jpeg_baseline_validated": True,
            "audio": False, "ffmpeg_version": version,
            "ffmpeg_args": [
                Path(executable).name, *command[1:command.index(str(source))],
                source.name, *command[command.index(str(source)) + 1:-1], "frame-%03d.jpg",
            ],
            "packer": "Prepare-KuroIntro.py make_pack and validate_jpeg",
            "rights": "Prerecorded official 3D footage remains KURO GAMES property; no redistribution license is asserted.",
        }
        output.parent.mkdir(parents=True, exist_ok=True)
        # Both validated artifacts are staged before touching either destination.
        with tempfile.TemporaryDirectory(prefix="shorekeeper3d-write-", dir=output.parent) as temporary:
            stage = Path(temporary)
            pack_stage, metadata_stage = stage / "pack.bin", stage / "metadata.json"
            pack_stage.write_bytes(packed)
            metadata_stage.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            if not args.force and (output.exists() or metadata_path.exists()):
                raise ValueError("Output appeared during conversion; refusing to replace it")
            pack_stage.replace(output)
            metadata_stage.replace(metadata_path)
        print(f"Packed {len(frames)} validated baseline JPEG frames: {output} ({len(packed)} bytes)")
        print(f"SHA-256: {metadata['sha256']}")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(2, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
