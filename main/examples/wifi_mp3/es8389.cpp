/*
 * @Description: ES8389 网络 MP3 音频输出实现
 */
#include "common.h"
#include "wifi_mp3.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)

namespace {

namespace codec = common::board::device::es8389;

esp_codec_dev_handle_t g_output_codec_dev = nullptr;

}  // namespace

bool InitWifiMp3Audio(const esp_audio_dec_info_t& stream_info) {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8389Ready() ||
      stream_info.bits_per_sample != codec::kBitsPerSample ||
      stream_info.sample_rate == 0 || stream_info.channel == 0 ||
      stream_info.channel > codec::kChannel ||
      !driver.SetEs8389OperatingMode(
          common::DeviceDriver::Es8389OperatingMode::kSleep)) {
    return false;
  }

  const auto output_codec_dev = driver.es8389_output_codec_dev();
  if (output_codec_dev == nullptr || !driver.SetAudioPowerEnabled(true)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(10));

  esp_codec_dev_sample_info_t sample_info = {
      .bits_per_sample = codec::kBitsPerSample,
      .channel = stream_info.channel,
      .channel_mask = 0,
      .sample_rate = stream_info.sample_rate,
      .mclk_multiple = codec::kMclkMultiple,
  };
  if (esp_codec_dev_open(output_codec_dev, &sample_info) != ESP_CODEC_DEV_OK) {
    esp_codec_dev_close(output_codec_dev);
    driver.SetAudioPowerEnabled(false);
    return false;
  }

  g_output_codec_dev = output_codec_dev;
  return true;
}

void DeinitWifiMp3Audio() {
  if (g_output_codec_dev != nullptr) {
    esp_codec_dev_close(g_output_codec_dev);
    common::GetDriver().SetAudioPowerEnabled(false);
    g_output_codec_dev = nullptr;
  }
}

bool WriteWifiMp3Pcm(const void* data, size_t byte_count) {
  return g_output_codec_dev != nullptr &&
         esp_codec_dev_write(g_output_codec_dev, const_cast<void*>(data),
             static_cast<int>(byte_count)) == ESP_CODEC_DEV_OK;
}

#endif
