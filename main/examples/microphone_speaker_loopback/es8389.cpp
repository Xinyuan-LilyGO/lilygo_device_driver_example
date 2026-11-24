/*
 * @Description: ES8389 麦克风采集与扬声器播放回环实现
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microphone_speaker_loopback.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)

namespace {

constexpr size_t kAudioSamplesPerFrame = 2;
constexpr size_t kMicrophoneInputSlot = 0;
constexpr size_t kRightOutputSlot = 1;
constexpr size_t kAudioBufferFrameCount = 512;
constexpr size_t kAudioBufferSampleCount =
    kAudioBufferFrameCount * kAudioSamplesPerFrame;
constexpr size_t kAudioBufferSize = kAudioBufferSampleCount * sizeof(int16_t);
constexpr int kLoopbackOutputVolume = 40;

}  // namespace

void RunEs8389MicrophoneSpeakerLoopback() {
  using OperatingMode = common::DeviceDriver::Es8389OperatingMode;

  auto& driver = common::GetDriver();
  if (!driver.IsEs8389Ready()) {
    printf("ES8389 init failed\n");
    return;
  }

  esp_codec_dev_handle_t input_codec_dev = driver.es8389_input_codec_dev();
  esp_codec_dev_handle_t output_codec_dev = driver.es8389_output_codec_dev();
  auto buffer = std::make_unique<int16_t[]>(kAudioBufferSampleCount);

  if (!driver.SetAudioPowerEnabled(true)) {
    printf("ES8389 audio power enable failed\n");
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(10));

  if (!driver.SetEs8389OperatingMode(OperatingMode::kActive)) {
    printf("ES8389 active operating mode setup failed\n");
    esp_codec_dev_close(input_codec_dev);
    esp_codec_dev_close(output_codec_dev);
    driver.SetAudioPowerEnabled(false);
    return;
  }

  const int volume_result =
      esp_codec_dev_set_out_vol(output_codec_dev, kLoopbackOutputVolume);
  if (volume_result != ESP_CODEC_DEV_OK) {
    printf("ES8389 output volume setup failed (error code: %#X)\n",
        volume_result);
    driver.SetEs8389OperatingMode(OperatingMode::kSleep);
    driver.SetAudioPowerEnabled(false);
    return;
  }

  printf("ES8389 microphone speaker loopback started\n");
  while (true) {
    int result =
        esp_codec_dev_read(input_codec_dev, buffer.get(), kAudioBufferSize);
    if (result != ESP_CODEC_DEV_OK) {
      printf("ES8389 audio read failed (error code: %#X)\n", result);
      break;
    }

    for (size_t frame_start = 0; frame_start < kAudioBufferSampleCount;
         frame_start += kAudioSamplesPerFrame) {
      buffer[frame_start + kRightOutputSlot] =
          buffer[frame_start + kMicrophoneInputSlot];
    }

    result =
        esp_codec_dev_write(output_codec_dev, buffer.get(), kAudioBufferSize);
    if (result != ESP_CODEC_DEV_OK) {
      printf("ES8389 audio write failed (error code: %#X)\n", result);
      break;
    }
  }

  driver.SetEs8389OperatingMode(OperatingMode::kSleep);
  driver.SetAudioPowerEnabled(false);
}

#endif
