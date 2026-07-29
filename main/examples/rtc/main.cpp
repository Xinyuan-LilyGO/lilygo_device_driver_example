/*
 * @Description: RTC 时钟、定时器与闹钟中断测试
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"

namespace {

volatile bool g_rtc_interrupt_flag = false;

void RtcInterruptCallback(void*) { g_rtc_interrupt_flag = true; }

}  // namespace

extern "C" void app_main(void) {
  printf("RTC example on %s\n", common::kBoardName);

  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

  auto& driver = common::GetDriver();
  if (!driver.IsXl9535Ready() || !driver.IsPcf8563Ready()) {
    printf("RTC or interrupt expander initialization failed\n");
    return;
  }

  auto& xl9535 = *driver.chip().xl9535;
  auto& rtc = *driver.chip().pcf8563;
  cpp_bus_driver::Tool tool;

  cpp_bus_driver::Pcf8563x::Time time = {
      .second = 55,
      .minute = 59,
      .hour = 23,
      .day = 31,
      .week = cpp_bus_driver::Pcf8563x::Week::kSunday,
      .month = 12,
      .year = 99,
  };
  cpp_bus_driver::Pcf8563x::TimeAlarm alarm = {
      .minute =
          {
              .value = 0,
              .alarm_flag = true,
          },
      .hour =
          {
              .value = 0,
              .alarm_flag = true,
          },
      .day =
          {
              .value = 1,
              .alarm_flag = true,
          },
      .week =
          {
              .value = cpp_bus_driver::Pcf8563x::Week::kSunday,
              .alarm_flag = false,
          },
  };

  const bool configured =
      rtc.SetClockFrequencyOutput(
          cpp_bus_driver::Pcf8563x::OutFreq::kClockOff) &&
      rtc.SetClock(false) && rtc.StopTimer() &&
      rtc.StopScheduledAlarm() && rtc.SetTime(time) &&
      rtc.RunTimer(10, cpp_bus_driver::Pcf8563x::TimerFreq::kClock1Hz) &&
      rtc.RunScheduledAlarm(alarm) && rtc.SetClock(true) &&
      xl9535.ClearIrqFlag();
  if (!configured) {
    printf("RTC test configuration failed\n");
    return;
  }

  if (!tool.InitGpioInterrupt(common::board::gpio::xl9535::kInt,
          cpp_bus_driver::Tool::InterruptMode::kFalling,
          RtcInterruptCallback)) {
    printf("RTC interrupt initialization failed\n");
    return;
  }

  printf("RTC clock, 10-second timer and scheduled alarm started\n");
  while (true) {
    if (rtc.CheckClockIntegrityFlag()) {
      if (rtc.GetTime(time)) {
        printf(
            "RTC time: 20%02u-%02u-%02u %02u:%02u:%02u, weekday: %u\n",
            static_cast<unsigned int>(time.year),
            static_cast<unsigned int>(time.month),
            static_cast<unsigned int>(time.day),
            static_cast<unsigned int>(time.hour),
            static_cast<unsigned int>(time.minute),
            static_cast<unsigned int>(time.second),
            static_cast<unsigned int>(time.week));
      }
    } else {
      printf("RTC clock integrity is not guaranteed\n");
      rtc.ClearClockIntegrityFlag();
    }

    if (g_rtc_interrupt_flag) {
      g_rtc_interrupt_flag = false;
      if (xl9535.GpioRead(common::board::gpio::xl9535::kRtcInt) == 0) {
        if (rtc.CheckTimerFlag()) {
          printf("RTC timer interrupt triggered\n");
          rtc.ClearTimerFlag();
        }
        if (rtc.CheckScheduledAlarmFlag()) {
          printf("RTC scheduled alarm interrupt triggered\n");
          rtc.ClearScheduledAlarmFlag();
        }
      }
      xl9535.ClearIrqFlag();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
