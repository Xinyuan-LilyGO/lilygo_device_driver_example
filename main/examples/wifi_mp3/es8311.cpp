/*
 * @Description: ES8311 网络 MP3 音频输出实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "wifi_mp3.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace {

cpp_bus_driver::Es8311* g_es8311 = nullptr;

}  // namespace

bool InitWifiMp3Audio() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8311Ready() ||
      !driver.SetEs8311OperatingMode(
          common::DeviceDriver::Es8311OperatingMode::kPlayback)) {
    return false;
  }

  g_es8311 = driver.chip().es8311.get();
  return g_es8311 != nullptr;
}

bool WriteWifiMp3Pcm(const void* data, size_t byte_count) {
  return g_es8311 != nullptr &&
         g_es8311->WriteI2s(data, byte_count) == byte_count;
}

#endif
