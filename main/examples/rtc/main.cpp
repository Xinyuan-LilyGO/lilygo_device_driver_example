#include "common.h"
#include "rtc.h"

namespace {

cpp_bus_driver::PlatformHal g_platform_hal;

}  // namespace

bool WaitForRtcReset() {
  static bool was_pressed = false;
  for (int i = 0; i < 50; ++i) {
    const bool pressed =
        g_platform_hal.GpioRead(common::BootButtonGpio()) == 0;
    if (pressed != was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if ((g_platform_hal.GpioRead(common::BootButtonGpio()) == 0) ==
          pressed) {
        was_pressed = pressed;
        if (pressed) {
          return true;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return false;
}

extern "C" void app_main(void) {
  printf("RTC example on %s %s\n", common::kBoardName,
      common::GetDriver().device_model_info().version);
  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }
  if (!g_platform_hal.SetGpioMode(common::BootButtonGpio(),
          cpp_bus_driver::PlatformHal::GpioMode::kInput,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    return;
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  RunPcf8563RtcExample();
#else
  RunInternalRtcExample();
#endif
}
