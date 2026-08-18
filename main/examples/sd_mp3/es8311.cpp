/*
 * @Description: ES8311 SD 卡 MP3 音频输出实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "sd_mp3.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace {

cpp_bus_driver::Es8311* g_es8311 = nullptr;
uint32_t g_sample_rate = common::board::device::es8311::kSampleRate;

}  // namespace

bool InitSdMp3Audio() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8311Ready() ||
      !driver.SetEs8311OperatingMode(
          common::DeviceDriver::Es8311OperatingMode::kPlayback)) {
    return false;
  }

  g_es8311 = driver.chip().es8311.get();
  g_sample_rate = common::board::device::es8311::kSampleRate;
  return g_es8311 != nullptr;
}

bool ConfigureSdMp3Audio(uint32_t sample_rate) {
  if (g_es8311 == nullptr || sample_rate == 0) {
    return false;
  }
  if (sample_rate == g_sample_rate) {
    return true;
  }

  if (!g_es8311->SetI2sChannelEnable(false)) {
    return false;
  }
  const bool clock_configured = g_es8311->SetClockReconfig(
      common::board::device::es8311::kMclkMultiple, sample_rate);
  const bool channel_enabled = g_es8311->SetI2sChannelEnable(true);
  if (clock_configured && channel_enabled) {
    g_sample_rate = sample_rate;
    return true;
  }
  return false;
}

bool WriteSdMp3Pcm(const void* data, size_t byte_count) {
  return g_es8311 != nullptr &&
         g_es8311->WriteI2s(data, byte_count) == byte_count;
}

#endif
