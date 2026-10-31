/*
 * @Description: RX8130CE 外部实时时钟示例
 * @License: GPL 3.0
 */
#include "rtc.h"

#include <cstdio>
#include <ctime>
#include "common.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

void RunRx8130ceRtcExample() {
  auto& driver = common::GetDriver();
  if (!driver.IsRx8130ceReady() || driver.chip().rx8130ce == nullptr) {
    printf("RX8130CE initialization failed\n");
    return;
  }

  auto& rtc = *driver.chip().rx8130ce;
  cpp_bus_driver::Rx8130ce::Status status;
  const bool status_read = rtc.GetStatus(status);
  if (!status_read) {
    printf("RX8130CE status read failed\n");
  }
  if (status_read && status.voltage_loss) {
    printf("RX8130CE VLF is set; checking calendar data\n");
  }

  printf("RX8130CE external RTC started\n");
  while (true) {
    cpp_bus_driver::Rx8130ce::Time time;
    if (rtc.GetTime(time)) {
      printf("RTC time: %04d-%02d-%02d %02d:%02d:%02d, weekday: %d\n",
          time.year + 2000, time.month, time.day, time.hour, time.minute,
          time.second, static_cast<int>(time.weekday));
    } else {
      printf("RX8130CE calendar read failed\n");
    }

    if (WaitForRtcReset()) {
      const cpp_bus_driver::Rx8130ce::Time initial_time = {
          55, 59, 23, cpp_bus_driver::Rx8130ce::Weekday::kThursday,
          31, 12, 26};
      const bool reset = rtc.SetTime(initial_time);
      const bool clear_voltage_loss = reset && rtc.ClearVoltageLossFlag();
      const bool success = reset && clear_voltage_loss;
      printf("BOOT: RX8130CE time reset to 2026-12-31 23:59:55: %s%s%s\n",
          success ? "success" : "failed", "", "");
    }
  }
}

#endif

