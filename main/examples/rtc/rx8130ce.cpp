/*
 * @Description: RX8130CE 外部实时时钟示例
 * @License: GPL 3.0
 */
#include "rtc.h"

#include <cstdio>
#include "common.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

namespace {

constexpr int kRtcInterruptGpio = 0;
using Rtc = cpp_bus_driver::Rx8130ce;

bool ConfigureAlarm(Rtc& rtc) {
  const Rtc::Alarm alarm = {
      .minute = 0,
      .hour = 0,
      .day_or_weekday = 1,
      .minute_enabled = true,
      .hour_enabled = true,
      .day_or_weekday_enabled = true,
      .weekday_mode = false,
  };
  return rtc.SetAlarmInterruptEnabled(false) && rtc.SetAlarm(alarm) &&
         rtc.ClearAlarmFlag() && rtc.SetAlarmInterruptEnabled(true);
}

bool ResetTime(Rtc& rtc) {
  const Rtc::Time initial_time = {
      55, 59, 23, Rtc::Weekday::kThursday, 31, 12, 26};
  const bool written = rtc.SetTime(initial_time);
  const bool flag_cleared = written && rtc.ClearVoltageLossFlag();
  return written && flag_cleared && ConfigureAlarm(rtc);
}

}  // namespace

void RunRx8130ceRtcExample() {
  auto& driver = common::GetDriver();
  if (!driver.IsRx8130ceReady() || driver.chip().rx8130ce == nullptr) {
    printf("RX8130CE initialization failed\n");
    return;
  }

  auto& rtc = *driver.chip().rx8130ce;
  cpp_bus_driver::PlatformHal platform_hal;
  if (!platform_hal.SetGpioMode(kRtcInterruptGpio,
          cpp_bus_driver::PlatformHal::GpioMode::kInput,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup)) {
    printf("RX8130CE interrupt GPIO0 initialization failed\n");
    return;
  }
  if (!ConfigureAlarm(rtc)) {
    printf("RX8130CE alarm configuration failed\n");
    return;
  }

  cpp_bus_driver::Rx8130ce::Status status;
  const bool status_read = rtc.GetStatus(status);
  if (!status_read) {
    printf("RX8130CE status read failed\n");
  }
  if (status_read && status.voltage_loss) {
    printf("RX8130CE VLF is set; checking calendar data\n");
  }

  printf("RX8130CE started; alarm: 1st day of each month at 00:00, INT: GPIO0\n");
  while (true) {
    cpp_bus_driver::Rx8130ce::Time time;
    if (rtc.GetTime(time)) {
      printf("RTC time: %04d-%02d-%02d %02d:%02d:%02d, weekday: %d\n",
          time.year + 2000, time.month, time.day, time.hour, time.minute,
          time.second, static_cast<int>(time.weekday));
    } else {
      printf("RX8130CE calendar read failed\n");
    }

    if (rtc.GetStatus(status)) {
      if (status.alarm_flag) {
        printf("RTC alarm flag set (GPIO0 level: %d)\n",
            platform_hal.GpioRead(kRtcInterruptGpio));
        if (!rtc.ClearAlarmFlag()) {
          printf("RX8130CE alarm flag clear failed\n");
        }
      }
    } else {
      printf("RX8130CE status read failed\n");
    }

    if (WaitForRtcReset()) {
      printf("BOOT: RTC time reset to 2026-12-31 23:59:55: %s\n",
          ResetTime(rtc) ? "success" : "failed");
    }
  }
}

#endif

