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
