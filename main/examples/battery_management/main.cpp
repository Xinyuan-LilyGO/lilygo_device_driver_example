/*
 * @Description: 根据当前硬件配置运行对应的电池管理示例
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "battery_management.h"
#include "common.h"

extern "C" void app_main(void) {
  printf("Battery management example on %s\n", common::kBoardName);

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  RunBq27220Example();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  RunAxp517Example();
#endif
}
