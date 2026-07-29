/*
 * @Description: 根据当前硬件配置运行对应的 GPS/GNSS 定位示例
 * @Author: LILYGO_L
 * @Date: 2026-07-29 00:22:40
 * @LastEditTime: 2026-07-29 00:22:40
 * @License: GPL 3.0
 */
#include "common.h"
#include "gps.h"

extern "C" void app_main(void) {
  printf("GPS/GNSS example on %s\n", common::kBoardName);

  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  gps::RunL76k();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  gps::RunNrf9151();
#endif
}
