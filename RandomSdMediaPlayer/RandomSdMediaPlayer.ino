#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <SD.h>
#include <SPI.h>
#include <driver/i2s.h>
#include <string.h>

namespace
{
constexpr int TFT_DC = 2;
constexpr int TFT_CS = 15;
constexpr int TFT_SCK = 14;
constexpr int TFT_MOSI = 13;
constexpr int TFT_MISO = 12;
constexpr int TFT_BACKLIGHT = 21;

constexpr int SD_CS = 5;
constexpr int SD_SCK = 18;
constexpr int SD_MOSI = 23;
constexpr int SD_MISO = 19;

constexpr int BOOT_BUTTON = 0;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 350;

constexpr uint32_t DISPLAY_SPI_SPEED = 40000000UL;
constexpr uint32_t SD_SPI_SPEED = 20000000UL;

constexpr const char *MEDIA_ROOT = "/media";
constexpr uint16_t DEFAULT_FPS = 12;
constexpr size_t MAX_MEDIA_ITEMS = 24;
constexpr i2s_port_t AUDIO_PORT = I2S_NUM_0;
constexpr i2s_dac_mode_t AUDIO_DAC_CHANNEL = I2S_DAC_CHANNEL_RIGHT_EN; // GPIO25
constexpr i2s_channel_fmt_t AUDIO_CHANNEL_FORMAT =
    AUDIO_DAC_CHANNEL == I2S_DAC_CHANNEL_LEFT_EN ? I2S_CHANNEL_FMT_ONLY_LEFT : I2S_CHANNEL_FMT_ONLY_RIGHT;
} // namespace

struct MediaClip
{
  String folderPath;
  String audioPath;
  uint16_t fps = DEFAULT_FPS;
  uint16_t frameCount = 0;
  bool hasAudio = false;
};

struct AudioPlayback
{
  File file;
  bool active = false;
  uint32_t dataRemaining = 0;
  uint32_t sampleRate = 22050;
  uint16_t channels = 1;
  uint16_t bitsPerSample = 16;
};

SPIClass displaySpi(HSPI);
SPIClass sdSpi(VSPI);
Arduino_DataBus *displayBus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, &displaySpi, true);
Arduino_GFX *gfx = new Arduino_ILI9341(displayBus);
JPEGDEC jpeg;

MediaClip clips[MAX_MEDIA_ITEMS];
size_t clipOrder[MAX_MEDIA_ITEMS];
size_t clipCount = 0;
size_t currentOrderIndex = 0;
uint32_t lastButtonPressAt = 0;
bool audioReady = false;
AudioPlayback audioPlayback;
uint8_t wavInputBuffer[512];
uint16_t wavOutputBuffer[512];

String baseName(const String &path)
{
  int slashIndex = path.lastIndexOf('/');
  return slashIndex >= 0 ? path.substring(slashIndex + 1) : path;
}

String joinPath(const String &directory, const String &name)
{
  if (name.startsWith("/"))
  {
    return name;
  }
  if (directory.endsWith("/"))
  {
    return directory + name;
  }
  return directory + "/" + name;
}

bool hasExtension(const String &path, const char *extension)
{
  String lower = baseName(path);
  lower.toLowerCase();
  return lower.endsWith(extension);
}

bool isFrameFile(const String &path)
{
  return hasExtension(path, ".jpg") || hasExtension(path, ".jpeg");
}

bool isWavFile(const String &path)
{
  return hasExtension(path, ".wav");
}

void sortStrings(String *items, size_t count)
{
  for (size_t i = 0; i < count; ++i)
  {
    size_t smallestIndex = i;
    for (size_t j = i + 1; j < count; ++j)
    {
      if (items[j].compareTo(items[smallestIndex]) < 0)
      {
        smallestIndex = j;
      }
    }
    if (smallestIndex != i)
    {
      String temp = items[i];
      items[i] = items[smallestIndex];
      items[smallestIndex] = temp;
    }
  }
}

void showStatus(const String &message, uint16_t color = RGB565_WHITE)
{
  Serial.println(message);
  gfx->fillScreen(RGB565_BLACK);
  gfx->setCursor(8, 8);
  gfx->setTextColor(color);
  gfx->setTextSize(2);
  gfx->println(message);
}

uint16_t loadClipFps(const String &folderPath)
{
  File fpsFile = SD.open(joinPath(folderPath, "fps.txt").c_str(), FILE_READ);
  if (!fpsFile)
  {
    return DEFAULT_FPS;
  }

  String fpsText = fpsFile.readString();
  fpsFile.close();
  fpsText.trim();

  long parsedFps = fpsText.toInt();
  if (parsedFps < 1 || parsedFps > 60)
  {
    return DEFAULT_FPS;
  }
  return static_cast<uint16_t>(parsedFps);
}

bool pollSkipButton()
{
  if (digitalRead(BOOT_BUTTON) != LOW)
  {
    return false;
  }

  uint32_t now = millis();
  if (now - lastButtonPressAt < BUTTON_DEBOUNCE_MS)
  {
    return false;
  }

  lastButtonPressAt = now;
  Serial.println("Skip requested");
  return true;
}

bool discoverSingleClip(const String &folderPath, MediaClip &clip)
{
  File clipDirectory = SD.open(folderPath.c_str(), FILE_READ);
  if (!clipDirectory || !clipDirectory.isDirectory())
  {
    return false;
  }

  clip.folderPath = folderPath;
  clip.fps = loadClipFps(folderPath);
  clip.frameCount = 0;
  clip.hasAudio = false;
  clip.audioPath = "";

  while (true)
  {
    File entry = clipDirectory.openNextFile();
    if (!entry)
    {
      break;
    }

    String entryName = entry.name();
    if (!entry.isDirectory())
    {
      if (isFrameFile(entryName))
      {
        ++clip.frameCount;
      }
      else if (!clip.hasAudio && isWavFile(entryName))
      {
        clip.hasAudio = true;
        clip.audioPath = joinPath(folderPath, baseName(entryName));
      }
    }

    entry.close();
  }

  clipDirectory.close();
  return clip.frameCount > 0;
}

void discoverMedia()
{
  File mediaDirectory = SD.open(MEDIA_ROOT, FILE_READ);
  if (!mediaDirectory || !mediaDirectory.isDirectory())
  {
    showStatus("Missing /media folder", RGB565_RED);
    clipCount = 0;
    return;
  }

  clipCount = 0;
  while (clipCount < MAX_MEDIA_ITEMS)
  {
    File entry = mediaDirectory.openNextFile();
    if (!entry)
    {
      break;
    }

    if (entry.isDirectory())
    {
      MediaClip clip;
      String folderPath = entry.name();
      if (!folderPath.startsWith("/"))
      {
        folderPath = joinPath(MEDIA_ROOT, folderPath);
      }

      if (discoverSingleClip(folderPath, clip))
      {
        clips[clipCount] = clip;
        clipOrder[clipCount] = clipCount;
        ++clipCount;
      }
    }

    entry.close();
  }

  mediaDirectory.close();

  if (clipCount == 0)
  {
    showStatus("No clips found in /media", RGB565_YELLOW);
    return;
  }

  Serial.printf("Discovered %u clips\n", static_cast<unsigned>(clipCount));
  for (size_t i = 0; i < clipCount; ++i)
  {
    Serial.printf("Clip %u: %s (%u frames @ %u FPS)%s\n",
                  static_cast<unsigned>(i + 1),
                  clips[i].folderPath.c_str(),
                  static_cast<unsigned>(clips[i].frameCount),
                  static_cast<unsigned>(clips[i].fps),
                  clips[i].hasAudio ? " with audio" : "");
  }
}

void shufflePlaylist()
{
  if (clipCount < 2)
  {
    currentOrderIndex = 0;
    return;
  }

  for (size_t i = 0; i < clipCount; ++i)
  {
    clipOrder[i] = i;
  }

  for (size_t i = clipCount - 1; i > 0; --i)
  {
    size_t randomIndex = static_cast<size_t>(random(static_cast<long>(i + 1)));
    size_t temp = clipOrder[i];
    clipOrder[i] = clipOrder[randomIndex];
    clipOrder[randomIndex] = temp;
  }

  currentOrderIndex = 0;
}

size_t loadFrameList(const MediaClip &clip, String *frames, size_t capacity)
{
  File clipDirectory = SD.open(clip.folderPath.c_str(), FILE_READ);
  if (!clipDirectory || !clipDirectory.isDirectory())
  {
    return 0;
  }

  size_t frameCount = 0;
  while (frameCount < capacity)
  {
    File entry = clipDirectory.openNextFile();
    if (!entry)
    {
      break;
    }

    if (!entry.isDirectory())
    {
      String entryName = entry.name();
      if (isFrameFile(entryName))
      {
        frames[frameCount] = joinPath(clip.folderPath, baseName(entryName));
        ++frameCount;
      }
    }

    entry.close();
  }

  clipDirectory.close();
  sortStrings(frames, frameCount);
  return frameCount;
}

uint16_t readLittleEndian16(File &file)
{
  uint8_t bytes[2] = {0, 0};
  file.read(bytes, sizeof(bytes));
  return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

uint32_t readLittleEndian32(File &file)
{
  uint8_t bytes[4] = {0, 0, 0, 0};
  file.read(bytes, sizeof(bytes));
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

void stopAudio()
{
  if (audioPlayback.file)
  {
    audioPlayback.file.close();
  }
  audioPlayback = AudioPlayback();
  if (audioReady)
  {
    i2s_zero_dma_buffer(AUDIO_PORT);
  }
}

bool ensureAudioOutput(uint32_t sampleRate)
{
  if (!audioReady)
  {
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    config.sample_rate = sampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = AUDIO_CHANNEL_FORMAT;
    config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;

    if (i2s_driver_install(AUDIO_PORT, &config, 0, nullptr) != ESP_OK)
    {
      Serial.println("Failed to install I2S audio driver");
      return false;
    }

    if (i2s_set_dac_mode(AUDIO_DAC_CHANNEL) != ESP_OK)
    {
      Serial.println("Failed to set DAC mode");
      return false;
    }

    audioReady = true;
  }

  if (i2s_set_clk(AUDIO_PORT, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO) != ESP_OK)
  {
    Serial.println("Failed to update I2S clock");
    return false;
  }

  i2s_zero_dma_buffer(AUDIO_PORT);
  return true;
}

bool startAudio(const String &wavPath)
{
  stopAudio();

  File file = SD.open(wavPath.c_str(), FILE_READ);
  if (!file)
  {
    Serial.printf("Unable to open WAV: %s\n", wavPath.c_str());
    return false;
  }

  char riff[4];
  char wave[4];
  if (file.readBytes(riff, sizeof(riff)) != 4 || memcmp(riff, "RIFF", 4) != 0)
  {
    file.close();
    return false;
  }

  (void)readLittleEndian32(file);
  if (file.readBytes(wave, sizeof(wave)) != 4 || memcmp(wave, "WAVE", 4) != 0)
  {
    file.close();
    return false;
  }

  bool foundFormatChunk = false;
  bool foundDataChunk = false;
  uint16_t formatCode = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataSize = 0;

  while (file.available())
  {
    char chunkId[4];
    if (file.readBytes(chunkId, sizeof(chunkId)) != 4)
    {
      break;
    }

    uint32_t chunkSize = readLittleEndian32(file);
    uint32_t nextChunk = file.position() + chunkSize + (chunkSize & 1U);

    if (memcmp(chunkId, "fmt ", 4) == 0)
    {
      formatCode = readLittleEndian16(file);
      channels = readLittleEndian16(file);
      sampleRate = readLittleEndian32(file);
      (void)readLittleEndian32(file);
      (void)readLittleEndian16(file);
      bitsPerSample = readLittleEndian16(file);
      foundFormatChunk = true;
    }
    else if (memcmp(chunkId, "data", 4) == 0)
    {
      dataSize = chunkSize;
      foundDataChunk = true;
      break;
    }

    file.seek(nextChunk);
  }

  if (!foundFormatChunk || !foundDataChunk || formatCode != 1 || channels < 1 || channels > 2 ||
      (bitsPerSample != 8 && bitsPerSample != 16) || sampleRate == 0)
  {
    file.close();
    Serial.printf("Unsupported WAV file: %s\n", wavPath.c_str());
    return false;
  }

  if (!ensureAudioOutput(sampleRate))
  {
    file.close();
    return false;
  }

  audioPlayback.file = file;
  audioPlayback.active = true;
  audioPlayback.dataRemaining = dataSize;
  audioPlayback.sampleRate = sampleRate;
  audioPlayback.channels = channels;
  audioPlayback.bitsPerSample = bitsPerSample;

  Serial.printf("Audio ready: %s (%lu Hz, %u-bit, %u channel)\n",
                wavPath.c_str(),
                static_cast<unsigned long>(sampleRate),
                static_cast<unsigned>(bitsPerSample),
                static_cast<unsigned>(channels));
  return true;
}

bool pumpAudio()
{
  if (!audioPlayback.active || !audioPlayback.file)
  {
    return false;
  }

  size_t requested = sizeof(wavInputBuffer);
  if (audioPlayback.dataRemaining < requested)
  {
    requested = audioPlayback.dataRemaining;
  }

  if (requested == 0)
  {
    stopAudio();
    return false;
  }

  size_t bytesRead = audioPlayback.file.read(wavInputBuffer, requested);
  if (bytesRead == 0)
  {
    stopAudio();
    return false;
  }

  audioPlayback.dataRemaining -= bytesRead;
  size_t outputSamples = 0;

  if (audioPlayback.bitsPerSample == 8)
  {
    for (size_t index = 0; index < bytesRead && outputSamples < (sizeof(wavOutputBuffer) / sizeof(wavOutputBuffer[0]));)
    {
      int32_t mono = 0;
      for (uint16_t channel = 0; channel < audioPlayback.channels && index < bytesRead; ++channel)
      {
        mono += (static_cast<int32_t>(wavInputBuffer[index++]) - 128) << 8;
      }

      mono /= audioPlayback.channels;
      int32_t dacValue = (mono >> 8) + 128;
      if (dacValue < 0)
      {
        dacValue = 0;
      }
      if (dacValue > 255)
      {
        dacValue = 255;
      }
      wavOutputBuffer[outputSamples++] = static_cast<uint16_t>(dacValue << 8);
    }
  }
  else
  {
    for (size_t index = 0; index + 1 < bytesRead && outputSamples < (sizeof(wavOutputBuffer) / sizeof(wavOutputBuffer[0]));)
    {
      int32_t mono = 0;
      for (uint16_t channel = 0; channel < audioPlayback.channels && index + 1 < bytesRead; ++channel)
      {
        int16_t sample = static_cast<int16_t>(wavInputBuffer[index] | (wavInputBuffer[index + 1] << 8));
        mono += sample;
        index += 2;
      }

      mono /= audioPlayback.channels;
      int32_t dacValue = (mono >> 8) + 128;
      if (dacValue < 0)
      {
        dacValue = 0;
      }
      if (dacValue > 255)
      {
        dacValue = 255;
      }
      wavOutputBuffer[outputSamples++] = static_cast<uint16_t>(dacValue << 8);
    }
  }

  size_t bytesWritten = 0;
  if (outputSamples > 0)
  {
    i2s_write(AUDIO_PORT, wavOutputBuffer, outputSamples * sizeof(uint16_t), &bytesWritten, portMAX_DELAY);
  }

  if (audioPlayback.dataRemaining == 0)
  {
    stopAudio();
  }

  return bytesWritten > 0;
}

int jpegDrawCallback(JPEGDRAW *draw)
{
  gfx->draw16bitBeRGBBitmap(draw->x, draw->y, draw->pPixels, draw->iWidth, draw->iHeight);
  return 1;
}

bool drawFrame(const String &framePath)
{
  File frameFile = SD.open(framePath.c_str(), FILE_READ);
  if (!frameFile || frameFile.isDirectory())
  {
    Serial.printf("Unable to open frame: %s\n", framePath.c_str());
    return false;
  }

  size_t frameSize = frameFile.size();
  uint8_t *frameBuffer = static_cast<uint8_t *>(malloc(frameSize));
  if (!frameBuffer)
  {
    frameFile.close();
    Serial.printf("Out of memory loading frame: %s\n", framePath.c_str());
    return false;
  }

  size_t bytesRead = frameFile.read(frameBuffer, frameSize);
  frameFile.close();
  if (bytesRead != frameSize)
  {
    free(frameBuffer);
    Serial.printf("Failed to read full frame: %s\n", framePath.c_str());
    return false;
  }

  bool opened = jpeg.openRAM(frameBuffer, frameSize, jpegDrawCallback);
  if (!opened)
  {
    free(frameBuffer);
    Serial.printf("JPEG open failed: %s\n", framePath.c_str());
    return false;
  }

  int frameX = 0;
  int frameY = 0;
  if (gfx->width() > jpeg.getWidth())
  {
    frameX = (gfx->width() - jpeg.getWidth()) / 2;
  }
  if (gfx->height() > jpeg.getHeight())
  {
    frameY = (gfx->height() - jpeg.getHeight()) / 2;
  }

  gfx->fillScreen(RGB565_BLACK);
  int decodeResult = jpeg.decode(frameX, frameY, 0);
  jpeg.close();
  free(frameBuffer);

  return decodeResult == JPEG_SUCCESS;
}

void playClip(const MediaClip &clip)
{
  String *clipFrames = new String[clip.frameCount];
  if (!clipFrames)
  {
    Serial.printf("Unable to allocate frame list for %s\n", clip.folderPath.c_str());
    return;
  }

  size_t frameCount = loadFrameList(clip, clipFrames, clip.frameCount);
  if (frameCount == 0)
  {
    Serial.printf("No frames found for %s\n", clip.folderPath.c_str());
    delete[] clipFrames;
    return;
  }

  Serial.printf("Playing %s in random mode\n", clip.folderPath.c_str());
  if (clip.hasAudio)
  {
    startAudio(clip.audioPath);
  }
  else
  {
    stopAudio();
  }

  uint16_t clipFps = clip.fps > 0 ? clip.fps : 1;
  uint32_t frameIntervalMs = 1000UL / clipFps;
  uint32_t nextFrameAt = millis();

  for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
  {
    if (pollSkipButton())
    {
      break;
    }

    drawFrame(clipFrames[frameIndex]);
    nextFrameAt += frameIntervalMs;

    while (static_cast<int32_t>(millis() - nextFrameAt) < 0)
    {
      if (pollSkipButton())
      {
        frameIndex = frameCount;
        break;
      }

      if (!pumpAudio())
      {
        delay(1);
      }
    }
  }

  while (!pollSkipButton() && audioPlayback.active)
  {
    if (!pumpAudio())
    {
      delay(1);
    }
  }

  stopAudio();
  delete[] clipFrames;
}

void setup()
{
  Serial.begin(115200);
  randomSeed(micros());

  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  if (!gfx->begin(DISPLAY_SPI_SPEED))
  {
    while (true)
    {
      delay(1000);
    }
  }

  gfx->setRotation(1);
  showStatus("Starting random player");

  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSpi, SD_SPI_SPEED))
  {
    showStatus("SD init failed", RGB565_RED);
    while (true)
    {
      delay(1000);
    }
  }

  discoverMedia();
  shufflePlaylist();
}

void loop()
{
  if (clipCount == 0)
  {
    delay(250);
    return;
  }

  if (currentOrderIndex >= clipCount)
  {
    shufflePlaylist();
  }

  const MediaClip &clip = clips[clipOrder[currentOrderIndex]];
  playClip(clip);
  ++currentOrderIndex;
}
