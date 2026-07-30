/*
 * @Description: 控制无线协处理器进入下载模式或保持正常供电的示例
 * @Author: LILYGO_L
 * @Date: 2026-07-30 14:57:16
 * @LastEditTime: 2026-07-30 15:10:29
 * @License: GPL 3.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "common.h"

namespace {

/**
 * @brief 初始化协处理器电源和下载模式所需的最小板级硬件
 * @return 初始化成功返回 true，否则返回 false
 */
bool InitCoprocessorControlHardware() {
  auto& driver = common::GetDriver();
  driver.CreateDrivers();

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  return driver.InitPower() && driver.InitAxp517() &&
         driver.InitXl9535() && driver.ConfigXl9535();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return driver.InitXl9535() && driver.InitPower() &&
         driver.ConfigXl9535();
#endif
}

/**
 * @brief 使协处理器进入下载模式；无 BOOT 控制时仅恢复正常供电
 * @return 控制时序执行成功返回 true，否则返回 false
 */
bool PrepareCoprocessor() {
  auto& driver = common::GetDriver();

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  if (!driver.SetUartTarget(common::DeviceDriver::UartTarget::kEsp32c5)) {
    printf("Failed to route the UART to ESP32-C5\n");
    return false;
  }
  if (!driver.EnterEsp32c5DownloadMode()) {
    printf("Failed to place ESP32-C5 into download mode\n");
    return false;
  }

  printf("ESP32-C5 entered download mode\n");
  printf("The external UART is connected to ESP32-C5\n");
  return true;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  auto* io_expander = driver.chip().xl9535.get();
  if (io_expander == nullptr ||
      !io_expander->GpioWrite(
          common::board::gpio::xl9535::kEsp32c6En, 1)) {
    printf("Failed to release ESP32-C6 reset control\n");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
  if (!io_expander->SetGpioMode(
          common::board::gpio::xl9535::kEsp32c6En,
          cpp_bus_driver::Xl95x5::Mode::kInput)) {
    printf("Failed to set ESP32-C6 reset control to high impedance\n");
    return false;
  }

  printf("ESP32-C6 reset is released and powered normally\n");
  printf("ESP32-C6 BOOT and reset are controlled by the external buttons\n");
  return true;
#endif
}

}  // namespace

extern "C" void app_main(void) {
  printf("Coprocessor download-mode helper on %s\n", common::kBoardName);

  if (!InitCoprocessorControlHardware()) {
    printf("Coprocessor control hardware initialization failed\n");
  } else if (!PrepareCoprocessor()) {
    printf("Coprocessor preparation failed\n");
  } else {
    printf("Coprocessor preparation completed\n");
  }

  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}
