/*
 * @Description: 演示关闭外设并进入无唤醒源深度睡眠
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-08-03 09:46:03
 * @License: GPL 3.0
 */
#include "common.h"
#include "driver/uart.h"
#include "esp_sleep.h"

namespace {

/**
 * @brief 等待控制台发送完成，避免最后一行日志被深度睡眠截断
 */
void WaitForConsoleTxIdle() {
  uart_wait_tx_idle_polling(
      static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM));
}

/**
 * @brief 为深度睡眠重新配置并启用GPIO自动隔离
 */
void ConfigureDeepSleepGpioIsolation() {
  // 将可隔离GPIO的睡眠方向与上下拉配置为高阻浮空状态。
  esp_sleep_config_gpio_isolate();
  // 进入睡眠时自动切换到上述睡眠配置。
  esp_sleep_enable_gpio_switch(true);
}

}  // namespace

extern "C" void app_main(void) {
  printf("Sleep example on %s\n", common::kBoardName);

  auto& driver = common::GetDriver();
  if (!driver.Init(common::DeviceDriver::InitMode::kSync)) {
    printf("Device initialization reported a failure; continue shutdown\n");
  }

  if (!driver.SetPowerState(common::DeviceDriver::PowerState::kOff)) {
    printf("Device sleep preparation reported a failure\n");
  }

  ConfigureDeepSleepGpioIsolation();
  printf("Entering deep sleep without wake-up sources\n");
  WaitForConsoleTxIdle();
  esp_deep_sleep_start();
}
