/*
 * @Description: T-Glasses-P4 的 BHI260AP 与 BMM350 九轴采样输出
 */
#include "common.h"
#include "imu.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
#include <cstdint>
#include <cstdio>

#include "bhy2_parse.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

using bhi2xy_sensorapi_cpp_bus_driver::Bhi2xy;

constexpr float kStandardGravity = 9.80665f;
constexpr float kDegreesToRadians = 0.01745329252f;
constexpr uint32_t kFifoPollIntervalMs = 10;
constexpr int64_t kLogIntervalUs = 500 * 1000;
constexpr int64_t kSampleTimeoutUs = 1000 * 1000;
constexpr uint8_t kVectorEventSize = 7;

struct ImuStream {
  const char* name;
  const char* unit;
  uint8_t virtual_id;
  uint8_t physical_id;
  float rate_hz;
  float unit_factor;
  bhy2_data_xyz sample{};
  int64_t last_sample_us = 0;
  float scale = 0;
};

void ParseSample(
    const bhy2_fifo_parse_data_info* callback_info, void* callback_reference) {
  if (callback_info->data_size != kVectorEventSize) {
    return;
  }
  auto& stream = *static_cast<ImuStream*>(callback_reference);
  bhy2_parse_xyz(callback_info->data_ptr, &stream.sample);
  stream.last_sample_us = esp_timer_get_time();
}

bool ConfigureStream(Bhi2xy& bhi260ap, ImuStream& stream) {
  auto* context = bhi260ap.context();
  if (context->event_size[stream.virtual_id] != kVectorEventSize) {
    uint8_t firmware_error = 0;
    const int8_t result = bhy2_get_error_value(&firmware_error, context);
    printf(
        "%s virtual sensor %u missing or incompatible (event size: %u, "
        "firmware error: 0x%02X, error register read: %d)\n",
        stream.name, static_cast<unsigned int>(stream.virtual_id),
        static_cast<unsigned int>(context->event_size[stream.virtual_id]),
        static_cast<unsigned int>(firmware_error), result);
    return false;
  }
  if (!bhi260ap.RegisterFifoCallback(stream.virtual_id, ParseSample, &stream) ||
      !bhi260ap.ConfigureSensor(stream.virtual_id, stream.rate_hz, 0)) {
    printf("%s stream configuration failed (API: %d)\n", stream.name,
        bhi260ap.last_error());
    return false;
  }

  bhy2_sensor_info virtual_info{};
  bhy2_phys_sensor_info physical_info{};
  const int8_t virtual_result =
      bhy2_get_sensor_info(stream.virtual_id, &virtual_info, context);
  const int8_t physical_result =
      bhy2_get_phys_sensor_info(stream.physical_id, &physical_info, context);
  const auto resolution = virtual_info.resolution.u16_val;
  const auto range = physical_info.curr_range.u16_val;
  if (virtual_result == BHY2_OK && physical_result == BHY2_OK && range > 0 &&
      resolution > 1 && resolution <= 16) {
    stream.scale = stream.unit_factor * range / (1U << (resolution - 1));
  } else {
    printf(
        "%s scale unavailable (API: %d/%d, range: %u, resolution: %u); "
        "reporting counts\n",
        stream.name, virtual_result, physical_result,
        static_cast<unsigned int>(range),
        static_cast<unsigned int>(resolution));
  }
  if (stream.physical_id == BHY2_PHYS_SENSOR_ID_MAGNETOMETER &&
      physical_result == BHY2_OK && physical_info.driver_id != 0) {
    printf(
        "BMM350 firmware sensor info: driver 0x%02X, aux address 0x%02X, "
        "GPIO %u, interface flags 0x%02X\n",
        static_cast<unsigned int>(physical_info.driver_id),
        static_cast<unsigned int>(physical_info.slave_address),
        static_cast<unsigned int>(physical_info.gpio_assignment),
        static_cast<unsigned int>(physical_info.flags));
  }
  return true;
}

bool PrintSample(const ImuStream& stream, int64_t now_us) {
  if (stream.last_sample_us == 0 ||
      now_us - stream.last_sample_us > kSampleTimeoutUs) {
    printf("%s: X: --, Y: --, Z: -- (%s)\n", stream.name,
        stream.last_sample_us == 0 ? "waiting for FIFO data" : "timeout");
    return false;
  }
  if (stream.scale == 0) {
    printf("%s (counts): X: %d, Y: %d, Z: %d\n", stream.name, stream.sample.x,
        stream.sample.y, stream.sample.z);
    return false;
  }
  printf("%s (%s): X: %.6f, Y: %.6f, Z: %.6f\n", stream.name, stream.unit,
      stream.sample.x * stream.scale, stream.sample.y * stream.scale,
      stream.sample.z * stream.scale);
  return true;
}

}  // namespace

void RunImuExample() {
  auto& driver = common::GetDriver();
  if (!driver.InitMinimal()) {
    return;
  }
  // 摄像头断电时可能拉低共享 SENSOR I2C；不初始化显示控制器。
  driver.InitSgm38121();
  if (!driver.InitBhi260ap()) {
    return;
  }
  auto& bhi260ap = *driver.chip().bhi260ap;
  if (!bhi260ap.ProcessFifo() || !bhi260ap.UpdateVirtualSensorList()) {
    printf("BHI260AP virtual sensor discovery failed (API: %d)\n",
        bhi260ap.last_error());
    driver.DeinitBhi260ap();
    return;
  }

  ImuStream acceleration{"Accelerometer", "m/s^2", BHY2_SENSOR_ID_ACC_RAW,
      BHY2_PHYS_SENSOR_ID_ACCELEROMETER, 100.0f, kStandardGravity};
  ImuStream angular_velocity{"Gyroscope", "rad/s", BHY2_SENSOR_ID_GYRO_RAW,
      BHY2_PHYS_SENSOR_ID_GYROSCOPE, 100.0f, kDegreesToRadians};
  ImuStream magnetic_field{"Magnetometer (BMM350)", "uT",
      BHY2_SENSOR_ID_MAG_RAW, BHY2_PHYS_SENSOR_ID_MAGNETOMETER, 25.0f, 1.0f};
  if (!ConfigureStream(bhi260ap, acceleration) ||
      !ConfigureStream(bhi260ap, angular_velocity)) {
    driver.DeinitBhi260ap();
    return;
  }
  const bool magnetic_enabled = ConfigureStream(bhi260ap, magnetic_field);
  printf("BHI260AP FIFO sampling started (uncalibrated, no sensor fusion)\n");

  int64_t last_log_us = esp_timer_get_time();
  while (true) {
    const bool fifo_ok = bhi260ap.ProcessFifo();
    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_log_us >= kLogIntervalUs) {
      if (!fifo_ok) {
        printf("BHI260AP FIFO processing failed (API: %d)\n",
            bhi260ap.last_error());
      }
      PrintSample(acceleration, now_us);
      PrintSample(angular_velocity, now_us);
      const bool magnetic_ready =
          magnetic_enabled && PrintSample(magnetic_field, now_us);
      PrintImuMagneticAngle(magnetic_field.sample.x * magnetic_field.scale,
          magnetic_field.sample.y * magnetic_field.scale, magnetic_ready);
      last_log_us = now_us;
    }
    vTaskDelay(pdMS_TO_TICKS(kFifoPollIntervalMs));
  }
}

#endif
