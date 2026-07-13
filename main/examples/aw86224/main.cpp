/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-07-10 10:38:17
 * @LastEditTime: 2026-07-11 11:52:04
 * @License: GPL 3.0
 */
#include "common.h"

namespace {

constexpr uint8_t kGainLevels[] = {
    16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255};
constexpr uint8_t kLoopCount = 1;
constexpr uint32_t kPlayMs = 220;
constexpr uint32_t kStopMs = 180;

}  // namespace

extern "C" void app_main(void) {
  printf("AW86224 example on %s\n", common::kBoardName);

  auto& driver = common::GetDriver();
  common::InitDriver();
  auto& aw86224 = driver.chip().aw86224;
  if (aw86224 == nullptr || !driver.status().aw86224.init_flag) {
    printf("AW86224 init failed\n");
    return;
  }

  while (true) {
    const auto& info = driver.status().aw86224.ram_waveform_info;
    if (info.data == nullptr || info.waveform_count == 0) {
      printf("AW86224 RAM waveform init failed\n");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    printf("Library: %s, sequences: %u, rated F0: %u Hz\n", info.name,
        static_cast<unsigned int>(info.waveform_count),
        static_cast<unsigned int>(info.rated_f0_hz));
    printf("Input voltage: %.06f V\n", aw86224->GetInputVoltage());

    for (uint8_t gain : kGainLevels) {
      for (uint8_t sequence = 1; sequence <= info.waveform_count; ++sequence) {
        printf("Play sequence %u, gain %u\n",
            static_cast<unsigned int>(sequence),
            static_cast<unsigned int>(gain));
        aw86224->PlayRamWaveform(sequence, kLoopCount, gain);
        vTaskDelay(pdMS_TO_TICKS(kPlayMs));
        aw86224->StopRamPlaybackWaveform();
        vTaskDelay(pdMS_TO_TICKS(kStopMs));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}
