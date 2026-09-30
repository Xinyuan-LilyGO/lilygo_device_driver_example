/*
 * @Description: IMU 示例公共磁场角度计算与输出
 * @Author: LILYGO_L
 * @Date: 2026-09-30 00:00:00
 * @LastEditTime: 2026-09-30 00:00:00
 * @License: GPL 3.0
 */
#include <cmath>
#include <cstdio>

#include "imu.h"

/**
 * @brief 输出 XY 平面磁场角度，不进行校准、轴向转换或倾斜补偿
 * @param x X 轴磁场，单位 uT
 * @param y Y 轴磁场，单位 uT
 * @param ready 磁场样本是否有效且未超时
 */
void PrintImuMagneticAngle(float x, float y, bool ready) {
  if (!ready || !std::isfinite(x) || !std::isfinite(y) ||
      (std::fabs(x) < 0.0001f && std::fabs(y) < 0.0001f)) {
    printf("Magnetic angle (deg): --\n");
    return;
  }

  constexpr double kRadiansToDegrees = 57.29577951308232;
  double degrees = std::atan2(static_cast<double>(y), static_cast<double>(x)) *
                   kRadiansToDegrees;
  if (degrees < 0.0) {
    degrees += 360.0;
  }
  printf("Magnetic angle (deg): %.1f\n", degrees);
}
