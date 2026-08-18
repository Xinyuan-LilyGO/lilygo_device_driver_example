/*
 * @Description: 配置并循环切换 SGM38121 多路电源输出的示例
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"

extern "C" void app_main(void) {
  printf("SGM38121 example on %s\n", common::kBoardName);
  auto& driver = common::GetDriver();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  if (!common::InitMinimalDriver() || !driver.InitSgm38121()) {
#else
  if (!common::InitMinimalDriver()) {
#endif
    printf("Minimal device driver initialization failed\n");
    return;
  }
  if (!driver.IsSgm38121Ready()) {
    printf("SGM38121 init failed\n");
    return;
  }
  auto& sgm38121 = driver.chip().sgm38121;

  using Channel = cpp_bus_driver::Sgm38121::Channel;
  using Status = cpp_bus_driver::Sgm38121::Status;
  sgm38121->SetOutputVoltage(Channel::kDvdd1, 1000);
  sgm38121->SetOutputVoltage(Channel::kDvdd2, 1000);
  sgm38121->SetOutputVoltage(Channel::kAvdd1, 1800);
  sgm38121->SetOutputVoltage(Channel::kAvdd2, 2800);

  while (true) {
    sgm38121->SetChannelStatus(Channel::kDvdd1, Status::kOn);
    sgm38121->SetChannelStatus(Channel::kDvdd2, Status::kOn);
    sgm38121->SetChannelStatus(Channel::kAvdd1, Status::kOn);
    sgm38121->SetChannelStatus(Channel::kAvdd2, Status::kOn);
    vTaskDelay(pdMS_TO_TICKS(1000));
    sgm38121->SetChannelStatus(Channel::kDvdd1, Status::kOff);
    sgm38121->SetChannelStatus(Channel::kDvdd2, Status::kOff);
    sgm38121->SetChannelStatus(Channel::kAvdd1, Status::kOff);
    sgm38121->SetChannelStatus(Channel::kAvdd2, Status::kOff);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
