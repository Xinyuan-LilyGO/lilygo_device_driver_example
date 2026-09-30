/*
 * @Description: ICM20948 或 QMI8658 + QMC6309 九轴采样数据输出
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-30 11:32:41
 * @License: GPL 3.0
 */
#include <cstdint>
#include <cstdio>

#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
#include "esp_timer.h"

namespace {

// 按常用传感器单位输出，仅作单位换算，不进行校准。
constexpr double kStandardGravity = 9.80665;
constexpr double kDegreesToRadians = 0.017453292519943295;

using lilygo_device_driver::t_display_p4::device::ImuType;

// 原始寄存器计数，不包含单位换算、校准或姿态计算。
struct ImuRawVector3 {
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
};

struct ImuRawData {
  ImuRawVector3 acceleration;
  ImuRawVector3 angular_velocity;
  ImuRawVector3 magnetic_field;
  // 当前配置对应的单位/LSB，供应用进行换算。
  float acceleration_g_per_lsb = 0.0f;
  float angular_velocity_dps_per_lsb = 0.0f;
  float magnetic_field_ut_per_lsb = 0.0f;
  bool data_ready = false;
  bool magnetometer_data_ready = false;
  bool magnetometer_overflow = false;
};

constexpr uint8_t kQmiStatus0 = 0x2E;
constexpr uint8_t kQmiAccelData = 0x35;
constexpr uint8_t kQmcStatus = 0x09;
constexpr uint8_t kQmcData = 0x01;

int16_t DecodeAxis(const uint8_t* bytes) {
  const uint16_t bits = static_cast<uint16_t>(bytes[0]) |
                        (static_cast<uint16_t>(bytes[1]) << 8);
  return static_cast<int16_t>(bits < 0x8000 ? static_cast<int32_t>(bits)
                                         : static_cast<int32_t>(bits) - 65536);
}

bool ReadImuRawData(lilygo_device_driver::TDisplayP4Driver& driver,
    ImuRawData& data) {
  const auto type = driver.imu_type();
  auto* icm20948 = driver.chip().icm20948.get();
  auto* qmi8658 = driver.chip().qmi8658.get();
  auto* qmc6309 = driver.chip().qmc6309.get();
  data = {};
  if (!driver.IsImuReady()) {
    return false;
  }
  if (type == ImuType::kIcm20948) {
    cpp_bus_driver::Icm20948::RawData raw;
    if (icm20948 == nullptr || !icm20948->ReadRawData(raw)) {
      return false;
    }
    data.acceleration = {raw.acceleration.x, raw.acceleration.y,
        raw.acceleration.z};
    data.angular_velocity = {raw.angular_velocity.x,
        raw.angular_velocity.y, raw.angular_velocity.z};
    data.magnetic_field = {raw.magnetic_field.x, raw.magnetic_field.y,
        raw.magnetic_field.z};
    // 与 InitIcm20948 的 ±2g / ±250 dps 配置对应；AK09916 为固定灵敏度。
    data.acceleration_g_per_lsb = 1.0f / 16384.0f;
    data.angular_velocity_dps_per_lsb = 1.0f / 131.0f;
    data.magnetic_field_ut_per_lsb = 0.15f;
    data.data_ready = true;
    data.magnetometer_overflow = raw.magnetometer_overflow;
    data.magnetometer_data_ready =
        raw.magnetometer_data_ready && !raw.magnetometer_overflow;
    return true;
  }

  if (type != ImuType::kQmi8658Qmc6309 || qmi8658 == nullptr ||
      qmc6309 == nullptr) {
    return false;
  }

  // SensorLib getDataReady() 会对 -1 直接做位运算；显式区分 I2C 错误。
  const int status = qmi8658->readReg(kQmiStatus0);
  if (status < 0) {
    return false;
  }
  if ((status & 0x03) != 0x03) {
    return true;  // 正常等待下次采样，不伪报读取故障。
  }
  ImuRawData sample;
  // SensorLib 0.4.1 的 getAccelRaw/getGyroRaw 只判断 != -1，
  // 底层实际可能返回 -200 等错误。仍使用 SensorLib 通信和量程系数，
  // 一次读取连续六轴寄存器，并检查所有负值错误。
  uint8_t raw[12] = {};
  if (qmi8658->readRegBuff(kQmiAccelData, raw, sizeof(raw)) < 0) {
    return false;
  }
  sample.acceleration =
      {DecodeAxis(raw), DecodeAxis(raw + 2), DecodeAxis(raw + 4)};
  sample.angular_velocity =
      {DecodeAxis(raw + 6), DecodeAxis(raw + 8), DecodeAxis(raw + 10)};
  sample.acceleration_g_per_lsb = qmi8658->getAccelerometerScales();
  sample.angular_velocity_dps_per_lsb = qmi8658->getGyroscopeScales();
  // 与 InitQmc6309 的 FS_8G 配置对应：4000 LSB/G = 40 LSB/uT。
  sample.magnetic_field_ut_per_lsb = 0.025f;
  sample.data_ready = true;

  // 通过现有 SensorLib 的寄存器接口采集原始数据，避免 readData 内部
  // 执行单位转换和航向计算。状态只读一次，不能提前清除 DRDY/OVFL。
  const int magnetic_status = qmc6309->readReg(kQmcStatus);
  if (magnetic_status < 0) {
    return false;
  }
  sample.magnetometer_overflow = (magnetic_status & 0x02) != 0;
  if ((magnetic_status & 0x01) != 0 && !sample.magnetometer_overflow) {
    uint8_t magnetic[6] = {};
    if (qmc6309->readRegBuff(kQmcData, magnetic, sizeof(magnetic)) < 0) {
      return false;
    }
    sample.magnetic_field = {DecodeAxis(magnetic), DecodeAxis(magnetic + 2),
        DecodeAxis(magnetic + 4)};
    sample.magnetometer_data_ready = true;
  }
  data = sample;
  return true;
}


// 仅换算物理单位，保留传感器轴向，不进行校准或姿态融合。
struct ImuVector3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct ImuData {
  ImuVector3 acceleration_g;
  ImuVector3 angular_velocity_dps;
  ImuVector3 magnetic_field_ut;
  bool magnetometer_data_ready = false;
  bool magnetometer_overflow = false;
};

ImuData ConvertImuData(const ImuRawData& raw) {
  ImuData data;
  data.acceleration_g = {raw.acceleration.x * raw.acceleration_g_per_lsb,
      raw.acceleration.y * raw.acceleration_g_per_lsb,
      raw.acceleration.z * raw.acceleration_g_per_lsb};
  data.angular_velocity_dps = {
      raw.angular_velocity.x * raw.angular_velocity_dps_per_lsb,
      raw.angular_velocity.y * raw.angular_velocity_dps_per_lsb,
      raw.angular_velocity.z * raw.angular_velocity_dps_per_lsb};
  data.magnetic_field_ut = {raw.magnetic_field.x * raw.magnetic_field_ut_per_lsb,
      raw.magnetic_field.y * raw.magnetic_field_ut_per_lsb,
      raw.magnetic_field.z * raw.magnetic_field_ut_per_lsb};
  data.magnetometer_data_ready = raw.magnetometer_data_ready;
  data.magnetometer_overflow = raw.magnetometer_overflow;
  return data;
}

constexpr uint32_t kSampleIntervalMs = 10;      // 传感器轮询间隔。
constexpr uint32_t kLogIntervalMs = 500;        // 采样日志间隔。
constexpr int64_t kMagneticDataTimeoutUs = 1000 * 1000;
constexpr uint32_t kErrorLogIntervalMs = 1000;  // 错误日志限频间隔。

}  // namespace

void RunImuExample() {
  auto& driver = common::GetDriver();
  if (!driver.InitImu() || !driver.SetImuSleep(false)) {
    printf("IMU initialization failed\n");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(100));
  printf("IMU sensor output started (m/s^2, rad/s, uT; no calibration or fusion)\n");

  ImuData latest;
  bool motion_ready = false;
  int64_t last_magnetic_time_us = 0;
  int64_t last_log_time_us = esp_timer_get_time();
  int64_t last_error_log_time_us =
      last_log_time_us - static_cast<int64_t>(kErrorLogIntervalMs) * 1000;

  while (true) {
    ImuRawData raw;
    const bool read = ReadImuRawData(driver, raw);
    const int64_t now_us = esp_timer_get_time();
    if (!read) {
      if (now_us - last_error_log_time_us >=
          static_cast<int64_t>(kErrorLogIntervalMs) * 1000) {
        printf("IMU data read failed\n");
        last_error_log_time_us = now_us;
      }
    } else if (raw.data_ready) {
      const ImuData data = ConvertImuData(raw);
      latest.acceleration_g = data.acceleration_g;
      latest.angular_velocity_dps = data.angular_velocity_dps;
      motion_ready = true;
      if (data.magnetometer_overflow) {
        latest.magnetometer_data_ready = false;
        latest.magnetometer_overflow = true;
      } else if (data.magnetometer_data_ready) {
        latest.magnetic_field_ut = data.magnetic_field_ut;
        latest.magnetometer_data_ready = true;
        latest.magnetometer_overflow = false;
        last_magnetic_time_us = now_us;
      }
    }

    if (now_us - last_log_time_us >=
        static_cast<int64_t>(kLogIntervalMs) * 1000) {
      if (motion_ready) {
        printf("Accelerometer (m/s^2): X: %.6f, Y: %.6f, Z: %.6f\n",
            latest.acceleration_g.x * kStandardGravity,
            latest.acceleration_g.y * kStandardGravity,
            latest.acceleration_g.z * kStandardGravity);
        printf("Gyroscope (rad/s): X: %.6f, Y: %.6f, Z: %.6f\n",
            latest.angular_velocity_dps.x * kDegreesToRadians,
            latest.angular_velocity_dps.y * kDegreesToRadians,
            latest.angular_velocity_dps.z * kDegreesToRadians);
      } else {
        printf("Accelerometer (m/s^2): X: --, Y: --, Z: -- (no new data)\n");
        printf("Gyroscope (rad/s): X: --, Y: --, Z: -- (no new data)\n");
      }
      // 暂无新样本时保留最近一次有效磁场，超过一秒不再输出旧值。
      const bool magnetic_ready = latest.magnetometer_data_ready &&
          now_us - last_magnetic_time_us <= kMagneticDataTimeoutUs;
      if (magnetic_ready) {
        printf("Magnetometer (uT): X: %.6f, Y: %.6f, Z: %.6f\n",
            latest.magnetic_field_ut.x, latest.magnetic_field_ut.y,
            latest.magnetic_field_ut.z);
      } else {
        printf("Magnetometer (uT): X: --, Y: --, Z: -- (%s)\n",
            latest.magnetometer_overflow ? "overflow"
                : last_magnetic_time_us != 0 ? "timeout" : "waiting for data");
      }
      PrintImuMagneticAngle(
          latest.magnetic_field_ut.x, latest.magnetic_field_ut.y, magnetic_ready);
      motion_ready = false;
      last_log_time_us = now_us;
    }

    vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
  }
}

#endif
