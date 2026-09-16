/*
 * @Description: 根据当前硬件配置输出惯性传感器的偏航角、俯仰角与横滚角
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "imu.h"

extern "C" void app_main(void) {
  printf("IMU example on %s\n", common::kBoardName);

  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors; continuing example\n");
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  RunIcm20948ImuExample();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    (defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
        defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2))
  RunBhi260apQmcImuExample();
#endif
}
