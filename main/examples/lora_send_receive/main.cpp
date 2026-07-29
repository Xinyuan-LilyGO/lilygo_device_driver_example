/*
 * @Description: 根据当前硬件配置运行对应的 LoRa 数据收发测试
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_send_receive.h"

extern "C" void app_main(void) {
  printf("LoRa send/receive example on %s\n", common::kBoardName);

  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  lora_send_receive::RunSx1262();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  lora_send_receive::RunLr1121();
#endif
}
