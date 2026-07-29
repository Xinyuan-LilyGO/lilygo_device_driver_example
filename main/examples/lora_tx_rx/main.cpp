/*
 * @Description: 根据当前硬件配置运行对应的 LoRa 数据收发测试
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-29 18:00:58
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_tx_rx.h"

extern "C" void app_main(void) {
  printf("LoRa TX/RX example on %s\n", common::kBoardName);
  printf("LoRa: %lu MHz, SF12, BW %lu kHz, CR 4/5, "
         "public sync word 0x34\n",
      static_cast<unsigned long>(lora_tx_rx::kFrequencyHz / 1000000U),
      static_cast<unsigned long>(lora_tx_rx::kBandwidthKhz));

  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  auto& driver = common::GetDriver();
  switch (driver.radio_type()) {
    case common::board::device::RadioType::kSx1262:
      lora_tx_rx::RunSx1262();
      break;
    case common::board::device::RadioType::kLr2021:
      lora_tx_rx::RunLr2021();
      break;
    default:
      printf("No supported LoRa radio was detected\n");
      break;
  }
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  lora_tx_rx::RunLr1121();
#endif
}
