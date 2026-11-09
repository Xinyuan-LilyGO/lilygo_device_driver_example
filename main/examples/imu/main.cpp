/*
 * @Description: 根据当前硬件配置输出加速度、角速度和磁场的三轴采样值
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-11-09 10:22:56
 */
#include <cstdio>

#include "common.h"
#include "imu.h"

extern "C" void app_main() {
  printf("IMU example on %s\n", common::kBoardName);

  if (!common::Init()) {
    printf(
        "Device driver initialization completed with errors; continuing "
        "example\n");
  }

  RunImuExample();
}
