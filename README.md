# ESP32 CYD random SD media player

This repository now contains a simple Arduino sketch for the **ESP32-2432S028R "Cheap Yellow Display" (CYD)** that:

- imports MP4 source videos into an SD-card-friendly format
- scans the SD card for media clips
- shuffles them into a random order
- plays each clip on the built-in ILI9341 display
- plays optional companion WAV audio on a speaker through the ESP32 DAC
- loops forever like a tiny offline random media player

## MP4 support

The CYD cannot practically decode raw H.264/AAC `.mp4` files directly on the ESP32.

This repository now supports **MP4 as the input format you prepare on your computer**:

1. put one or more `.mp4` files on your computer
2. run the converter script in this repo
3. copy the generated `/media/...` folders to the SD card
4. boot the CYD and it plays them in random order

Generate the SD-card clips with:

```bash
python /path/to/your/clone/tools/prepare_mp4_for_sd.py \
  --input /path/to/video1.mp4 /path/to/video2.mp4 \
  --output /path/to/sd-card-root
```

That creates:

```text
/path/to/sd-card-root/media/<video-name>/
```

with numbered JPEG frames, optional `audio.wav`, and `fps.txt`.

Use `--dry-run` to print the `ffmpeg` commands without running them.

## Supported media layout

Put clips on the SD card like this:

```text
/media
  /clip01
    0001.jpg
    0002.jpg
    0003.jpg
    audio.wav
    fps.txt
  /clip02
    0001.jpg
    0002.jpg
    0003.jpg
```

### Rules

- Each subfolder inside `/media` is treated as one video clip.
- Frames must be `.jpg` or `.jpeg`.
- Name frames with zero-padded numbers so they sort correctly, for example `0001.jpg`, `0002.jpg`, `0003.jpg`.
- `audio.wav` is optional.
- `fps.txt` is optional. If missing, the sketch uses `12` FPS.

## Audio support

The sketch supports **PCM WAV** audio:

- 8-bit or 16-bit PCM
- mono or stereo
- mixed down to mono for speaker output

By default it uses the ESP32 built-in DAC on **GPIO25**. If your CYD speaker amp is wired to the other DAC pin, switch the constant in the sketch to `I2S_DAC_CHANNEL_LEFT_EN` for **GPIO26**. The sketch routes the audio channel to the matching DAC automatically.

### Wiring the XYJ-XK01 speaker amplifier

The XYJ-XK01 is a small class-D amplifier board (analog line-in, not I2S), so it wires directly to the ESP32 DAC output that the sketch already produces:

- module `VCC` → ESP32 `5V`
- module `GND` → ESP32 `GND` (must share ground with the CYD board)
- module audio input (often labeled `IN`, `AIN`, or `L`) → ESP32 `GPIO25` (the DAC pin the sketch uses by default)
- module speaker output terminals → the small speaker

If the board has separate `L`/`R` input pads, connect only one channel — the sketch already mixes stereo WAV files down to mono before sending them to the DAC, so a single input pad is enough. Many of these boards expect an AC-coupled line-level input; if you hear a hum/hiss with no board-side coupling capacitor, add a 1–10 µF capacitor in series between `GPIO25` and the module's audio input pad.

Some boards ship with the pins labeled differently — check the silkscreen on your specific unit against `VCC`/`GND`/`IN`/`OUT+`/`OUT-` before wiring.

## Required Arduino libraries

Install these from the Arduino Library Manager:

- `Arduino_GFX_Library`
- `JPEGDEC`

The sketch also uses the standard ESP32 Arduino core libraries:

- `SD`
- `SPI`
- `driver/i2s.h`

## Hardware defaults for ESP32-2432S028R

The sketch is set up for the common ILI9341 CYD wiring:

- TFT DC: `GPIO2`
- TFT CS: `GPIO15`
- TFT SCK: `GPIO14`
- TFT MOSI: `GPIO13`
- TFT MISO: `GPIO12`
- TFT backlight: `GPIO21`
- SD CS: `GPIO5`
- SD SCK: `GPIO18`
- SD MOSI: `GPIO23`
- SD MISO: `GPIO19`
- BOOT button: `GPIO0`

## Hardware defaults for the 3.5" 320x480 board (ESP32-3248S035R "ESP32-32E")

If your board is the larger 3.5" resistive-touch unit (silkscreen: "3.5" LCD Display ESP32-32E 320x480 Resistance Touch", ST7796 controller), the sketch auto-selects this wiring when `BOARD_CYD_35` is defined at the top of the `.ino` file (this is the default). To switch back to the 2.8" ILI9341 CYD, comment out that `#define`.

- TFT DC: `GPIO2`
- TFT CS: `GPIO15`
- TFT SCK: `GPIO14`
- TFT MOSI: `GPIO13`
- TFT MISO: `GPIO12`
- TFT backlight: `GPIO27`
- SD CS: `GPIO5`
- SD SCK: `GPIO18`
- SD MOSI: `GPIO23`
- SD MISO: `GPIO19`
- BOOT button: `GPIO0`
- Speaker header: `GPIO26` (DAC2 / left channel) — this is where the onboard "Speaker Interface" connector (and your XYJ-XK01 amp) is wired, so no extra wiring is needed beyond what's already plugged in
- Native panel resolution is 320x480 portrait. The sketch keeps it in portrait (`DISPLAY_ROTATION = 0`) with the connector edge (USB/SD/battery) down and the ESP32 module up. If the image comes up upside-down on your unit, change `DISPLAY_ROTATION` to `2` in the `.ino` file.

Since this board has an onboard FM8002A amplifier chip feeding that speaker header already, the XYJ-XK01 is effectively a second amplifier stage. If audio sounds distorted or too quiet, try connecting a bare 8Ω speaker directly to that header instead of routing through the XYJ-XK01.

When using this board, generate your SD clips at the native portrait resolution so anime fills the screen without letterboxing:

```bash
python tools/prepare_mp4_for_sd.py --input episode.mp4 --output /path/to/sd-card-root --width 320 --height 480 --fps 15
```

## Build and upload

1. Open `/home/runner/work/ESP32-video-player-/ESP32-video-player-/RandomSdMediaPlayer/RandomSdMediaPlayer.ino` in Arduino IDE.
2. Select **ESP32 Dev Module**.
3. Install the required libraries.
4. Insert an SD card prepared with the `/media` folder structure.
5. Upload the sketch.
6. Open Serial Monitor at `115200`.

Press the **BOOT** button to skip to the next random clip.

## Converting MP4 files for the player

Example:

```bash
python /path/to/your/clone/tools/prepare_mp4_for_sd.py \
  --input /videos/input.mp4 \
  --output /tmp/cyd-sd \
  --fps 12
```

Then copy `/tmp/cyd-sd/media` to the SD card root.

### Requirements for the converter

- `python`
- `ffmpeg`

### What the converter does

- scales the video into a 320x240 letterboxed output
- extracts numbered JPEG frames
- writes mono 22050 Hz PCM WAV audio
- writes `fps.txt`
- creates a folder name that is safe for FAT-formatted SD cards

## Auto-converting a folder (drop files in and walk away)

Instead of running the converter per file, point `tools/watch_and_convert.py` at a folder (e.g. your downloads folder) and it converts any new video dropped there, automatically:

```bash
python tools/watch_and_convert.py --watch-folder /path/to/downloads --output /path/to/sd-card-root --width 320 --height 480 --fps 15
```

- It scans the folder every few seconds for new `.mp4`/`.mkv`/`.mov`/`.avi`/`.webm`/`.m4v` files.
- It waits until a file's size stops changing before converting, so it won't grab a half-downloaded/half-copied file.
- After converting, it moves the original into a `converted/` subfolder inside the watch folder so it's never re-processed.
- Leave it running in a terminal; press `Ctrl+C` to stop watching.

