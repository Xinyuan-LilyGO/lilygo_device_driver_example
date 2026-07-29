/*
 * @Description: 根据当前硬件配置运行麦克风到扬声器的音频回环测试
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "microphone_speaker_loopback.h"

extern "C" void app_main(void) {
  printf("Microphone speaker loopback example on %s\n", common::kBoardName);

  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  RunEs8311MicrophoneSpeakerLoopback();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  RunEs8389MicrophoneSpeakerLoopback();
#endif
}
