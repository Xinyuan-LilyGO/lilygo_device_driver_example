/*
 * @Description: ES8311 麦克风采集与扬声器播放回环实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "microphone_speaker_loopback.h"

#include <memory>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace {

constexpr size_t kAudioBufferSampleCount = 1024;
constexpr size_t kAudioBufferSize =
    kAudioBufferSampleCount * sizeof(int16_t);

}  // namespace

void RunEs8311MicrophoneSpeakerLoopback() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8311Ready()) {
    printf("ES8311 init failed\n");
    return;
  }
  auto& es8311 = driver.chip().es8311;

  if (!driver.SetEs8311PowerState(
          common::DeviceDriver::Es8311PowerState::kDuplex)) {
    printf("ES8311 duplex power state setup failed\n");
    return;
  }

  auto buffer = std::make_unique<int16_t[]>(kAudioBufferSampleCount);
  printf("ES8311 microphone speaker loopback started\n");

  while (true) {
    const size_t bytes_read = es8311->ReadI2s(buffer.get(), kAudioBufferSize);
    if (bytes_read == 0) {
      printf("ES8311 audio read failed\n");
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    const size_t bytes_written =
        es8311->WriteI2s(buffer.get(), bytes_read);
    if (bytes_written != bytes_read) {
      printf("ES8311 audio write failed (%u/%u bytes)\n",
          static_cast<unsigned int>(bytes_written),
          static_cast<unsigned int>(bytes_read));
    }
  }
}

#endif
