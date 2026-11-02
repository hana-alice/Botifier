# Shorekeeper eye-break introduction

`shorekeeper_intro.bin` is a silent **3.8-second pre-rendered 3D character clip** from KURO GAMES' [official Shorekeeper profile](https://wutheringwaves.kurogames.com/en/main/news/detail/1419). The article's Resonance Liberation GIF contains game-rendered animation; its surrounding static information card is cropped out. The device plays those rendered frames locally.

The pack reuses the existing `KINTRO1` format and shared JPEG worker: 38 baseline 320×240 JPEG frames at 10 fps, 422,532 bytes total. The image fills the entire 320×240 display: a centered 500×374 crop at source x150/y208 is scaled to 320×240, with no black padding or reserved menu area. SHA256: `5a67997f01f8be973bd1f3d9b0b05785c89bb78b227ed679751bcc5f364155ab`. Exact CDN URL, source GIF SHA, trim, crop and ownership information: `shorekeeper-3d-source.json`; frame metadata: `shorekeeper_intro.bin.json`.

From the outer Botifier repository, after downloading the original GIF from the source record:

```powershell
python scripts/Prepare-Shorekeeper3D.py --input path/to/official-profile-03.gif --output StackChan/firmware/main/apps/common/idle_program/media/shorekeeper_intro.bin
```

The converter validates the exact original SHA and reuses `Prepare-KuroIntro.py`'s validation/packer. It needs only the standard library and existing FFmpeg, never downloads files, and refuses to overwrite outputs unless `--force` is supplied. Two independent full-screen rebuilds produced identical bytes. The previous seq49 320×184-plus-padding pack is retained in git history and locally under `.tools/shorekeeper-3d/fullbleed/seq49-shorekeeper_intro.bin`.

After the clip actually finishes, a rhythmic pet face accompanies small relative head movements for 4.5 seconds. Only then does the configured REST countdown begin (20 seconds by default). NFC and the whole eye-break sequence share the highest automatic priority; first started completes, taps during EyeRest are consumed without queuing, and a break due during NFC remains pending. Distance warnings follow both; explicit HOME/TUNE/OFF still exits.

Earlier AI-generated `shorekeeper-sheet.png`, `shorekeeper.bin`, `shorekeeper.bin.json` and `source.json` are retained as **historical seq47 assets**. The six-frame BGRA pack and its runtime code are no longer linked. They are not the source of the new 3D clip.

Character design and original footage belong to KURO GAMES and respective owners. Public availability does not establish a redistribution license.
