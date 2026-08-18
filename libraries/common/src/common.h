/*
 * @Description: Common board helpers for device driver examples
 * @Author: LILYGO_L
 * @Date: 2026-07-11 16:22:23
 * @LastEditTime: 2026-07-13 14:39:13
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "lilygo_device_driver_library.h"

namespace common {

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
// 当前构建使用的设备驱动类型
using DeviceDriver = lilygo_device_driver::TDisplayP4Driver;
// 当前构建使用的板级命名空间
namespace board = lilygo_device_driver::t_display_p4;
// 当前构建的板卡名称
inline constexpr const char* kBoardName = "T-Display-P4";
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
// 当前构建使用的设备驱动类型
using DeviceDriver = lilygo_device_driver::TDisplayP4AirDriver;
// 当前构建使用的板级命名空间
namespace board = lilygo_device_driver::t_display_p4_air;
// 当前构建的板卡名称
inline constexpr const char* kBoardName = "T-Display-P4-Air";
#else
#error "These examples support T-Display-P4 and T-Display-P4-Air only"
#endif

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
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
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
#else
  return true;
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
}

/**
 * @brief 启动当前屏幕的渐变背光效果
 */
inline void StartBacklight() {
  auto& driver = GetDriver();
  if (IsHi8561Screen() && driver.IsHi8561BacklightReady()) {
    driver.chip().hi8561_backlight->FadeTo(
        {.value = 1, .scale = 1}, 500,
        cpp_bus_driver::Pwm::FadeMode::kWaitForCompletion);
    return;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  if (IsRm69a10Screen() && driver.IsRm69a10Ready()) {
    for (uint16_t brightness = 0; brightness < 255; brightness += 5) {
      driver.chip().rm69a10->SetBrightness(static_cast<uint8_t>(brightness));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
#endif
}

}  // namespace common
