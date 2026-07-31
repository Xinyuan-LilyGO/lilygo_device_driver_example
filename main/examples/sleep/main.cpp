/*
 * @Description: 演示关闭外设后直接进入无唤醒源的深度睡眠
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-31 15:45:00
 * @License: GPL 3.0
 */
#include "common.h"
#include "driver/uart.h"
#include "esp_sleep.h"

extern "C" void app_main(void) {
  printf("Sleep example on %s\n", common::kBoardName);

  auto& driver = common::GetDriver();
  driver.Init(common::DeviceDriver::InitMode::kSync);

  if (!driver.SetPowerState(common::DeviceDriver::PowerState::kOff)) {
    printf("Device sleep preparation reported a failure\n");
  }
  printf("Entering deep sleep without wake-up sources\n");
  uart_wait_tx_idle_polling(
      static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM));
  esp_deep_sleep_start();
}
