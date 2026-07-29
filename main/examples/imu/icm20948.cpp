/*
 * @Description: ICM20948 姿态角读取实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "imu.h"

#include <cmath>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace {

constexpr float kRadiansToDegrees = 57.2957795f;
constexpr uint32_t kLogIntervalMs = 100;

float NormalizeDegrees(float degrees) {
  while (degrees < 0.0f) {
    degrees += 360.0f;
  }
  while (degrees >= 360.0f) {
    degrees -= 360.0f;
  }
  return degrees;
}

}  // namespace

void RunIcm20948ImuExample() {
  auto& driver = common::GetDriver();
  auto& icm20948 = driver.chip().icm20948;
  if (icm20948 == nullptr || !driver.IsIcm20948Ready() ||
      !driver.SetIcm20948Sleep(false)) {
    printf("ICM20948 initialization failed\n");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(100));
  printf("IMU orientation output started\n");

  while (true) {
    icm20948->readSensor();

    const float pitch = icm20948->getPitch();
    const float roll = icm20948->getRoll();
    xyzFloat magnetic_field;
    icm20948->getMagValues(&magnetic_field);
    const float yaw = NormalizeDegrees(
        std::atan2(magnetic_field.y, magnetic_field.x) * kRadiansToDegrees);

    printf("Yaw: %7.2f deg, Pitch: %7.2f deg, Roll: %7.2f deg\n",
        yaw, pitch, roll);
    vTaskDelay(pdMS_TO_TICKS(kLogIntervalMs));
  }
}

#endif
