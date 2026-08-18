/*
 * @Description: 准备无线协处理器固件下载所需的电源和控制信号
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
  if (!driver.InitMinimal()) {
    return false;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  return driver.InitXl9535();
#else
  return true;
#endif
}

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
/**
 * @brief 为外部调试器开启 nRF9151 电源
 * @return 电源控制成功返回 true，否则返回 false
 */
bool EnableNrf9151ProgrammingPower() {
  auto& driver = common::GetDriver();
  auto* io_expander = driver.chip().xl9535.get();
  if (io_expander == nullptr) {
    return false;
  }

  constexpr auto kOutput = cpp_bus_driver::Xl95x5::Mode::kOutput;
  // 只开启编程电源，不初始化串口调制解调器。空白固件无法响应 AT 探测，
  // 调用 InitNrf9151() 会在探测失败后再次关闭电源。
  bool result = io_expander->GpioWrite(
      common::board::gpio::xl9535::kNrf9151En, 0);
  result &= io_expander->SetGpioMode(
      common::board::gpio::xl9535::kNrf9151En, kOutput);
  vTaskDelay(pdMS_TO_TICKS(10));
  result &= io_expander->GpioWrite(
      common::board::gpio::xl9535::kNrf9151En, 1);
  return result;
}
#endif

/**
 * @brief 准备用于固件下载的无线协处理器
 * @return 控制时序执行成功返回 true，否则返回 false
 */
bool PrepareCoprocessors() {
  auto& driver = common::GetDriver();

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  if (!EnableNrf9151ProgrammingPower()) {
    printf("Failed to enable nRF9151 programming power\n");
    return false;
  }
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
  printf("nRF9151 power is enabled for external SWD programming\n");
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
  } else if (!PrepareCoprocessors()) {
    printf("Coprocessor preparation failed\n");
  } else {
    printf("Coprocessor preparation completed\n");
  }

  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}
