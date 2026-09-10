/*
 * @Description: PCF8563 时钟、定时器与闹钟测试
 * @Author: LILYGO_L
 * @License: GPL 3.0
 */
#include "common.h"
#include "rtc.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)

namespace {

using Rtc = cpp_bus_driver::Pcf8563x;

/**
 * @brief 配置定时器和闹钟中断，不改写日历
 * @param rtc 已初始化的 RTC
 * @return 配置成功返回 true，否则返回 false
 */
bool ConfigureInterrupts(Rtc& rtc) {
  const Rtc::Alarm alarm = {
      .minute = 0,
      .hour = 0,
      .day = 1,
      .minute_enabled = true,
      .hour_enabled = true,
      .day_enabled = true,
  };
  const Rtc::TimerConfig timer = {
      .enabled = true,
      .frequency = Rtc::TimerFrequency::k1Hz,
      .value = 10,
  };
  return rtc.SetTimerInterrupt(false, Rtc::TimerInterruptMode::kLevel) &&
         rtc.StopTimer() && rtc.SetAlarmInterrupt(false) &&
         rtc.SetAlarm(alarm) && rtc.ClearAlarmFlag() &&
         rtc.ClearTimerFlag() && rtc.SetAlarmInterrupt(true) &&
         rtc.SetTimerInterrupt(true, Rtc::TimerInterruptMode::kLevel) &&
         rtc.SetTimer(timer);
}

/**
 * @brief 设置测试时间并重新开始定时器和闹钟测试
 * @param rtc 已初始化的 RTC
 * @return 所有操作成功返回 true，否则返回 false
 */
bool ResetTime(Rtc& rtc) {
  const Rtc::Time time = {
      .second = 55,
      .minute = 59,
      .hour = 23,
      .day = 31,
      .week = Rtc::Week::kThursday,
      .month = 12,
      .year = 26,
      .century = false,
  };
  if (!rtc.SetClockEnabled(false)) {
    return false;
  }
  const bool written = rtc.SetTime(time);
  // 即使写入失败，也尝试恢复走时，避免短路返回使时钟一直停止。
  const bool started = rtc.SetClockEnabled(true);
  return written && started && ConfigureInterrupts(rtc);
}

}  // namespace

void RunPcf8563RtcExample() {
  auto& driver = common::GetDriver();
  if (!driver.IsXl9535Ready() || !driver.IsPcf8563Ready()) {
    printf("RTC or interrupt expander initialization failed\n");
    return;
  }
  auto& rtc = *driver.chip().pcf8563;
  auto& xl9535 = *driver.chip().xl9535;
  const Rtc::ClockOutConfig clock_out = {};
  if (!rtc.SetClockOut(clock_out) || !ConfigureInterrupts(rtc)) {
    printf("RTC test configuration failed\n");
    return;
  }
  printf("PCF8563 started; calendar preserved, timer period: 10 seconds\n");
  while (true) {
    Rtc::Time time;
    bool voltage_low = false;
    if (rtc.GetTime(time, voltage_low)) {
      printf("RTC time: %02u-%02u-%02u %02u:%02u:%02u, weekday: %u, C: %u, "
             "VL: %u\n",
          static_cast<unsigned>(time.year), static_cast<unsigned>(time.month),
          static_cast<unsigned>(time.day), static_cast<unsigned>(time.hour),
          static_cast<unsigned>(time.minute), static_cast<unsigned>(time.second),
          static_cast<unsigned>(time.week), static_cast<unsigned>(time.century),
          static_cast<unsigned>(voltage_low));
    } else {
      printf("RTC calendar read failed or date invalid\n");
    }
    Rtc::Status status;
    if (rtc.GetStatus(status)) {
      if (status.voltage_low || status.clock_stopped) {
        printf("RTC status: voltage low=%u, clock stopped=%u\n",
            static_cast<unsigned>(status.voltage_low),
            static_cast<unsigned>(status.clock_stopped));
      }
      const int interrupt_level =
          xl9535.GpioRead(common::board::gpio::xl9535::kRtcInt);
      if (status.timer_flag) {
        printf("RTC timer flag set (INT level: %d)\n", interrupt_level);
        if (!rtc.ClearTimerFlag()) {
          printf("RTC timer flag clear failed\n");
        }
      }
      if (status.alarm_flag) {
        printf("RTC alarm flag set (INT level: %d)\n", interrupt_level);
        if (!rtc.ClearAlarmFlag()) {
          printf("RTC alarm flag clear failed\n");
        }
      }
      if (!xl9535.ClearIrqFlag()) {
        printf("RTC interrupt expander clear failed\n");
      }
    } else {
      printf("RTC status read failed\n");
    }
    if (WaitForRtcReset()) {
      printf("BOOT: RTC time reset to 2026-12-31 23:59:55: %s\n",
          ResetTime(rtc) ? "success" : "failed");
    }
  }
}

#endif
