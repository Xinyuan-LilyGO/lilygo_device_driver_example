/*
 * @Description: i2c_scan
 * @Author: LILYGO_L
 * @Date: 2026-07-10 10:27:41
 * @LastEditTime: 2026-07-13 00:00:00
 * @License: GPL 3.0
 */
#include <memory>
#include <vector>

#include "common.h"

namespace {

void Scan(const char* name,
    const std::shared_ptr<cpp_bus_driver::HardwareI2c1>& bus) {
  if (bus == nullptr) {
    printf("%s is not available\n", name);
    return;
  }

  std::vector<uint8_t> addresses;
  if (!bus->Scan7bitAddress(&addresses)) {
    printf("%s scan failed\n", name);
    return;
  }
  for (size_t i = 0; i < addresses.size(); ++i) {
    printf("%s device[%zu]: %#x\n", name, i, addresses[i]);
  }
}

}  // namespace

extern "C" void app_main(void) {
  printf("I2C scan example on %s\n", common::kBoardName);
  auto& driver = common::GetDriver();
  common::InitDriver();

  while (true) {
    Scan("port1", driver.bus().xl9535_i2c_bus);
    Scan("port2", driver.bus().sgm38121_i2c_bus);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
