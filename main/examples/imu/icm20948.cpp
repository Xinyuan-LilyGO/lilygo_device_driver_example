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

#include "esp_timer.h"

namespace {

using cpp_bus_driver::Icm20948;

// 姿态角，单位为度。
struct EulerAngles {
  float yaw = 0.0f;    // 偏航角，范围 0 至 360 度。
  float pitch = 0.0f;  // 俯仰角。
  float roll = 0.0f;   // 横滚角。
};

constexpr float kDegreesToRadians = 0.0174532925f;  // 度转弧度。
constexpr float kRadiansToDegrees = 57.2957795f;  // 弧度转度。
constexpr float kAccelerationMinimumSquared =
    0.0001f;  // 有效加速度向量模平方下限。
constexpr float kMagneticFieldMinimumSquared =
    0.0001f;  // 有效磁场向量模平方下限。
constexpr float kAccelCorrectionTimeConstantSeconds =
    0.5f;  // 加速度姿态校正时间常数。
constexpr float kMagCorrectionTimeConstantSeconds =
    0.5f;  // 磁场偏航校正时间常数。
constexpr uint32_t kSampleIntervalMs = 10;  // 传感器轮询间隔。
constexpr uint32_t kLogIntervalMs = 100;  // 姿态日志间隔。
constexpr uint32_t kErrorLogIntervalMs = 1000;  // 错误日志限频间隔。

/**
 * @brief 将角度归一化到 0 至 360 度
 * @param degrees 原始角度
 * @return 归一化后的角度
 */
float NormalizeDegrees(float degrees) {
  while (degrees < 0.0f) {
    degrees += 360.0f;
  }
  while (degrees >= 360.0f) {
    degrees -= 360.0f;
  }
  return degrees;
}

/**
 * @brief 计算从当前角度转到目标角度的最短有符号角度差
 * @param target_degrees 目标角度
 * @param current_degrees 当前角度
 * @return -180 至 180 度范围内的角度差
 */
float ShortestAngleDifference(
    float target_degrees, float current_degrees) {
  float difference =
      NormalizeDegrees(target_degrees) - NormalizeDegrees(current_degrees);
  if (difference > 180.0f) {
    difference -= 360.0f;
  } else if (difference < -180.0f) {
    difference += 360.0f;
  }
  return difference;
}

/**
 * @brief 使用加速度计算俯仰角和横滚角
 * @param acceleration_g 三轴加速度，单位 g
 * @param angles 返回俯仰角和横滚角
 * @return 加速度向量有效时返回 true，否则返回 false
 */
bool CalculateAccelAngles(
    const Icm20948::Vector3& acceleration_g, EulerAngles& angles) {
  const float magnitude_squared =
      acceleration_g.x * acceleration_g.x +
      acceleration_g.y * acceleration_g.y +
      acceleration_g.z * acceleration_g.z;
  if (magnitude_squared < kAccelerationMinimumSquared) {
    return false;
  }

  angles.pitch =
      std::atan2(-acceleration_g.x,
          std::sqrt(acceleration_g.y * acceleration_g.y +
                    acceleration_g.z * acceleration_g.z)) *
      kRadiansToDegrees;
  angles.roll =
      std::atan2(acceleration_g.y, acceleration_g.z) *
      kRadiansToDegrees;
  return true;
}

/**
 * @brief 使用倾斜补偿后的磁场计算偏航角
 * @param magnetic_field_ut 三轴磁场，单位 uT
 * @param pitch_degrees 当前俯仰角
 * @param roll_degrees 当前横滚角
 * @param yaw_degrees 返回偏航角
 * @return 磁场向量有效时返回 true，否则返回 false
 */
bool CalculateMagneticYaw(const Icm20948::Vector3& magnetic_field_ut,
    float pitch_degrees, float roll_degrees, float& yaw_degrees) {
  const float magnitude_squared =
      magnetic_field_ut.x * magnetic_field_ut.x +
      magnetic_field_ut.y * magnetic_field_ut.y +
      magnetic_field_ut.z * magnetic_field_ut.z;
  if (magnitude_squared < kMagneticFieldMinimumSquared) {
    return false;
  }

  const float pitch_radians = pitch_degrees * kDegreesToRadians;
  const float roll_radians = roll_degrees * kDegreesToRadians;
  const float magnetic_x_horizontal =
      magnetic_field_ut.x * std::cos(pitch_radians) +
      magnetic_field_ut.z * std::sin(pitch_radians);
  const float magnetic_y_horizontal =
      magnetic_field_ut.x * std::sin(roll_radians) *
          std::sin(pitch_radians) +
      magnetic_field_ut.y * std::cos(roll_radians) -
      magnetic_field_ut.z * std::sin(roll_radians) *
          std::cos(pitch_radians);

  yaw_degrees =
      NormalizeDegrees(std::atan2(
                           magnetic_y_horizontal, magnetic_x_horizontal) *
                       kRadiansToDegrees);
  return true;
}

/**
 * @brief 融合加速度、角速度和磁场得到连续姿态角
 *
 * 本估算器仅进行运行时互补融合，不计算或保存陀螺仪零偏、加速度偏置、
 * 磁力计硬铁偏置与软铁矩阵。
 */
class OrientationEstimator {
 public:
  /**
   * @brief 使用一组传感器数据更新姿态角
   * @param data ICM20948 换算后的传感器数据
   * @param delta_time_seconds 距离上次采样的时间，单位秒
   * @param angles 返回当前姿态角
   * @return 姿态角可用时返回 true，否则返回 false
   */
  bool Update(const Icm20948::SensorData& data,
      float delta_time_seconds, EulerAngles& angles) {
    EulerAngles accel_angles;
    if (!CalculateAccelAngles(data.acceleration_g, accel_angles)) {
      return false;
    }

    float magnetic_yaw = 0.0f;
    const bool magnetic_data_valid =
        !data.magnetometer_overflow &&
        CalculateMagneticYaw(data.magnetic_field_ut, accel_angles.pitch,
            accel_angles.roll, magnetic_yaw);

    if (!initialized_) {
      if (!magnetic_data_valid) {
        return false;
      }
      angles_ = accel_angles;
      angles_.yaw = magnetic_yaw;
      initialized_ = true;
      angles = angles_;
      return true;
    }

    if (delta_time_seconds <= 0.0f || delta_time_seconds > 1.0f) {
      return false;
    }

    const float accel_weight =
        delta_time_seconds /
        (kAccelCorrectionTimeConstantSeconds + delta_time_seconds);
    angles_.roll +=
        data.angular_velocity_dps.x * delta_time_seconds;
    angles_.pitch +=
        data.angular_velocity_dps.y * delta_time_seconds;
    angles_.yaw = NormalizeDegrees(
        angles_.yaw + data.angular_velocity_dps.z * delta_time_seconds);

    angles_.roll +=
        accel_weight * (accel_angles.roll - angles_.roll);
    angles_.pitch +=
        accel_weight * (accel_angles.pitch - angles_.pitch);

    if (magnetic_data_valid) {
      if (CalculateMagneticYaw(data.magnetic_field_ut, angles_.pitch,
              angles_.roll, magnetic_yaw)) {
        const float mag_weight =
            delta_time_seconds /
            (kMagCorrectionTimeConstantSeconds + delta_time_seconds);
        angles_.yaw = NormalizeDegrees(
            angles_.yaw +
            mag_weight *
                ShortestAngleDifference(magnetic_yaw, angles_.yaw));
      }
    }

    angles = angles_;
    return true;
  }

 private:
  // 当前融合姿态角。
  EulerAngles angles_;
  // 首组有效加速度和磁场数据完成初始化后置位。
  bool initialized_ = false;
};

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
  printf("IMU orientation output started without sensor calibration\n");

  OrientationEstimator estimator;
  int64_t last_sample_time_us = esp_timer_get_time();
  int64_t last_log_time_us = last_sample_time_us;
  int64_t last_error_log_time_us =
      last_sample_time_us -
      static_cast<int64_t>(kErrorLogIntervalMs) * 1000;

  while (true) {
    Icm20948::SensorData data;
    if (!icm20948->ReadData(data)) {
      const int64_t now_us = esp_timer_get_time();
      if (now_us - last_error_log_time_us >=
          static_cast<int64_t>(kErrorLogIntervalMs) * 1000) {
        printf("ICM20948 data read failed\n");
        last_error_log_time_us = now_us;
      }
      last_sample_time_us = now_us;
      vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
      continue;
    }

    const int64_t now_us = esp_timer_get_time();
    const float delta_time_seconds =
        (now_us - last_sample_time_us) / 1000000.0f;
    last_sample_time_us = now_us;

    EulerAngles angles;
    if (estimator.Update(data, delta_time_seconds, angles) &&
        now_us - last_log_time_us >=
            static_cast<int64_t>(kLogIntervalMs) * 1000) {
      printf("Yaw: %7.2f deg, Pitch: %7.2f deg, Roll: %7.2f deg\n",
          angles.yaw, angles.pitch, angles.roll);
      last_log_time_us = now_us;
    }

    vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
  }
}

#endif
