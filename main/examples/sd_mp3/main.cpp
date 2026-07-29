/*
 * @Description: 从 SD 卡读取、解码并播放 MP3 音频文件
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "audio/mp3_metadata.h"
#include "common.h"
#include "sd_mp3.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "esp_audio_dec.h"
#include "esp_audio_dec_default.h"

namespace {

constexpr size_t kReadBufferSize = 4 * 1024;
constexpr size_t kPcmBufferSize = 8 * 1024;
const std::string kMp3FilePath =
    std::string(common::board::device::sd::kBasePath) + "/music.mp3";

cpp_bus_driver::Tool g_tool;

bool PlayMp3File(const char* path) {
  common::audio::Mp3Metadata metadata;
  if (!common::audio::ReadMp3Metadata(path, &metadata)) {
    printf("Unable to read MP3 stream metadata: %s\n", path);
    return false;
  }
  common::audio::PrintMp3Input(path, metadata);
  if (!ConfigureSdMp3Audio(metadata.sample_rate_hz)) {
    printf("Audio sample rate configuration failed\n");
    return false;
  }

  FILE* file = fopen(path, "rb");
  if (file == nullptr) {
    printf("Open MP3 file failed: %s\n", path);
    return false;
  }
  if (fseek(
          file, static_cast<long>(metadata.audio_data_offset), SEEK_SET) != 0) {
    printf("Seek to MP3 audio stream failed\n");
    fclose(file);
    return false;
  }

  esp_audio_dec_register_default();
  esp_audio_dec_cfg_t decoder_config = {
      .type = ESP_AUDIO_TYPE_MP3,
  };
  esp_audio_dec_handle_t decoder = nullptr;
  if (esp_audio_dec_open(&decoder_config, &decoder) != ESP_AUDIO_ERR_OK) {
    printf("MP3 decoder initialization failed\n");
    esp_audio_dec_unregister_default();
    fclose(file);
    return false;
  }

  auto read_buffer = std::make_unique<uint8_t[]>(kReadBufferSize);
  auto pcm_buffer = std::make_unique<uint8_t[]>(kPcmBufferSize);
  size_t remaining_bytes = 0;
  bool playback_ok = true;

  while (playback_ok) {
    const size_t read_bytes = fread(read_buffer.get() + remaining_bytes, 1,
        kReadBufferSize - remaining_bytes, file);
    const size_t total_input = remaining_bytes + read_bytes;
    if (total_input == 0) {
      break;
    }

    esp_audio_dec_in_raw_t input = {
        .buffer = read_buffer.get(),
        .len = total_input,
    };
    esp_audio_dec_out_frame_t output = {
        .buffer = pcm_buffer.get(),
        .len = kPcmBufferSize,
    };

    while (input.len > 0) {
      output.len = kPcmBufferSize;
      output.decoded_size = 0;
      const esp_audio_err_t result =
          esp_audio_dec_process(decoder, &input, &output);
      if (result != ESP_AUDIO_ERR_OK) {
        if (result == ESP_AUDIO_ERR_FAIL) {
          printf("MP3 decoding failed\n");
          playback_ok = false;
        }
        break;
      }

      if (output.decoded_size > 0 &&
          !WriteSdMp3Pcm(output.buffer, output.decoded_size)) {
        printf("PCM audio output failed\n");
        playback_ok = false;
        break;
      }

      if (input.consumed == 0) {
        break;
      }
      input.buffer += input.consumed;
      input.len -= input.consumed;
    }

    remaining_bytes = input.len;
    if (remaining_bytes > 0) {
      std::memmove(read_buffer.get(), input.buffer, remaining_bytes);
    }
    if (read_bytes == 0 && remaining_bytes == total_input) {
      break;
    }
  }

  esp_audio_dec_close(decoder);
  esp_audio_dec_unregister_default();
  fclose(file);
  return playback_ok;
}

bool InitBootButton() {
  return g_tool.SetGpioMode(common::BootButtonGpio(),
      cpp_bus_driver::Tool::GpioMode::kInput,
      cpp_bus_driver::Tool::GpioStatus::kPullup);
}

bool BootButtonPressed() {
  return g_tool.GpioRead(common::BootButtonGpio()) == 0;
}

}  // namespace

extern "C" void app_main(void) {
  printf("SD MP3 example on %s\n", common::kBoardName);
  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

  auto& driver = common::GetDriver();
  if (!driver.IsSdmmcReady() &&
      !driver.InitSdmmc(
          common::board::device::sd::kBasePath, SDMMC_FREQ_52M)) {
    printf("SD card initialization failed\n");
    return;
  }
  if (!InitBootButton()) {
    printf("BOOT button initialization failed\n");
    return;
  }
  if (!InitSdMp3Audio()) {
    printf("Audio output initialization failed\n");
    return;
  }

  printf("Press BOOT to play: %s\n", kMp3FilePath.c_str());
  bool was_pressed = false;
  while (true) {
    const bool pressed = BootButtonPressed();
    if (pressed && !was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (BootButtonPressed()) {
        printf("MP3 playback started\n");
        const bool played = PlayMp3File(kMp3FilePath.c_str());
        printf("MP3 playback %s\n", played ? "finished" : "failed");
      }
    }
    was_pressed = pressed;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
