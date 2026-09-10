/*
 * @Description: Common board helpers for device driver examples
 * @Author: LILYGO_L
 * @Date: 2026-07-11 16:22:23
 * @LastEditTime: 2026-09-10 15:50:26
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdio>

#include "lilygo_device_driver.h"

namespace common {

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
// 当前构建使用的设备驱动类型
using DeviceDriver = lilygo_device_driver::TDisplayP4Driver;
// 当前构建使用的板级命名空间
namespace board = lilygo_device_driver::t_display_p4;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
// 当前构建使用的设备驱动类型
using DeviceDriver = lilygo_device_driver::TDisplayP4AirDriver;
// 当前构建使用的板级命名空间
namespace board = lilygo_device_driver::t_display_p4_air;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
// 当前构建使用的设备驱动类型
using DeviceDriver = lilygo_device_driver::TGlassesP4Driver;
// 当前构建使用的板级命名空间
namespace board = lilygo_device_driver::t_glasses_p4;
#else
#error "These examples support T-Display-P4, T-Display-P4-Air and T-Glasses-P4 only"
#endif

// 当前构建的设备型号名称
inline constexpr const char* kBoardName =
    board::device::kDeviceModelInfo.name;

/**
 * @brief 获取当前板卡的设备驱动单例
 * @return 设备驱动单例引用
 */
inline DeviceDriver& GetDriver() { return DeviceDriver::GetInstance(); }

/**
 * @brief 初始化当前板卡的最小设备驱动集合
 * @return 初始化成功返回 true，否则返回 false
 */
inline bool InitMinimalDriver() { return GetDriver().InitMinimal(); }

/**
 * @brief 以同步模式初始化当前板卡设备驱动
 * @return 初始化成功返回 true，否则返回 false
 */
inline bool InitDriver() {
  return GetDriver().Init(DeviceDriver::InitMode::kSync);
}

/**
 * @brief 设置当前板卡 Wi-Fi 协处理器的电源使能状态
 * @param enabled true 开启协处理器，false 关闭协处理器
 * @return 设置成功返回 true，否则返回 false
 */
inline bool SetWifiCoprocessorPowerEnabled(bool enabled) {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  return GetDriver().SetEsp32c6PowerEnabled(enabled);
#else
  return GetDriver().SetEsp32c5PowerEnabled(enabled);
#endif
}

/**
 * @brief 注册 ESP-Hosted Wi-Fi 协处理器复位回调
 * @return 注册成功返回 true，否则返回 false
 */
bool RegisterWifiCoprocessorResetCallback();

/**
 * @brief 获取 ESP32-P4 启动按键 GPIO
 * @return 启动按键 GPIO 编号
 */
inline constexpr int BootButtonGpio() {
  return board::gpio::button::kEsp32p4Boot;
}

/**
 * @brief 判断当前屏幕是否为 HI8561
 * @return 当前屏幕为 HI8561 返回 true，否则返回 false
 */
inline bool IsHi8561Screen() {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return GetDriver().screen_type() == board::device::ScreenType::kHi8561;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  return true;
#else
  return false;
#endif
}

/**
 * @brief 判断当前屏幕是否为 RM69A10
 * @return 当前屏幕为 RM69A10 返回 true，否则返回 false
 */
inline bool IsRm69a10Screen() {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return GetDriver().screen_type() == board::device::ScreenType::kRm69a10;
#else
  return false;
#endif
}

/**
 * @brief 向当前屏幕写入指定区域的像素数据
 * @param x_start 区域起始 X 坐标
 * @param y_start 区域起始 Y 坐标
 * @param x_end 区域结束 X 坐标
 * @param y_end 区域结束 Y 坐标
 * @param data 像素数据地址
 * @return 写入成功返回 true，否则返回 false
 */
inline bool SendScreen(
    int x_start, int y_start, int x_end, int y_end, const void* data) {
  auto& driver = GetDriver();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
  return driver.IsScreenReady() && driver.bus().screen_mipi_bus != nullptr &&
         driver.bus().screen_mipi_bus->Write(
             x_start, y_start, x_end, y_end, data);
#else
  if (IsHi8561Screen() && driver.IsHi8561Ready()) {
    return driver.chip().hi8561->SendColorStreamCoordinate(
        x_start, y_start, x_end, y_end, data);
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  if (IsRm69a10Screen() && driver.IsRm69a10Ready()) {
    return driver.chip().rm69a10->SendColorStreamCoordinate(
        x_start, y_start, x_end, y_end, data);
  }
#endif
  return false;
#endif
}

/**
 * @brief 唤醒当前屏幕并启动亮度渐变
 */
inline void StartBacklight() {
  auto& driver = GetDriver();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) || \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  if (IsHi8561Screen()) {
    if (!driver.IsHi8561Ready()) {
      printf("Screen is not ready\n");
      return;
    }
    auto* screen = driver.chip().hi8561.get();
    // 初始化后屏幕处于睡眠和关屏状态，开启背光前先恢复显示。
    if (!screen->SetSleep(false) || !screen->SetScreenOff(false)) {
      printf("Screen wake-up failed\n");
      return;
    }
  }
#endif
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  if (IsHi8561Screen() && driver.IsSy7200aReady()) {
    driver.chip().sy7200a->FadeTo(
        {.value = 1, .scale = 1}, 500,
        cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion);
    return;
  }
#else
  if (IsHi8561Screen() && driver.IsPt4103Ready()) {
    driver.chip().pt4103->FadeTo(
        {.value = 1, .scale = 1}, 500,
        cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion);
    return;
  }
#endif
  if (IsRm69a10Screen() && driver.IsRm69a10Ready()) {
    auto* screen = driver.chip().rm69a10.get();
    // 初始化后屏幕处于睡眠和关屏状态，调节亮度前先恢复显示。
    if (!screen->SetSleep(false) || !screen->SetScreenOff(false)) {
      printf("Screen wake-up failed\n");
      return;
    }
    for (uint16_t brightness = 0; brightness <= 255; brightness += 5) {
      if (!screen->SetBrightness(static_cast<uint8_t>(brightness))) {
        printf("Screen brightness update failed\n");
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  if (driver.IsSy7200aReady()) {
    driver.chip().sy7200a->FadeTo(
        {.value = 1, .scale = 1}, 500,
        cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion);
  }
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
  if (driver.IsS023msafjf10111e1Ready()) {
    // 逐步点亮屏幕至目标亮度增益。
    constexpr uint16_t kFadeSteps = 32;
    constexpr uint16_t kTargetBrightnessGain = 256;
    for (uint16_t step = 0; step <= kFadeSteps; ++step) {
      const uint16_t gain =
          kTargetBrightnessGain * step / kFadeSteps;
      if (!driver.chip().s023msafjf10111e1->SetBrightnessGain(gain)) {
        printf("Screen brightness gain update failed\n");
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(15));
    }
  }
#endif
}

}  // namespace common
