/*
 * @Description: 准备无线协处理器固件下载所需的电源和控制信号
 * @Author: LILYGO_L
 * @Date: 2026-07-30 14:57:16
 * @LastEditTime: 2026-07-30 15:10:29
 * @License: GPL 3.0
 */
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "common.h"

namespace {

/**
 * @brief 初始化协处理器电源和下载模式所需的最小板级硬件
 * @return 初始化成功返回 true，否则返回 false
 */
bool InitCoprocessorControlHardware() {
  return common::GetDriver().InitMinimal();
}

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
/**
 * @brief 切换外部串口连接目标并输出切换日志
 * @param target 目标处理器
 * @return 切换成功返回 true，否则返回 false
 */
bool SwitchUartTarget(common::DeviceDriver::UartTarget target) {
  const char* name = target == common::DeviceDriver::UartTarget::kEsp32c5
                         ? "ESP32-C5"
                         : "ESP32-P4";
  printf("UART switching to %s\n", name);
  // 切离 P4 前刷新日志，并留出串口发送时间。
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!common::GetDriver().SetUartTarget(target)) {
    printf("UART switch to %s failed\n", name);
    return false;
  }
  printf("UART connected to %s\n", name);
  return true;
}

/**
 * @brief 通过 BOOT 按键切换外部串口连接目标
 * @note 按下消抖后切换一次，长按不重复触发。
 */
void RunBootUartSwitch() {
  cpp_bus_driver::PlatformHal platform_hal;
  if (!platform_hal.SetGpioMode(common::BootButtonGpio(),
          cpp_bus_driver::PlatformHal::GpioMode::kInput,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    return;
  }

  using UartTarget = common::DeviceDriver::UartTarget;
  UartTarget target = UartTarget::kEsp32c5;
  // 启动时已按住的按键需要先释放。
  bool sampled = !platform_hal.GpioRead(common::BootButtonGpio());
  bool stable = sampled;
  int64_t changed_ms = platform_hal.GetSystemTimeMs();
  while (true) {
    const bool pressed = !platform_hal.GpioRead(common::BootButtonGpio());
    const int64_t now_ms = platform_hal.GetSystemTimeMs();
    if (pressed != sampled) {
      sampled = pressed;
      changed_ms = now_ms;
    }
    if (sampled != stable && now_ms - changed_ms >= 30) {
      stable = sampled;
      if (stable) {
        const auto next = target == UartTarget::kEsp32c5
                              ? UartTarget::kEsp32p4
                              : UartTarget::kEsp32c5;
        if (SwitchUartTarget(next)) {
          target = next;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

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
  if (!driver.EnterEsp32c5DownloadMode()) {
    printf("Failed to place ESP32-C5 into download mode\n");
    return false;
  }

  printf("ESP32-C5 entered download mode\n");
  printf("nRF9151 power is enabled for external SWD programming\n");
  printf("BOOT GPIO %d toggles the UART between ESP32-P4 and ESP32-C5\n",
      common::BootButtonGpio());
  return SwitchUartTarget(common::DeviceDriver::UartTarget::kEsp32c5);
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  if (!driver.EnterEsp32c5DownloadMode()) {
    printf("Failed to place ESP32-C5 into download mode\n");
    return false;
  }

  printf("ESP32-C5 download-mode control sequence completed\n");
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
  printf("Coprocessor download-mode helper on %s %s\n", common::kBoardName,
      common::GetDriver().device_model_info().version);

  if (!InitCoprocessorControlHardware()) {
    printf("Coprocessor control hardware initialization failed\n");
  } else if (!PrepareCoprocessors()) {
    printf("Coprocessor preparation failed\n");
  } else {
    printf("Coprocessor preparation completed\n");
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
    RunBootUartSwitch();
#endif
  }

  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}
