/*
 * @Description: T-Display-P4 的 L76K GPS 定位示例实现
 * @Author: LILYGO_L
 * @Date: 2026-07-29 00:22:40
 * @LastEditTime: 2026-07-29 00:22:40
 * @License: GPL 3.0
 */
#include "common.h"
#include "gps.h"

#include <memory>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace gps {

void RunL76k() {
  constexpr char kSource[] = "L76K";
  auto& driver = common::GetDriver();

  if (!driver.SetL76kSleep(false) || !driver.IsL76kReady() ||
      (driver.chip().l76k == nullptr)) {
    printf("[%s] initialization or wake-up failed\n", kSource);
    return;
  }

  auto& l76k = driver.chip().l76k;
  cpp_bus_driver::L76k::NmeaOutputConfig nmea_config;
  nmea_config.rmc = 1;
  nmea_config.gga = 1;
  nmea_config.gll = 1;
  nmea_config.gsv = 5;
  nmea_config.gsa = 1;
  nmea_config.vtg = 1;
  nmea_config.zda = 1;
  nmea_config.ant = 1;
  printf("[%s] enable RMC/GGA/GLL/GSA/VTG/ZDA/ANT every update and GSV "
         "every five updates: %s\n",
      kSource, l76k->SetNmeaOutputConfig(nmea_config) ? "success" : "failed");
  printf("[%s] UART baud rate: %u, update interval: %u ms\n", kSource,
      static_cast<unsigned int>(l76k->GetBaudRate()),
      static_cast<unsigned int>(l76k->update_interval_ms()));
  l76k->ClearRxBufferData();

  while (true) {
    std::unique_ptr<uint8_t[]> buffer;
    uint32_t buffer_length = 0;
    if (!l76k->GetInfoData(buffer, &buffer_length)) {
      printf("[%s] read failed or timed out\n", kSource);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    PrintRawBlock(kSource, buffer.get(), buffer_length);
    GnssParser::Info info;
    if (l76k->ParseInfo(buffer.get(), buffer_length, info)) {
      PrintGnssInfo(kSource, info);
    } else {
      printf("[%s] no supported NMEA sentence could be parsed\n", kSource);
    }
  }
}

}  // namespace gps

#endif
