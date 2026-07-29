/*
 * @Description: BHI260AP 与 QMC6310N 姿态角读取实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "imu.h"

#include <cmath>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

#include "bhy2_parse.h"

namespace {

using bhi2xy_sensorapi_cpp_bus_driver::Bhi2xy;

struct EulerAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
  float roll = 0.0f;
};

constexpr float kAccelerometerScale = 1.0f / 4096.0f;
constexpr float kDegreesToRadians = 0.0174532925f;
constexpr float kRadiansToDegrees = 57.2957795f;
constexpr float kSampleRateHz = 100.0f;
constexpr uint32_t kReportLatencyMs = 0;
constexpr uint32_t kLogIntervalMs = 500;

Bhi2xy* g_bhi260ap = nullptr;
SensorQMC6310* g_qmc6310n = nullptr;
float g_acceleration[3] = {0.0f, 0.0f, 0.0f};
float g_magnetic_field[3] = {0.0f, 0.0f, 0.0f};
bool g_acceleration_ready = false;
bool g_magnetic_field_ready = false;

float NormalizeDegrees(float degrees) {
  while (degrees < 0.0f) {
    degrees += 360.0f;
  }
  while (degrees >= 360.0f) {
    degrees -= 360.0f;
  }
  return degrees;
}

EulerAngles CalculateEulerAngles(
    const float acceleration[3], const float magnetic_field[3]) {
  EulerAngles angles;
  const float acceleration_z = -acceleration[2];

  angles.pitch =
      std::atan2(-acceleration[0],
          std::sqrt(acceleration[1] * acceleration[1] +
                    acceleration_z * acceleration_z)) *
      kRadiansToDegrees;
  angles.roll =
      std::atan2(acceleration[1], acceleration_z) * kRadiansToDegrees;

  const float pitch_radians = angles.pitch * kDegreesToRadians;
  const float roll_radians = angles.roll * kDegreesToRadians;
  const float magnetic_x_horizontal =
      magnetic_field[0] * std::cos(pitch_radians) +
      magnetic_field[2] * std::sin(pitch_radians);
  const float magnetic_y_horizontal =
      magnetic_field[0] * std::sin(roll_radians) *
          std::sin(pitch_radians) -
      magnetic_field[2] * std::sin(roll_radians) *
          std::cos(pitch_radians) +
      magnetic_field[1] * std::cos(roll_radians);

  angles.yaw =
      NormalizeDegrees(std::atan2(
                           magnetic_y_horizontal, magnetic_x_horizontal) *
                       kRadiansToDegrees);
  return angles;
}

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

bool ConfigureBhi260ap() {
  auto& driver = common::GetDriver();
  if (!driver.IsBhi260apReady()) {
    const auto* bhi260ap = driver.chip().bhi260ap.get();
    printf("BHI260AP is not ready (error code: %d)\n",
        bhi260ap == nullptr ? BHY2_E_NULL_PTR : bhi260ap->last_error());
    return false;
  }

  g_bhi260ap = driver.chip().bhi260ap.get();
  if (!g_bhi260ap->RegisterFifoCallback(
          BHY2_SENSOR_ID_ACC_PASS, ParseAcceleration) ||
      !g_bhi260ap->ProcessFifo() ||
      !g_bhi260ap->UpdateVirtualSensorList() ||
      !g_bhi260ap->ConfigureSensor(
          BHY2_SENSOR_ID_ACC_PASS, kSampleRateHz, kReportLatencyMs)) {
    printf("BHI260AP configuration failed (error code: %d)\n",
        g_bhi260ap->last_error());
    return false;
  }

  return true;
}

bool GetQmc6310n() {
  auto& driver = common::GetDriver();
  if (!driver.IsQmc6310nReady()) {
    printf("QMC6310N is not ready\n");
    return false;
  }

  g_qmc6310n = driver.chip().qmc6310n.get();
  return g_qmc6310n != nullptr;
}

}  // namespace

void RunBhi260apQmc6310nImuExample() {
  if (!ConfigureBhi260ap() || !GetQmc6310n()) {
    printf("IMU initialization failed\n");
    return;
  }

  TickType_t last_log_tick = xTaskGetTickCount();
  printf("IMU orientation output started\n");

  while (true) {
    if (!g_bhi260ap->ProcessFifo()) {
      printf("BHI260AP FIFO processing failed (error code: %d)\n",
          g_bhi260ap->last_error());
    }

    if (g_qmc6310n->isDataReady()) {
      MagnetometerData data;
      if (g_qmc6310n->readData(data)) {
        g_magnetic_field[0] = data.magnetic_field.x;
        g_magnetic_field[1] = data.magnetic_field.y;
        g_magnetic_field[2] = data.magnetic_field.z;
        g_magnetic_field_ready = true;
      }
    }

    const TickType_t now = xTaskGetTickCount();
    if (g_acceleration_ready && g_magnetic_field_ready &&
        now - last_log_tick >= pdMS_TO_TICKS(kLogIntervalMs)) {
      const EulerAngles angles =
          CalculateEulerAngles(g_acceleration, g_magnetic_field);
      printf("Yaw: %7.2f deg, Pitch: %7.2f deg, Roll: %7.2f deg\n",
          angles.yaw, angles.pitch, angles.roll);
      g_acceleration_ready = false;
      g_magnetic_field_ready = false;
      last_log_tick = now;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

#endif
