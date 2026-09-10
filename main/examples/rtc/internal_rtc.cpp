#include "common.h"
#include "rtc.h"

#include <cstdlib>
#include <ctime>
#include <sys/time.h>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    (defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
        defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2))

namespace {

bool ResetTime() {
  std::tm initial_time = {};
  initial_time.tm_year = 2026 - 1900;
  initial_time.tm_mon = 11;
  initial_time.tm_mday = 31;
  initial_time.tm_hour = 23;
  initial_time.tm_min = 59;
  initial_time.tm_sec = 55;
  const std::time_t seconds = std::mktime(&initial_time);
  if (seconds == static_cast<std::time_t>(-1)) {
    return false;
  }
  const timeval time = {.tv_sec = seconds, .tv_usec = 0};
  return settimeofday(&time, nullptr) == 0;
}

}  // namespace

void RunInternalRtcExample() {
#if !defined(CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT) && \
    !defined(CONFIG_LIBC_TIME_SYSCALL_USE_RTC) && \
    !defined(CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC_HRT) && \
    !defined(CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC)
  printf("Internal RTC requires an RTC-backed system time configuration\n");
  return;
#endif
  if (setenv("TZ", "CST-8", 1) != 0) {
    printf("RTC timezone configuration failed\n");
    return;
  }
  tzset();
  printf("Internal RTC started (UTC+8)\n");
  while (true) {
    const std::time_t now = std::time(nullptr);
    std::tm time = {};
    char text[32] = {};
    if (now != static_cast<std::time_t>(-1) &&
        localtime_r(&now, &time) != nullptr &&
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &time) != 0) {
      printf("RTC time: %s, weekday: %d\n", text, time.tm_wday);
    } else {
      printf("Internal RTC time read failed\n");
    }
    if (WaitForRtcReset()) {
      const bool reset = ResetTime();
      printf("BOOT: RTC time reset to 2026-12-31 23:59:55: %s\n",
          reset ? "success" : "failed");
    }
  }
}

#endif
