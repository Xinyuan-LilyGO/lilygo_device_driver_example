/*
 * @Description: ES8390 麦克风采集与扬声器播放回环实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-22 17:04:20
 * @License: GPL 3.0
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microphone_speaker_loopback.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    (defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) &&    \
        defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2))

namespace {

constexpr size_t kAudioBufferSampleCount = 1024;
constexpr size_t kAudioBufferSize = kAudioBufferSampleCount * sizeof(int16_t);

}  // namespace

void RunEs8390MicrophoneSpeakerLoopback() {
  auto& driver = common::GetDriver();
  if (!driver.IsEs8390Ready() ||
      !driver.SetEs8390OperatingMode(
          common::DeviceDriver::Es8390OperatingMode::kActive)) {
    printf("ES8390 active operating mode setup failed\n");
    return;
  }

  esp_codec_dev_handle_t input_codec_dev = driver.es8390_input_codec_dev();
  esp_codec_dev_handle_t output_codec_dev = driver.es8390_output_codec_dev();
  if (input_codec_dev == nullptr || output_codec_dev == nullptr) {
    printf("ES8390 codec device is unavailable\n");
    return;
  }

  auto buffer = std::make_unique<int16_t[]>(kAudioBufferSampleCount);
  printf("ES8390 microphone speaker loopback started\n");

  while (true) {
    int result =
        esp_codec_dev_read(input_codec_dev, buffer.get(), kAudioBufferSize);
    if (result != ESP_CODEC_DEV_OK) {
      printf("ES8390 audio read failed (error code: %#X)\n", result);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    result =
        esp_codec_dev_write(output_codec_dev, buffer.get(), kAudioBufferSize);
    if (result != ESP_CODEC_DEV_OK) {
      printf("ES8390 audio write failed (error code: %#X)\n", result);
    }
  }
}

#endif
