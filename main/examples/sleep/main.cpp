/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-07-10 11:03:22
 * @LastEditTime: 2026-07-10 14:06:11
 * @License: GPL 3.0
 */
#include "common.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_sleep.h"

namespace {

constexpr uint64_t kTimerWakeupUs = 10ULL * 1000ULL * 1000ULL;

const char* WakeupReasonName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
      return "button";
    default:
      return "power-on or other";
  }
}

void EnableWakeupSources() {
  const auto button = static_cast<gpio_num_t>(common::BootButtonGpio());
  const gpio_config_t config = {
      .pin_bit_mask = 1ULL << static_cast<int>(button),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
      .hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE,
#endif
  };
  ESP_ERROR_CHECK(gpio_config(&config));
  ESP_ERROR_CHECK(gpio_wakeup_enable(button, GPIO_INTR_LOW_LEVEL));
  ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
  ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(kTimerWakeupUs));
}

}  // namespace

extern "C" void app_main(void) {
  printf("Sleep example on %s; wake reason: %s\n", common::kBoardName,
      WakeupReasonName(esp_sleep_get_wakeup_cause()));

  auto& driver = common::GetDriver();
  common::InitDriver();
  printf("Entering deep sleep in 5 seconds\n");
  vTaskDelay(pdMS_TO_TICKS(5000));

  if (!driver.SetSleep(common::DeviceDriver::SleepLevel::kDeep, true)) {
    printf("Device sleep preparation reported a failure\n");
  }
  EnableWakeupSources();
  uart_wait_tx_idle_polling(
      static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM));
  esp_deep_sleep_start();
}
