/*
 * @Description: 控制 XL9535 GPIO 扩展器全部引脚高低电平切换的示例
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"

extern "C" void app_main(void) {
  printf("XL9535 example on %s\n", common::kBoardName);
  auto& driver = common::GetDriver();
  common::InitDriver();
  if (!driver.IsXl9535Ready()) {
    printf("XL9535 init failed\n");
    return;
  }
  auto& xl9535 = driver.chip().xl9535;

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
