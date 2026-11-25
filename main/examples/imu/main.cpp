/*
 * @Description: 根据当前硬件配置输出加速度、角速度和磁场的三轴采样值
 */
#include <cstdio>

#include "common.h"
#include "imu.h"

extern "C" void app_main() {
  printf("IMU example on %s\n", common::kBoardName);

#if !defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
  if (!common::Init()) {
    printf(
        "Device driver initialization completed with errors; continuing "
        "example\n");
  }
#endif

  RunImuExample();
}
