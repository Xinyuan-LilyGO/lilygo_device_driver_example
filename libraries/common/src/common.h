/*
 * @Description: Common board helpers for device driver examples
 * @Author: LILYGO_L
 * @Date: 2026-07-11 16:22:23
 * @LastEditTime: 2026-07-13 14:39:13
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

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
 * @brief 以同步模式初始化当前板卡设备驱动
 * @return 初始化成功返回 true，否则返回 false
 */
inline bool InitDriver() {
  return GetDriver().Init(DeviceDriver::InitMode::kSync);
}

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
 * @brief 判断屏幕及其背光驱动是否已经初始化
 * @return 屏幕可用返回 true，否则返回 false
 */
inline bool ScreenReady() {
  auto& driver = GetDriver();
  if (IsHi8561Screen()) {
    return driver.status().hi8561.init_flag &&
           driver.chip().hi8561 != nullptr &&
           driver.status().hi8561_backlight.init_flag &&
           driver.chip().hi8561_backlight != nullptr;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return IsRm69a10Screen() && driver.status().rm69a10.init_flag &&
         driver.chip().rm69a10 != nullptr;
#else
  return false;
#endif
}

/**
 * @brief 判断当前屏幕和触摸驱动是否已经初始化
 * @return 屏幕和触摸均可用返回 true，否则返回 false
 */
inline bool TouchReady() {
  auto& driver = GetDriver();
  if (IsHi8561Screen()) {
    return ScreenReady() && driver.status().hi8561_touch.init_flag &&
           driver.chip().hi8561_touch != nullptr;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return ScreenReady() && driver.status().gt9895.init_flag &&
         driver.chip().gt9895 != nullptr;
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
  if (IsHi8561Screen()) {
    return driver.chip().hi8561->SendColorStreamCoordinate(
        x_start, y_start, x_end, y_end, data);
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  if (IsRm69a10Screen()) {
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
  if (IsHi8561Screen()) {
    driver.chip().hi8561_backlight->StartGradientTime(100, 500);
    return;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  for (uint16_t brightness = 0; brightness < 255; brightness += 5) {
    driver.chip().rm69a10->SetBrightness(static_cast<uint8_t>(brightness));
    vTaskDelay(pdMS_TO_TICKS(10));
  }
#endif
}

/**
 * @brief 从指定触摸驱动读取一个触摸点
 * @tparam Touch 触摸驱动类型
 * @param touch 触摸驱动指针
 * @param x 触摸点 X 坐标输出引用
 * @param y 触摸点 Y 坐标输出引用
 * @return 读取到触摸点返回 true，否则返回 false
 */
template <typename Touch>
bool ReadSingleTouchFrom(Touch* touch, int& x, int& y) {
  typename Touch::TouchPoint point;
  if (!touch->GetSingleTouchPoint(point) || point.info.empty()) {
    return false;
  }
  x = point.info[0].x;
  y = point.info[0].y;
  return true;
}

/**
 * @brief 从当前屏幕对应的触摸驱动读取一个触摸点
 * @param x 触摸点 X 坐标输出引用
 * @param y 触摸点 Y 坐标输出引用
 * @return 读取到触摸点返回 true，否则返回 false
 */
inline bool ReadSingleTouch(int& x, int& y) {
  auto& driver = GetDriver();
  if (IsHi8561Screen()) {
    return ReadSingleTouchFrom(driver.chip().hi8561_touch.get(), x, y);
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return ReadSingleTouchFrom(driver.chip().gt9895.get(), x, y);
#else
  return false;
#endif
}

/**
 * @brief 读取并打印指定触摸驱动的多点触摸信息
 * @tparam Touch 触摸驱动类型
 * @param touch 触摸驱动指针
 */
template <typename Touch>
void PrintMultipleTouchFrom(Touch* touch) {
  typename Touch::TouchPoint point;
  if (!touch->GetMultipleTouchPoint(point)) {
    return;
  }
  printf("Touch finger: %u edge touch flag: %u\n",
      static_cast<unsigned int>(point.finger_count),
      static_cast<unsigned int>(point.edge_touch_flag));
  for (size_t i = 0; i < point.info.size(); ++i) {
    printf("Touch num:[%u] x: %u y: %u p: %u\n",
        static_cast<unsigned int>(i + 1),
        static_cast<unsigned int>(point.info[i].x),
        static_cast<unsigned int>(point.info[i].y),
        static_cast<unsigned int>(point.info[i].pressure_value));
  }
}

/**
 * @brief 读取并打印当前屏幕对应的多点触摸信息
 */
inline void PrintMultipleTouch() {
  auto& driver = GetDriver();
  if (IsHi8561Screen()) {
    PrintMultipleTouchFrom(driver.chip().hi8561_touch.get());
    return;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  PrintMultipleTouchFrom(driver.chip().gt9895.get());
#endif
}

/**
 * @brief 判断摄像头画面是否需要沿 Y 方向镜像
 * @return 需要沿 Y 方向镜像返回 true，否则返回 false
 */
inline bool CameraMirrorY() {
  return IsHi8561Screen() && board::device::screen::kRotationDirection == 0;
}

/**
 * @brief 运行当前屏幕支持的镜像、颜色、反色和休眠测试效果
 */
inline void RunScreenEffects() {
  auto& driver = GetDriver();
  if (IsHi8561Screen()) {
    auto* screen = driver.chip().hi8561.get();
    screen->SetMirror(cpp_bus_driver::Hi8561::MirrorMode::kHorizontal);
    vTaskDelay(pdMS_TO_TICKS(1000));
    screen->SetMirror(cpp_bus_driver::Hi8561::MirrorMode::kVertical);
    vTaskDelay(pdMS_TO_TICKS(1000));
    screen->SetMirror(cpp_bus_driver::Hi8561::MirrorMode::kHorizontalVertical);
    vTaskDelay(pdMS_TO_TICKS(1000));
    screen->SetMirror(cpp_bus_driver::Hi8561::MirrorMode::kOff);
    screen->SetColorOrder(cpp_bus_driver::Hi8561::ColorOrder::kBgr);
    vTaskDelay(pdMS_TO_TICKS(1000));
    screen->SetColorOrder(cpp_bus_driver::Hi8561::ColorOrder::kRgb);
    screen->SetInversion(true);
    vTaskDelay(pdMS_TO_TICKS(1000));
    screen->SetInversion(false);
    screen->SetScreenOff(true);
    screen->SetSleep(true);
    vTaskDelay(pdMS_TO_TICKS(1000));
    screen->SetSleep(false);
    screen->SetScreenOff(false);
    return;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  auto* screen = driver.chip().rm69a10.get();
  screen->SetInversion(true);
  vTaskDelay(pdMS_TO_TICKS(1000));
  screen->SetInversion(false);
  screen->SetScreenOff(true);
  screen->SetSleep(true);
  vTaskDelay(pdMS_TO_TICKS(1000));
  screen->SetSleep(false);
  screen->SetScreenOff(false);
#endif
}

}  // namespace common
