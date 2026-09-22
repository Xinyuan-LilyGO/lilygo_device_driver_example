/*
 * @Description: T-Display-P4 v2.0 应用公共 AXP517 PD 轮询任务
 * @Author: LILYGO_L
 * @Date: 2026-09-22 14:14:36
 * @LastEditTime: 2026-09-22 15:11:24
 * @License: GPL 3.0
 */
#include "battery/axp517_pd_service.h"

#include "common.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace common {
namespace {

constexpr uint32_t kStopTimeoutMs = 5000;

struct PdService {
  std::unique_ptr<cpp_bus_driver::Axp517Sink> sink;
  SemaphoreHandle_t mutex = nullptr;
  SemaphoreHandle_t stopped = nullptr;
  TaskHandle_t task = nullptr;
  uint16_t internal_charge_current_ma = 0;
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> stop_result{true};
  std::atomic<bool> restart_requested{false};
  uint16_t external_charge_current_ma = 1024;
  Axp517PdSnapshot snapshot;
};

PdService& GetPdService() {
  // 服务由 Start/Stop 显式管理，不依赖静态析构顺序。
  static PdService* const service = new PdService;
  return *service;
}

void RunPdService(void* argument) {
  auto& service = *static_cast<PdService*>(argument);
  auto& driver = GetDriver();
  while (!service.stop_requested.load()) {
    const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    if (service.restart_requested.exchange(false)) {
      service.sink->Restart(now_ms);
    }
    const bool external = driver.IsExternalBatterySelected();
    const bool success = service.sink->Poll(now_ms, true,
        external ? service.external_charge_current_ma
                 : service.internal_charge_current_ma);
    xSemaphoreTake(service.mutex, portMAX_DELAY);
    service.snapshot.status = service.sink->status();
    service.snapshot.external_battery_selected = external;
    service.snapshot.service_running = true;
    xSemaphoreGive(service.mutex);
    const auto state = service.sink->status().state;
    const uint32_t delay_ms =
        !success                                                ? 100
        : state == cpp_bus_driver::Axp517Sink::State::kReady    ? 10
        : state == cpp_bus_driver::Axp517Sink::State::kDisabled ? 20
                                                                : 2;
    const TickType_t delay = pdMS_TO_TICKS(delay_ms);
    vTaskDelay(delay > 0 ? delay : 1);
  }

  // 应用层停止轮询时撤销高压合同，再允许板级驱动释放 AXP517。
  const bool stopped =
      service.sink->Poll(static_cast<uint64_t>(esp_timer_get_time() / 1000),
          false, service.internal_charge_current_ma);
  service.stop_result.store(stopped);
  xSemaphoreTake(service.mutex, portMAX_DELAY);
  service.snapshot.status = service.sink->status();
  service.snapshot.service_running = false;
  xSemaphoreGive(service.mutex);
  xSemaphoreGive(service.stopped);
  vTaskDelete(nullptr);
}

}  // namespace

bool InitDriver(uint16_t external_charge_current_ma) {
  const bool result = GetDriver().Init(DeviceDriver::InitMode::kSync);
  if (GetDriver().IsAxp517Ready() &&
      !StartAxp517PdService(external_charge_current_ma)) {
    printf(
        "AXP517 PD application task unavailable; charging stays "
        "conservative\n");
  }
  return result;
}

bool StartAxp517PdService(uint16_t external_charge_current_ma) {
  auto& service = GetPdService();
  if (external_charge_current_ma < 64 || external_charge_current_ma > 5120 ||
      external_charge_current_ma % 64 != 0) {
    return false;
  }
  if (service.task != nullptr) {
    return service.external_charge_current_ma == external_charge_current_ma;
  }
  auto& driver = GetDriver();
  if (!driver.IsAxp517Ready() || driver.chip().axp517 == nullptr) {
    return false;
  }
  const auto* default_pd_config = driver.battery_info().default_pd_config;
  if (default_pd_config == nullptr) {
    return false;
  }
  if (service.mutex == nullptr) {
    service.mutex = xSemaphoreCreateMutex();
  }
  if (service.stopped == nullptr) {
    service.stopped = xSemaphoreCreateBinary();
  }
  if (service.mutex == nullptr || service.stopped == nullptr) {
    return false;
  }

  auto config = *default_pd_config;
  service.internal_charge_current_ma = config.contract_charge_current_ma;
  service.external_charge_current_ma = external_charge_current_ma;
  // 电池充电电流跟随所选电池，与是否建立 PD 合同无关。
  // Sink 会再与 Poll 传入的当前电池电流取最小值；USB 输入限流保持板级策略。
  config.fallback_charge_current_ma = std::max(
      service.internal_charge_current_ma, service.external_charge_current_ma);
  service.sink = std::make_unique<cpp_bus_driver::Axp517Sink>(
      *driver.chip().axp517, config);
  xSemaphoreTake(service.mutex, portMAX_DELAY);
  service.snapshot = {};
  xSemaphoreGive(service.mutex);
  service.stop_requested.store(false);
  service.restart_requested.store(false);
  if (xTaskCreate(RunPdService, "axp517_sink", 6144, &service,
          tskIDLE_PRIORITY + 3, &service.task) != pdPASS) {
    service.sink.reset();
    service.task = nullptr;
    return false;
  }
  return true;
}

bool StopAxp517PdService() {
  auto& service = GetPdService();
  if (service.task == nullptr) {
    return true;
  }
  service.stop_requested.store(true);
  service.restart_requested.store(false);
  if (xSemaphoreTake(service.stopped, pdMS_TO_TICKS(kStopTimeoutMs)) !=
      pdTRUE) {
    return false;
  }
  service.task = nullptr;
  service.sink.reset();
  return service.stop_result.load();
}

bool RequestAxp517PdRestart() {
  auto& service = GetPdService();
  if (service.mutex == nullptr || service.stop_requested.load()) {
    return false;
  }
  xSemaphoreTake(service.mutex, portMAX_DELAY);
  const bool accepted = service.snapshot.service_running &&
                        service.snapshot.status.state ==
                            cpp_bus_driver::Axp517Sink::State::kError;
  if (accepted) {
    service.restart_requested.store(true);
  }
  xSemaphoreGive(service.mutex);
  return accepted;
}

bool GetAxp517PdSnapshot(Axp517PdSnapshot& snapshot) {
  auto& service = GetPdService();
  if (service.mutex == nullptr) {
    return false;
  }
  xSemaphoreTake(service.mutex, portMAX_DELAY);
  snapshot = service.snapshot;
  xSemaphoreGive(service.mutex);
  return true;
}

}  // namespace common

#endif
