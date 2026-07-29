/*
 * @Description: ES8389 麦克风采集与扬声器播放回环实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "microphone_speaker_loopback.h"

#include <memory>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

namespace {

constexpr size_t kAudioBufferSampleCount = 1024;
constexpr size_t kAudioBufferSize =
    kAudioBufferSampleCount * sizeof(int16_t);

}  // namespace

void RunEs8389MicrophoneSpeakerLoopback() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8389Ready() ||
      !driver.SetEs8389PowerState(
          common::DeviceDriver::Es8389PowerState::kActive)) {
    printf("ES8389 active power state setup failed\n");
    return;
  }

  esp_codec_dev_handle_t input_codec_dev =
      driver.es8389_input_codec_dev();
  esp_codec_dev_handle_t output_codec_dev =
      driver.es8389_output_codec_dev();
  if (input_codec_dev == nullptr || output_codec_dev == nullptr) {
    printf("ES8389 codec device is unavailable\n");
    return;
  }

  auto buffer = std::make_unique<int16_t[]>(kAudioBufferSampleCount);
  printf("ES8389 microphone speaker loopback started\n");

  while (true) {
    int result =
        esp_codec_dev_read(input_codec_dev, buffer.get(), kAudioBufferSize);
    if (result != ESP_CODEC_DEV_OK) {
      printf("ES8389 audio read failed (error code: %#X)\n", result);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    result =
        esp_codec_dev_write(output_codec_dev, buffer.get(), kAudioBufferSize);
    if (result != ESP_CODEC_DEV_OK) {
      printf("ES8389 audio write failed (error code: %#X)\n", result);
    }
  }
}

#endif
