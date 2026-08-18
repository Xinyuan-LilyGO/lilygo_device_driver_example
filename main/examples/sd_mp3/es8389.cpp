/*
 * @Description: ES8389 SD 卡 MP3 音频输出实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "sd_mp3.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

namespace {

esp_codec_dev_handle_t g_output_codec_dev = nullptr;
uint32_t g_sample_rate = common::board::device::es8389::kSampleRate;

}  // namespace

bool InitSdMp3Audio() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8389Ready() ||
      !driver.SetEs8389OperatingMode(
          common::DeviceDriver::Es8389OperatingMode::kActive)) {
    return false;
  }

  g_output_codec_dev = driver.es8389_output_codec_dev();
  g_sample_rate = common::board::device::es8389::kSampleRate;
  return g_output_codec_dev != nullptr;
}

bool ConfigureSdMp3Audio(uint32_t sample_rate) {
  if (g_output_codec_dev == nullptr || sample_rate == 0) {
    return false;
  }
  if (sample_rate == g_sample_rate) {
    return true;
  }

  esp_codec_dev_sample_info_t sample_info = {
      .bits_per_sample = common::board::device::es8389::kBitsPerSample,
      .channel = common::board::device::es8389::kChannel,
      .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                      ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
      .sample_rate = sample_rate,
      .mclk_multiple = common::board::device::es8389::kMclkMultiple,
  };
  if (esp_codec_dev_close(g_output_codec_dev) != ESP_CODEC_DEV_OK ||
      esp_codec_dev_open(g_output_codec_dev, &sample_info) !=
          ESP_CODEC_DEV_OK) {
    return false;
  }

  g_sample_rate = sample_rate;
  return true;
}

bool WriteSdMp3Pcm(const void* data, size_t byte_count) {
  return g_output_codec_dev != nullptr &&
         esp_codec_dev_write(g_output_codec_dev, const_cast<void*>(data),
             static_cast<int>(byte_count)) == ESP_CODEC_DEV_OK;
}

#endif
