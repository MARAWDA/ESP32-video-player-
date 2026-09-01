# ESP32 CYD random SD media player

This repository now contains a simple Arduino sketch for the **ESP32-2432S028R "Cheap Yellow Display" (CYD)** that:

- scans the SD card for media clips
- shuffles them into a random order
- plays each clip on the built-in ILI9341 display
- plays optional companion WAV audio on a speaker through the ESP32 DAC
- loops forever like a tiny offline random media player

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

## Converting a video into a clip

Example with `ffmpeg`:

```bash
mkdir -p clip01
ffmpeg -i input.mp4 -vf "fps=12,scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2:black" clip01/%04d.jpg
ffmpeg -i input.mp4 -ac 1 -ar 22050 -c:a pcm_s16le clip01/audio.wav
printf "12\n" > clip01/fps.txt
```

Then copy `clip01` into `/media` on the SD card.
