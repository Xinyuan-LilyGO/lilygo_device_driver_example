/*
 * @Description: 根据当前硬件配置输出惯性传感器的偏航角、俯仰角与横滚角
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-22 17:03:59
 * @License: GPL 3.0
 */
#include <cstdio>

#include "common.h"
#include "imu.h"

extern "C" void app_main() {
  printf("IMU example on %s\n", common::kBoardName);

  if (!common::InitDriver()) {
    printf(
        "Device driver initialization completed with errors; continuing "
        "example\n");
  }

  RunImuExample();
}
