/*
 * @Description: ES8389 网络 MP3 音频输出实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "wifi_mp3.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

namespace {

esp_codec_dev_handle_t g_output_codec_dev = nullptr;

}  // namespace

bool InitWifiMp3Audio() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8389Ready() ||
      !driver.SetEs8389OperatingMode(
          common::DeviceDriver::Es8389OperatingMode::kActive)) {
    return false;
  }

  g_output_codec_dev = driver.es8389_output_codec_dev();
  return g_output_codec_dev != nullptr;
}

bool WriteWifiMp3Pcm(const void* data, size_t byte_count) {
  return g_output_codec_dev != nullptr &&
         esp_codec_dev_write(g_output_codec_dev, const_cast<void*>(data),
             static_cast<int>(byte_count)) == ESP_CODEC_DEV_OK;
}

#endif
