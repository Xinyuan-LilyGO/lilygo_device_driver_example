/*
 * @Description: sgm38121
 * @Author: LILYGO_L
 * @Date: 2026-07-10 10:27:43
 * @LastEditTime: 2026-07-13 00:00:00
 * @License: GPL 3.0
 */
#include "common.h"

extern "C" void app_main(void) {
  printf("SGM38121 example on %s\n", common::kBoardName);
  auto& driver = common::GetDriver();
  common::InitDriver();
  auto& sgm38121 = driver.chip().sgm38121;
  if (sgm38121 == nullptr || !driver.status().sgm38121.init_flag) {
    printf("SGM38121 init failed\n");
    return;
  }

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
