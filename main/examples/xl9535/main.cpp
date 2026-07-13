/*
 * @Description: xl9535
 * @Author: LILYGO_L
 * @Date: 2026-07-10 10:27:46
 * @LastEditTime: 2026-07-13 15:14:10
 * @License: GPL 3.0
 */
#include "common.h"

extern "C" void app_main(void) {
  printf("XL9535 example on %s\n", common::kBoardName);
  auto& driver = common::GetDriver();
  common::InitDriver();
  auto& xl9535 = driver.chip().xl9535;
  if (xl9535 == nullptr || !driver.status().xl9535.init_flag) {
    printf("XL9535 init failed\n");
    return;
  }

  using Mode = cpp_bus_driver::Xl95x5::Mode;
  using Pin = cpp_bus_driver::Xl95x5::Pin;
  xl9535->SetGpioMode(Pin::kIoPort0, Mode::kOutput);
  xl9535->SetGpioMode(Pin::kIoPort1, Mode::kOutput);

  while (true) {
    xl9535->GpioWrite(Pin::kIoPort0, 0xFF);
    xl9535->GpioWrite(Pin::kIoPort1, 0xFF);
    printf("XL9535 all pins high\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    xl9535->GpioWrite(Pin::kIoPort0, 0x00);
    xl9535->GpioWrite(Pin::kIoPort1, 0x00);
    printf("XL9535 all pins low\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
