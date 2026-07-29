/*
 * @Description: 根据当前无线芯片运行 LoRa 接收灵敏度测试
 * @Author: LILYGO_L
 * @Date: 2026-07-29 15:09:12
 * @LastEditTime: 2026-07-29 15:19:19
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_rx_sensitivity.h"

extern "C" void app_main(void) {
  printf("LoRa RX sensitivity example on %s\n",
      common::kBoardName);

  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  auto& driver = common::GetDriver();
  switch (driver.radio_type()) {
    case common::board::device::RadioType::kSx1262:
      lora_rx_sensitivity::RunSx1262();
      break;
    case common::board::device::RadioType::kLr2021:
      lora_rx_sensitivity::RunLr2021();
      break;
    default:
      printf("No supported LoRa radio was detected\n");
      break;
  }
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  lora_rx_sensitivity::RunLr1121();
#endif
}
