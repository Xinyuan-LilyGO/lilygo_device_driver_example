/*
 * @Description: RX8130CE 时钟、定时器与闹钟测试
 */
#include "rtc.h"

#include <cstdio>
#include "common.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)

namespace {

using PlatformHal = cpp_bus_driver::PlatformHal;
using Rtc = cpp_bus_driver::Rx8130ce;

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
constexpr int kRtcInterruptGpio = common::board::gpio::rx8130ce::kInt;
constexpr bool kTestTimerInterrupt = true;
constexpr bool kSkipCalendarOnVoltageLoss = true;
#else
constexpr int kRtcInterruptGpio = 0;
constexpr bool kTestTimerInterrupt = false;
constexpr bool kSkipCalendarOnVoltageLoss = false;
#endif
constexpr uint16_t kTimerPeriodSeconds = 10;

void OnRtcInterrupt(void* arg) {
  BaseType_t task_woken = pdFALSE;
  vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(arg), &task_woken);
  if (task_woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

bool InitInterruptPin(PlatformHal& platform_hal) {
  if constexpr (kTestTimerInterrupt) {
    return platform_hal.InitGpioInterrupt(kRtcInterruptGpio,
        PlatformHal::InterruptMode::kFalling, OnRtcInterrupt,
        xTaskGetCurrentTaskHandle(), PlatformHal::GpioStatus::kPullup);
  }
  return platform_hal.SetGpioMode(kRtcInterruptGpio,
      PlatformHal::GpioMode::kInput, PlatformHal::GpioStatus::kPullup);
}

bool ConfigureInterrupts(Rtc& rtc) {
  const Rtc::Alarm alarm = {
      .minute = 0,
      .hour = 0,
      .day_or_weekday = 1,
      .minute_enabled = true,
      .hour_enabled = true,
      .day_or_weekday_enabled = true,
      .weekday_mode = false,
  };
  if constexpr (kTestTimerInterrupt) {
    if (!rtc.SetUpdateInterruptEnabled(false) ||
        !rtc.SetTimerInterruptEnabled(false) || !rtc.StopTimer() ||
        !rtc.ClearTimerFlag()) {
      return false;
    }
  }
  if (!rtc.SetAlarmInterruptEnabled(false) || !rtc.SetAlarm(alarm) ||
      !rtc.ClearAlarmFlag() || !rtc.SetAlarmInterruptEnabled(true)) {
    return false;
  }
  if constexpr (kTestTimerInterrupt) {
    const Rtc::Timer timer = {
        .counter = kTimerPeriodSeconds,
        .clock = Rtc::TimerClock::k1Hz,
        .enabled = true,
    };
    return rtc.SetTimerInterruptEnabled(true) && rtc.SetTimer(timer);
  }
  return true;
}

bool ResetTime(Rtc& rtc) {
  const Rtc::Time initial_time = {
      55, 59, 23, Rtc::Weekday::kThursday, 31, 12, 26};
  Rtc::Status status;
  return rtc.SetTime(initial_time) && rtc.ClearVoltageLossFlag() &&
         rtc.GetStatus(status) && !status.voltage_loss &&
         ConfigureInterrupts(rtc);
}

}  // namespace

void RunRx8130ceRtcExample() {
  auto& driver = common::GetDriver();
  if (!driver.InitRx8130ce()) {
    return;
  }

  auto& rtc = *driver.chip().rx8130ce;
  PlatformHal platform_hal;
  if (!InitInterruptPin(platform_hal)) {
    return;
  }
  if (!ConfigureInterrupts(rtc)) {
    if constexpr (kTestTimerInterrupt) {
      platform_hal.DeinitGpioInterrupt(kRtcInterruptGpio);
    }
    return;
  }

  printf("RX8130CE started; alarm: 1st day of each month at 00:00, INT: GPIO%d\n",
      kRtcInterruptGpio);
  if constexpr (kTestTimerInterrupt) {
    printf("RTC timer period: %u seconds, INT idle level: %d\n",
        static_cast<unsigned>(kTimerPeriodSeconds),
        platform_hal.GpioRead(kRtcInterruptGpio));
  }
  bool reported_voltage_loss = false;
  while (true) {
    if constexpr (kTestTimerInterrupt) {
      const auto interrupt_count = ulTaskNotifyTake(pdTRUE, 0);
      if (interrupt_count != 0) {
        printf("RTC GPIO%d falling-edge interrupts: %lu\n", kRtcInterruptGpio,
            static_cast<unsigned long>(interrupt_count));
      }
    }
    Rtc::Status status;
    const bool status_read = rtc.GetStatus(status);
    if (!kSkipCalendarOnVoltageLoss || (status_read && !status.voltage_loss)) {
      Rtc::Time time;
      if (rtc.GetTime(time)) {
        printf("RTC time: %04d-%02d-%02d %02d:%02d:%02d, weekday: %d\n",
            time.year + 2000, time.month, time.day, time.hour, time.minute,
            time.second, static_cast<int>(time.weekday));
      } else {
        printf("RX8130CE calendar read failed\n");
      }
    }
    if (status_read) {
      if (status.voltage_loss && !reported_voltage_loss) {
        if constexpr (kSkipCalendarOnVoltageLoss) {
          printf("RX8130CE calendar invalid (VLF set); "
                 "press BOOT to set the test time\n");
        } else {
          printf("RX8130CE VLF is set; checking calendar data\n");
        }
      }
      reported_voltage_loss = status.voltage_loss;

      if constexpr (kTestTimerInterrupt) {
        if (status.timer_flag) {
          printf("RTC timer flag set\n");
          if (!rtc.ClearTimerFlag()) {
            printf("RX8130CE timer flag clear failed\n");
          }
        }
      }
      if (status.alarm_flag) {
        printf("RTC alarm flag set (GPIO%d level: %d)\n", kRtcInterruptGpio,
            platform_hal.GpioRead(kRtcInterruptGpio));
        if (!rtc.ClearAlarmFlag()) {
          printf("RX8130CE alarm flag clear failed\n");
        } else if constexpr (kTestTimerInterrupt) {
          printf("RTC GPIO%d level after clearing alarm: %d\n",
              kRtcInterruptGpio, platform_hal.GpioRead(kRtcInterruptGpio));
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

