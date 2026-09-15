#pragma once

#include "sdkconfig.h"

#if !defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) || \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
#error "USB host examples require T-Display-P4 V2.0"
#endif

namespace common::usb_host {

/**
 * @brief 初始化 V2.0 板卡并开启 Type-A 接口的 5 V 主机供电。
 * @return 成功返回 true；失败时尝试关闭主机供电并返回 false。
 */
bool InitPower();

/**
 * @brief USB 主机停止后，关闭 Type-A 输出、USB 电源和升压。
 * @return 所有已初始化的电源控制均关闭成功返回 true，否则返回 false。
 */
bool DisablePower();

/**
 * @brief 初始化 BOOT 按键监测任务，按下后请求安全卸载 U 盘。
 * @return 按键配置和任务创建成功返回 true，否则返回 false。
 */
bool InitMscTestControl();

/**
 * @brief 获取并清除安全卸载请求，由 MSC 任务在当前轮读写完成后调用。
 * @return 有待处理的卸载请求返回 true，否则返回 false。
 */
bool TakeMscEjectRequest();

}  // namespace common::usb_host
