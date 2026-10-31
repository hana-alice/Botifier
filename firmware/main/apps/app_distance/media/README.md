# KURO GAMES logo clip

`kuro_intro.bin` contains the original black-background KURO GAMES logo fade from the public PGR **Aeon Reforged** promotional video. It is trimmed to 0–3.4 seconds, center-cropped and scaled for the 320×240 LCD. The logo was not redrawn. The source logo interval is silent; this asset has no audio, synthesized voice or PCM payload.

The mark and source video belong to **KURO GAMES**. Source URLs, input provenance, hashes and exact conversion settings are recorded in `source.json`. This attribution does not grant a redistribution license.

## Regenerate

From the outer Botifier repository, using Python and an existing `ffmpeg` on PATH:

```powershell
python scripts/Prepare-KuroIntro.py --input .tools/kuro-intro/research/aeon-first6.mp4 --output StackChan/firmware/main/apps/app_distance/media/kuro_intro.bin
```

The input used here is the first six seconds of the 1920×1080 official MP4, copied with `ffmpeg -i SOURCE_URL -t 6 -c copy aeon-first6.mp4`. `SOURCE_URL` is the `original_video_url` in `source.json`. The generator itself only reads the supplied local file and does not download media. It uses a temporary directory that is removed automatically.

The crop is the central 30% of source width and 40% of source height: 576×432 pixels on the native source. Frames are scaled to 320×240, sampled at 12 fps and encoded as 8-bit baseline JPEG 4:2:0 with quality `-q:v 3`. The generator validates every JPEG and caps the pack at 1,300,000 bytes. For this source the pack has 41 frames; the last frame is held only until the 3400 ms end time.

## Binary format

All integers are unsigned and little-endian. There are no pointers, padding bytes or audio fields.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 8 bytes | `KINTRO1\0` magic |
| 8 | uint16 | Width, 320 |
| 10 | uint16 | Height, 240 |
| 12 | uint16 | Frames per second, 12 |
| 14 | uint16 | Reserved, zero |
| 16 | uint32 | Frame count, 41 |
| 20 | uint32 | Duration in milliseconds, 3400 |
| 24 | frame count × 8 bytes | Table of `(uint32 absolute_offset, uint32 byte_length)` pairs |
| after table | bytes | Complete JPEG frames, contiguous through end of file |

The first frame starts at `24 + 8 × frame_count`. Each following frame starts immediately after its predecessor. Playback ends at the declared duration; it does not loop.
