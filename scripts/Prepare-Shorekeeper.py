#!/usr/bin/env python3
"""Pack a local transparent 3-column/2-row PNG sheet as six 160x160 BGRA frames.

Only crop and Lanczos scaling are applied. Python's standard library and existing
ffmpeg/ffprobe executables are used; no downloads or image generation occur.
"""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


WIDTH = HEIGHT = 160
FRAME_BYTES = WIDTH * HEIGHT * 4
PACK_BYTES = FRAME_BYTES * 6
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


class PreparationError(Exception):
    pass


class Parser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(2, 'ERROR: invalid arguments; use --help\n')


def require(condition, message):
    if not condition:
        raise PreparationError(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def executable(name, sibling=None):
    found = shutil.which(name)
    if found is None and sibling is not None:
        candidate = Path(sibling).with_name(name + ('.exe' if os.name == 'nt' else ''))
        if candidate.is_file():
            found = str(candidate)
    require(found is not None, 'Required ffmpeg/ffprobe executable is unavailable; supply its existing path')
    return found


def run(command, input_data=None):
    try:
        result = subprocess.run(command, input=input_data, capture_output=True,
            timeout=45, check=False, shell=False,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except subprocess.TimeoutExpired:
        raise PreparationError('Media tool timed out') from None
    except OSError:
        raise PreparationError('Media tool could not start') from None
    require(result.returncode == 0, 'Media tool rejected the input or conversion')
    return result.stdout


def probe(source, ffprobe):
    with source.open('rb') as handle:
        require(handle.read(8) == PNG_SIGNATURE, 'Input must be a local PNG image')
    raw = run([ffprobe, '-v', 'error', '-protocol_whitelist', 'file,pipe', '-select_streams', 'v:0',
               '-show_entries', 'stream=codec_name,width,height,pix_fmt', '-of', 'json', str(source)])
    require(len(raw) <= 8192, 'Unexpected image probe response size')
    try:
        streams = json.loads(raw)['streams']
        require(len(streams) == 1, 'Expected one static PNG stream')
        stream = streams[0]
        width, height = stream['width'], stream['height']
        require(stream['codec_name'] == 'png', 'Input must be a static PNG sprite sheet')
        require(type(width) is int and type(height) is int and width >= 3 and height >= 2
                and width * height <= 32000000, 'PNG dimensions are unreadable or exceed 32 megapixels')
        return width, height, stream.get('pix_fmt')
    except (KeyError, TypeError, ValueError):
        raise PreparationError('Image dimensions could not be read') from None


def validate_frame(data, index):
    require(len(data) == FRAME_BYTES, f'Frame {index} does not contain exactly 102400 BGRA bytes')
    pixels = WIDTH * HEIGHT
    transparent = sum(alpha == 0 for alpha in data[3::4])
    nontransparent = sum(alpha > 0 for alpha in data[3::4])
    visible = sum(alpha >= 32 for alpha in data[3::4])
    require(transparent * 20 >= pixels, f'Frame {index} has less than 5% true transparency; opaque/checkerboard backgrounds are rejected')
    require(nontransparent * 20 >= pixels and visible * 20 >= pixels,
            f'Frame {index} is empty or has less than 5% visible content')
    colors, neutral_border, border_count = set(), Counter(), 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 4
            blue, green, red, alpha = data[offset:offset + 4]
            if alpha >= 32:
                colors.add((blue, green, red))
            if x < 24 or x >= WIDTH - 24 or y < 24 or y >= HEIGHT - 24:
                border_count += 1
                if alpha >= 240 and max(red, green, blue) - min(red, green, blue) <= 8 and red >= 96:
                    neutral_border[(red + green + blue) // 24] += 1
    require(len(colors) >= 8, f'Frame {index} is blank or lacks visible color variation')
    # Catch an opaque two-grey background even when a small separate area has
    # real alpha. This conservative border heuristic does not classify all art.
    dominant = neutral_border.most_common(2)
    checker_like = (len(dominant) == 2 and sum(neutral_border.values()) >= border_count * .65
                    and sum(count for _, count in dominant) >= sum(neutral_border.values()) * .8
                    and min(count for _, count in dominant) >= border_count * .15
                    and abs(dominant[0][0] - dominant[1][0]) >= 2)
    require(not checker_like, f'Frame {index} has a checkerboard-like opaque neutral border')
    return dict(index=index, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                transparent_pixels=transparent, nontransparent_pixels=nontransparent,
                visible_alpha_at_least32_pixels=visible, visible_rgb_colors=len(colors))


def main(argv=None):
    parser = Parser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path, help='Existing local 3-column/2-row transparent PNG sheet')
    parser.add_argument('--output', required=True, type=Path, help='Six raw 160x160 BGRA frames, no header; 614400 bytes')
    parser.add_argument('--preview-dir', type=Path, help='Optionally write six final 160px PNG previews')
    parser.add_argument('--ffmpeg', default='ffmpeg', help='Existing ffmpeg executable name or path')
    parser.add_argument('--ffprobe', default='ffprobe', help='Existing ffprobe executable name or path')
    parser.add_argument('--force', action='store_true', help='Explicitly allow replacement of existing outputs')
    args = parser.parse_args(argv)
    try:
        source = args.input.resolve(strict=True)
        output = args.output.resolve()
        metadata_path = output.with_name(output.name + '.json')
        previews = [args.preview_dir.resolve() / f'frame-{index:02d}.png' for index in range(1, 7)] if args.preview_dir else []
        targets = [output, metadata_path, *previews]
        require(source not in targets and len(set(targets)) == len(targets), 'Input/output paths must be distinct')
        require(args.force or not any(path.exists() for path in targets), 'An output already exists; use --force only to replace it intentionally')
        require(source.is_file() and source.stat().st_size <= 64 * 1024 * 1024, 'Input is unavailable or exceeds 64 MiB')
        ffmpeg = executable(args.ffmpeg)
        ffprobe = executable(args.ffprobe, ffmpeg)
        source_width, source_height, source_format = probe(source, ffprobe)
        frames, frame_metadata, commands = [], [], []
        for row in range(2):
            for column in range(3):
                left, right = column * source_width // 3, (column + 1) * source_width // 3
                top, bottom = row * source_height // 2, (row + 1) * source_height // 2
                filter_text = f'format=bgra,crop={right-left}:{bottom-top}:{left}:{top},scale=160:160:flags=lanczos,format=bgra'
                command = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-threads', '1',
                    '-filter_threads', '1', '-protocol_whitelist', 'file,pipe', '-i', str(source),
                    '-an', '-sn', '-dn', '-vf', filter_text, '-frames:v', '1', '-pix_fmt', 'bgra',
                    '-threads', '1', '-f', 'rawvideo', 'pipe:1']
                frame = run(command)
                entry = validate_frame(frame, len(frames) + 1)
                entry.update(offset=len(frames) * FRAME_BYTES, crop=dict(x=left, y=top, width=right-left, height=bottom-top))
                frames.append(frame)
                frame_metadata.append(entry)
                commands.append([Path(ffmpeg).name, *command[1:command.index(str(source))], source.name, *command[command.index(str(source)) + 1:]])
        require(len({entry['sha256'] for entry in frame_metadata}) >= 4, 'At least four distinct final frame hashes are required')
        packed = b''.join(frames)
        require(len(packed) == PACK_BYTES, 'Packed data must contain exactly 614400 bytes')
        metadata = dict(format='Six consecutive raw BGRA frames; no header', lvgl_format='LV_COLOR_FORMAT_ARGB8888',
            byte_order='little endian', channel_bytes=['B', 'G', 'R', 'A'], alpha='straight alpha',
            input=dict(file=source.name, bytes=source.stat().st_size, sha256=sha256(source),
                       width=source_width, height=source_height, pixel_format=source_format),
            grid=dict(columns=3, rows=2, order='reading order: left-to-right, then top-to-bottom',
                      boundaries='integer partitions cover every source pixel, including non-divisible grid dimensions'),
            width=WIDTH, height=HEIGHT, frame_count=6, frame_bytes=FRAME_BYTES, pack_bytes=len(packed),
            pack_sha256=hashlib.sha256(packed).hexdigest(), frames=frame_metadata,
            ffmpeg_args=commands, ffmpeg_args_note='Executable and input are recorded by filename; all conversion flags are literal.',
            transformations='Crop each grid cell, Lanczos scale to 160x160, pack BGRA; no chroma key, repainting or synthesized content',
            validation='Every frame: >=5% alpha0, >=5% alpha>0, >=5% alpha>=32, >=8 visible RGB colors; reject two-grey opaque-border heuristic; >=4 distinct raw hashes',
            limits='Checkerboard heuristic is conservative, not a visual-content classifier; inspect optional final previews.')
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='shorekeeper-pack-', dir=output.parent) as directory:
            temporary = Path(directory)
            pack_stage, metadata_stage = temporary / 'pack.raw', temporary / 'metadata.json'
            pack_stage.write_bytes(packed)
            staged = [(pack_stage, output)]
            preview_commands = []
            for index, destination in enumerate(previews):
                stage = temporary / destination.name
                command = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-f', 'rawvideo',
                    '-pixel_format', 'bgra', '-video_size', '160x160', '-i', 'pipe:0', '-frames:v', '1',
                    '-threads', '1', '-c:v', 'png', '-y', str(stage)]
                run(command, frames[index])
                require(stage.is_file() and stage.stat().st_size > 8, 'Preview PNG could not be created')
                preview_commands.append([Path(ffmpeg).name, *command[1:-1], destination.name])
                staged.append((stage, destination))
            metadata['preview_ffmpeg_args'] = preview_commands
            metadata_stage.write_text(json.dumps(metadata, indent=2, allow_nan=False) + '\n', encoding='utf-8')
            staged.append((metadata_stage, metadata_path))
            for stage, destination in staged:
                destination.parent.mkdir(parents=True, exist_ok=True)
                if args.force:
                    os.replace(stage, destination)
                else:
                    with stage.open('rb') as reader, destination.open('xb') as writer:
                        shutil.copyfileobj(reader, writer)
        print(json.dumps(dict(frames=6, width=WIDTH, height=HEIGHT, bytes=len(packed),
                              sha256=metadata['pack_sha256'], distinct_frames=len({entry['sha256'] for entry in frame_metadata}))))
        return 0
    except PreparationError as error:
        print('ERROR: ' + str(error), file=sys.stderr)
    except (OSError, ValueError, TypeError, OverflowError):
        print('ERROR: local input, metadata or output operation failed', file=sys.stderr)
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
