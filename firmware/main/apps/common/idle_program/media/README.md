# Shorekeeper eye-rest animation

`shorekeeper-sheet.png` is original AI-generated chibi fan art of **The Shorekeeper / 守岸人** from **Wuthering Waves**, selected by the user. Character design belongs to KURO GAMES. No other fan artist's animation, official game pixels, audio or video frames were copied into this asset.

The built-in `image_gen` tool generated a transparent 1536×1024 sheet of six aligned wave/blink poses. The complete generation prompt and design references are in `source.json`. Generation is nondeterministic; the checked-in PNG is the source for reproducible conversion.

`shorekeeper.bin` contains six consecutive 160×160 BGRA frames (straight alpha, little endian `LV_COLOR_FORMAT_ARGB8888`), 102,400 bytes per frame and 614,400 bytes total. It has no header or executable data. `shorekeeper.bin.json` records the source, conversion flags, frame and pack hashes, alpha checks and limitations. Source pixels are only cropped and resized with the existing FFmpeg; there is no runtime decoder or per-frame RGB allocation.

From the outer Botifier repository:

```powershell
python scripts/Prepare-Shorekeeper.py --input StackChan/firmware/main/apps/common/idle_program/media/shorekeeper-sheet.png --output StackChan/firmware/main/apps/common/idle_program/media/shorekeeper.bin --preview-dir .tools/shorekeeper/frames
```

Existing outputs are preserved unless `--force` is explicitly supplied for a deliberate regeneration. No additional Python packages are needed.

Only the due **EyeRest** programme displays this artwork: six poses at 300ms per frame, followed by a still pose to complete a 3.6s loop, plus gentle floating and four star highlights. The original 20-minute work interval and configured rest duration remain independent of artwork timing. NFC and EyeRest share the highest automatic priority: the animation that starts first completes, taps during EyeRest are consumed without queuing, and an eye break that becomes due during NFC remains pending. Distance warnings wait below both animations; explicit HOME/TUNE/OFF still exits.
