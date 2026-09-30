/*
 * @Description: BHI260AP 与 QMC6309/QMC6310N 九轴采样数据输出
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-30 11:32:30
 * @License: GPL 3.0
 */
#include <cstdint>
#include <cstdio>

#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    (defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) &&    \
        defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2))
#include "bhy2_parse.h"
#include "esp_timer.h"

namespace {

// 按常用传感器单位输出，仅作单位换算，不进行校准。
constexpr double kStandardGravity = 9.80665;
constexpr double kDegreesToRadians = 0.017453292519943295;

using bhi2xy_sensorapi_cpp_bus_driver::Bhi2xy;

constexpr float kAccelerometerScale = 1.0f / 4096.0f;
constexpr float kGyroscopeScale = 2000.0f / 32768.0f;
constexpr float kSampleRateHz = 100.0f;
constexpr uint32_t kReportLatencyMs = 0;
constexpr uint32_t kSampleIntervalMs = 10;
constexpr uint32_t kLogIntervalMs = 500;
constexpr uint32_t kErrorLogIntervalMs = 1000;
constexpr int64_t kMagneticDataTimeoutUs = 1000 * 1000;

Bhi2xy* g_bhi260ap = nullptr;
MagnetometerBase* g_magnetometer = nullptr;
float g_acceleration[3] = {};
float g_angular_velocity[3] = {};
float g_magnetic_field[3] = {};
bool g_acceleration_ready = false;
bool g_angular_velocity_ready = false;
bool g_magnetic_field_ready = false;
bool g_magnetic_field_overflow = false;

/**
 * @brief 解析加速度直通数据，仅换算为 g，保留传感器轴向
 * @param callback_info FIFO 数据及长度
 */
void ParseAcceleration(
    const struct bhy2_fifo_parse_data_info* callback_info, void*) {
  if (callback_info == nullptr || callback_info->data_ptr == nullptr ||
      callback_info->data_size < 6) {
    return;
  }

  struct bhy2_data_xyz data{};
  bhy2_parse_xyz(callback_info->data_ptr, &data);
  g_acceleration[0] = data.x * kAccelerometerScale;
  g_acceleration[1] = data.y * kAccelerometerScale;
  g_acceleration[2] = data.z * kAccelerometerScale;
  g_acceleration_ready = true;
}

/**
 * @brief 解析陀螺仪直通数据，仅换算为 deg/s，保留传感器轴向
 * @param callback_info FIFO 数据及长度
 */
void ParseAngularVelocity(
    const struct bhy2_fifo_parse_data_info* callback_info, void*) {
  if (callback_info == nullptr || callback_info->data_ptr == nullptr ||
      callback_info->data_size < 6) {
    return;
  }

  struct bhy2_data_xyz data{};
  bhy2_parse_xyz(callback_info->data_ptr, &data);
  g_angular_velocity[0] = data.x * kGyroscopeScale;
  g_angular_velocity[1] = data.y * kGyroscopeScale;
  g_angular_velocity[2] = data.z * kGyroscopeScale;
  g_angular_velocity_ready = true;
}

bool ConfigureBhi260ap() {
  auto& driver = common::GetDriver();
  if (!driver.IsBhi260apReady() || !driver.SetBhi260apSleep(false)) {
    const auto* bhi260ap = driver.chip().bhi260ap.get();
    printf("BHI260AP is not ready (error code: %d)\n",
        bhi260ap == nullptr ? BHY2_E_NULL_PTR : bhi260ap->last_error());
    return false;
  }

  g_bhi260ap = driver.chip().bhi260ap.get();
  if (!g_bhi260ap->RegisterFifoCallback(
          BHY2_SENSOR_ID_ACC_PASS, ParseAcceleration) ||
      !g_bhi260ap->RegisterFifoCallback(
          BHY2_SENSOR_ID_GYRO_PASS, ParseAngularVelocity) ||
      !g_bhi260ap->ProcessFifo() || !g_bhi260ap->UpdateVirtualSensorList() ||
      !g_bhi260ap->ConfigureSensor(
          BHY2_SENSOR_ID_ACC_PASS, kSampleRateHz, kReportLatencyMs) ||
      !g_bhi260ap->ConfigureSensor(
          BHY2_SENSOR_ID_GYRO_PASS, kSampleRateHz, kReportLatencyMs)) {
    printf("BHI260AP configuration failed (error code: %d)\n",
        g_bhi260ap->last_error());
    return false;
  }

  return true;
}

bool GetMagnetometer() {
  auto& driver = common::GetDriver();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  if (!driver.IsQmc6309Ready() || !driver.SetQmc6309Sleep(false)) {
    printf("QMC6309 is not ready\n");
    return false;
  }
  g_magnetometer = driver.chip().qmc6309.get();
#else
  if (!driver.IsQmc6310nReady() || !driver.SetQmc6310nSleep(false)) {
    printf("QMC6310N is not ready\n");
    return false;
  }

  g_magnetometer = driver.chip().qmc6310n.get();
#endif
  return g_magnetometer != nullptr;
}

}  // namespace

void RunImuExample() {
  if (!GetMagnetometer() || !ConfigureBhi260ap()) {
    auto& driver = common::GetDriver();
    driver.SetBhi260apSleep(true);
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
    driver.SetQmc6309Sleep(true);
#else
    driver.SetQmc6310nSleep(true);
#endif
    printf("IMU initialization failed\n");
    return;
  }

  int64_t last_magnetic_time_us = 0;
  int64_t last_log_time_us = esp_timer_get_time();
  int64_t last_error_log_time_us =
      last_log_time_us - static_cast<int64_t>(kErrorLogIntervalMs) * 1000;
  printf("IMU sensor output started (m/s^2, rad/s, uT; no calibration or fusion)\n");

  while (true) {
    if (!g_bhi260ap->ProcessFifo()) {
      const int64_t error_time_us = esp_timer_get_time();
      if (error_time_us - last_error_log_time_us >=
          static_cast<int64_t>(kErrorLogIntervalMs) * 1000) {
        printf("BHI260AP FIFO processing failed (error code: %d)\n",
            g_bhi260ap->last_error());
        last_error_log_time_us = error_time_us;
      }
    }

    // QMC6309 的 DRDY/OVFL 在读状态寄存器后清除，readData 内部已检查。
    MagnetometerData data{};
    const bool magnetic_data_read = g_magnetometer->readData(data);
    const int64_t now_us = esp_timer_get_time();
    if (data.overflow) {
      g_magnetic_field_ready = false;
      g_magnetic_field_overflow = true;
    } else if (magnetic_data_read) {
      // SensorLib 输出 Gauss，仅换算为 uT，不使用其航向结果。
      g_magnetic_field[0] = data.magnetic_field.x * 100.0f;
      g_magnetic_field[1] = data.magnetic_field.y * 100.0f;
      g_magnetic_field[2] = data.magnetic_field.z * 100.0f;
      g_magnetic_field_ready = true;
      g_magnetic_field_overflow = false;
      last_magnetic_time_us = now_us;
    }

    if (now_us - last_log_time_us >=
        static_cast<int64_t>(kLogIntervalMs) * 1000) {
      if (g_acceleration_ready) {
        printf("Accelerometer (m/s^2): X: %.6f, Y: %.6f, Z: %.6f\n",
            g_acceleration[0] * kStandardGravity,
            g_acceleration[1] * kStandardGravity,
            g_acceleration[2] * kStandardGravity);
      } else {
        printf("Accelerometer (m/s^2): X: --, Y: --, Z: -- (no new data)\n");
      }
      if (g_angular_velocity_ready) {
        printf("Gyroscope (rad/s): X: %.6f, Y: %.6f, Z: %.6f\n",
            g_angular_velocity[0] * kDegreesToRadians,
            g_angular_velocity[1] * kDegreesToRadians,
            g_angular_velocity[2] * kDegreesToRadians);
      } else {
        printf("Gyroscope (rad/s): X: --, Y: --, Z: -- (no new data)\n");
      }
      // 暂无新样本时保留最近一次有效磁场，超过一秒不再输出旧值。
      const bool magnetic_ready = g_magnetic_field_ready &&
          now_us - last_magnetic_time_us <= kMagneticDataTimeoutUs;
      if (magnetic_ready) {
        printf("Magnetometer (uT): X: %.6f, Y: %.6f, Z: %.6f\n",
            g_magnetic_field[0], g_magnetic_field[1], g_magnetic_field[2]);
      } else {
        printf("Magnetometer (uT): X: --, Y: --, Z: -- (%s)\n",
            g_magnetic_field_overflow ? "overflow"
                : last_magnetic_time_us != 0 ? "timeout" : "waiting for data");
      }
      PrintImuMagneticAngle(
          g_magnetic_field[0], g_magnetic_field[1], magnetic_ready);
      g_acceleration_ready = false;
      g_angular_velocity_ready = false;
      last_log_time_us = now_us;
    }

    vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
  }
}

#endif
