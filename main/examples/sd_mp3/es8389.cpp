#include "sd_mp3.h"

#include "common.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)

namespace {

namespace codec = common::board::device::es8389;

esp_codec_dev_handle_t g_output_codec_dev = nullptr;
uint32_t g_sample_rate = 0;

}  // namespace

bool InitSdMp3Audio() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8389Ready() ||
      !driver.SetEs8389OperatingMode(
          common::DeviceDriver::Es8389OperatingMode::kSleep)) {
    return false;
  }

  g_output_codec_dev = driver.es8389_output_codec_dev();
  g_sample_rate = 0;
  return g_output_codec_dev != nullptr;
}

void DeinitSdMp3Audio() {
  if (g_output_codec_dev != nullptr) {
    esp_codec_dev_close(g_output_codec_dev);
    common::GetDriver().SetAudioPowerEnabled(false);
  }
  g_output_codec_dev = nullptr;
  g_sample_rate = 0;
}

bool ConfigureSdMp3Audio(uint32_t sample_rate) {
  if (g_output_codec_dev == nullptr || sample_rate == 0) {
    return false;
  }
  if (sample_rate == g_sample_rate) {
    return true;
  }
  if (esp_codec_dev_close(g_output_codec_dev) != ESP_CODEC_DEV_OK) {
    return false;
  }
  g_sample_rate = 0;

  if (!common::GetDriver().SetAudioPowerEnabled(true)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(10));

  esp_codec_dev_sample_info_t sample_info = {
      .bits_per_sample = codec::kBitsPerSample,
      .channel = codec::kChannel,
      .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                      ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
      .sample_rate = sample_rate,
      .mclk_multiple = codec::kMclkMultiple,
  };
  if (esp_codec_dev_open(g_output_codec_dev, &sample_info) !=
      ESP_CODEC_DEV_OK) {
    esp_codec_dev_close(g_output_codec_dev);
    return false;
  }

  g_sample_rate = sample_rate;
  return true;
}

bool WriteSdMp3Pcm(const void* data, size_t byte_count) {
  return g_sample_rate != 0 &&
         esp_codec_dev_write(g_output_codec_dev, const_cast<void*>(data),
             static_cast<int>(byte_count)) == ESP_CODEC_DEV_OK;
}

#endif
