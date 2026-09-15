#include "host.h"

#include <atomic>

#include "common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lilygo_device_driver.h"

namespace common::usb_host {
namespace {

constexpr char kTag[] = "usb_host_power";

constexpr char kMscTag[] = "usb_msc_control";
std::atomic<bool> s_eject_requested{false};
TaskHandle_t s_button_task = nullptr;

/**
 * @brief 独立采样 BOOT 按键并消抖，避免文件读写期间漏掉按键。
 * @param arg 未使用的任务参数。
 */
void ButtonTask(void* arg) {
  (void)arg;
  cpp_bus_driver::PlatformHal platform_hal;
  bool previous_pressed = platform_hal.GpioRead(BootButtonGpio()) == 0;
  bool stable_pressed = previous_pressed;
  // 上电时已经按住的 BOOT 不触发卸载，必须先松开再按下。
  bool armed = !previous_pressed;
  TickType_t changed_at = xTaskGetTickCount();

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10));
    const bool pressed = platform_hal.GpioRead(BootButtonGpio()) == 0;
    const TickType_t now = xTaskGetTickCount();
    if (pressed != previous_pressed) {
      previous_pressed = pressed;
      changed_at = now;
    }
    if (pressed == stable_pressed ||
        now - changed_at < pdMS_TO_TICKS(30)) {
      continue;
    }
    stable_pressed = pressed;
    if (!pressed) {
      armed = true;
    } else if (armed) {
      armed = false;
      s_eject_requested.store(true);
      ESP_LOGI(kMscTag, "BOOT: MSC eject requested; waiting for current round");
    }
  }
}

}  // namespace

bool DisablePower() {
  auto& driver = lilygo_device_driver::TDisplayP4Driver::GetInstance();
  const bool result = driver.SetUsbHostPowerEnabled(false);
  if (!result) {
    ESP_LOGE(kTag, "Failed to disable USB host power");
  }
  return result;
}

bool InitPower() {
  auto& driver = lilygo_device_driver::TDisplayP4Driver::GetInstance();
  if (!driver.InitMinimal()) {
    ESP_LOGW(kTag, "Minimal device driver initialization completed with errors; continuing USB host power setup");
  }

  if (!driver.InitUsbHostPower() || !driver.SetUsbHostPowerEnabled(true)) {
    DisablePower();
    return false;
  }
  ESP_LOGI(kTag, "T-Display-P4 V2.0: Type-A 5 V enabled, Type-C remains sink");
  return true;
}

bool InitMscTestControl() {
  if (s_button_task != nullptr) {
    return true;
  }
  cpp_bus_driver::PlatformHal platform_hal;
  if (!platform_hal.SetGpioMode(BootButtonGpio(),
          cpp_bus_driver::PlatformHal::GpioMode::kInput,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup)) {
    return false;
  }
  s_eject_requested.store(false);
  return xTaskCreate(ButtonTask, "msc_boot", 3072, nullptr, 4,
             &s_button_task) == pdPASS;
}

bool TakeMscEjectRequest() {
  return s_eject_requested.exchange(false);
}

}  // namespace common::usb_host
