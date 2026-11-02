# Shorekeeper eye-break introduction

`shorekeeper_intro.bin` is a silent **3.8-second pre-rendered 3D character clip** from KURO GAMES' [official Shorekeeper profile](https://wutheringwaves.kurogames.com/en/main/news/detail/1419). The article's Resonance Liberation GIF contains game-rendered animation; its surrounding static information card is cropped out. The device plays those rendered frames locally.

The pack reuses the existing `KINTRO1` format and shared JPEG worker: 38 baseline 320×240 JPEG frames at 10 fps, 324,546 bytes total. The top 320×184 contains video; the bottom 56 pixels are black behind HOME. SHA256: `fa420d4e4620552d5a2fc3ee9306164fba24dc96adf698b3a24424a50ef6a7af`. Exact CDN URL, source GIF SHA, trim, crop and ownership information: `shorekeeper-3d-source.json`; frame metadata: `shorekeeper_intro.bin.json`.

From the outer Botifier repository, after downloading the original GIF from the source record:

```powershell
python scripts/Prepare-Shorekeeper3D.py --input path/to/official-profile-03.gif --output StackChan/firmware/main/apps/common/idle_program/media/shorekeeper_intro.bin
```

The converter validates the exact original SHA and reuses `Prepare-KuroIntro.py`'s validation/packer. It needs only the standard library and existing FFmpeg, never downloads files, and refuses to overwrite outputs unless `--force` is supplied. The final pack was rebuilt byte for byte.

After the clip actually finishes, a rhythmic pet face accompanies small relative head movements for 4.5 seconds. Only then does the configured REST countdown begin (20 seconds by default). NFC and the whole eye-break sequence share the highest automatic priority; first started completes, taps during EyeRest are consumed without queuing, and a break due during NFC remains pending. Distance warnings follow both; explicit HOME/TUNE/OFF still exits.

Earlier AI-generated `shorekeeper-sheet.png`, `shorekeeper.bin`, `shorekeeper.bin.json` and `source.json` are retained as **historical seq47 assets**. The six-frame BGRA pack and its runtime code are no longer linked. They are not the source of the new 3D clip.

Character design and original footage belong to KURO GAMES and respective owners. Public availability does not establish a redistribution license.
