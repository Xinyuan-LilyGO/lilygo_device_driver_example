/*
 * @Description: T-Display-P4-Air 的 nRF9151 GNSS 定位示例实现
 * @Author: LILYGO_L
 * @Date: 2026-07-29 00:22:40
 * @LastEditTime: 2026-07-29 00:22:40
 * @License: GPL 3.0
 */
#include "common.h"
#include "gps.h"

#include <cstdio>
#include <string>
#include <vector>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

namespace gps {
namespace {

constexpr uint32_t kCommandTimeoutMs = 5000;
constexpr size_t kPendingDataLimit = 8192;

bool SendCommand(cpp_bus_driver::Nrf9151& nrf9151, const char* command) {
  std::string response;
  const cpp_bus_driver::Nrf9151::CommandResult result =
      nrf9151.SendCommand(command, &response, kCommandTimeoutMs);

  printf("\n[nRF9151][AT] command: %s\n", command);
  printf("[nRF9151][AT] result: %s\n",
      cpp_bus_driver::Nrf9151::CommandResultToString(result));
  PrintRawBlock("nRF9151 AT response",
      reinterpret_cast<const uint8_t*>(response.data()), response.size());
  return result == cpp_bus_driver::Nrf9151::CommandResult::kOk;
}

const char* GnssStatusToString(int status) {
  switch (status) {
    case 0:
      return "stopped";
    case 1:
      return "started";
    case 2:
      return "periodic wake-up";
    case 3:
      return "sleep after timeout";
    case 4:
      return "sleep after fix";
    default:
      return "unknown";
  }
}

void PrintCustomGnssLine(const std::string& line) {
  double latitude = 0.0;
  double longitude = 0.0;
  float altitude = 0.0F;
  float accuracy = 0.0F;
  float speed = 0.0F;
  float heading = 0.0F;
  char datetime[32] = {};

  const int fix_fields = sscanf(line.c_str(),
      "#XGNSS: %lf,%lf,%f,%f,%f,%f,\"%31[^\"]\"", &latitude, &longitude,
      &altitude, &accuracy, &speed, &heading, datetime);
  if (fix_fields == 7) {
    printf("[nRF9151][FIX] latitude: %.10lf deg\n", latitude);
    printf("[nRF9151][FIX] longitude: %.10lf deg\n", longitude);
    printf("[nRF9151][FIX] altitude: %.3f m\n", altitude);
    printf("[nRF9151][FIX] accuracy: %.3f m\n", accuracy);
    printf("[nRF9151][FIX] speed: %.3f m/s, %.3f km/h\n", speed,
        speed * 3.6F);
    printf("[nRF9151][FIX] heading: %.3f deg\n", heading);
    printf("[nRF9151][FIX] UTC date/time: %s\n", datetime);
    return;
  }

  int activated = 0;
  int status = 0;
  if (sscanf(line.c_str(), "#XGNSS: %d,%d", &activated, &status) == 2) {
    printf("[nRF9151][STATUS] activated: %s, state: %d (%s)\n",
        activated != 0 ? "yes" : "no", status, GnssStatusToString(status));
  }
}

std::string TrimLineEndings(const std::string& value) {
  size_t begin = 0;
  while ((begin < value.size()) &&
      ((value[begin] == '\r') || (value[begin] == '\n'))) {
    ++begin;
  }

  size_t end = value.size();
  while ((end > begin) &&
      ((value[end - 1] == '\r') || (value[end - 1] == '\n'))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

void ProcessUartLine(const std::string& raw_line, GnssParser& parser) {
  const std::string line = TrimLineEndings(raw_line);
  if (line.empty()) {
    return;
  }

  printf("\n[nRF9151][UART] %s\n", line.c_str());
  if (line.front() == '$') {
    GnssParser::Info info;
    if (parser.ParseInfo(reinterpret_cast<const uint8_t*>(line.data()),
            line.size(), info)) {
      PrintGnssInfo("nRF9151", info);
    } else {
      printf("[nRF9151][NMEA] unsupported or invalid sentence\n");
    }
  } else if (line.rfind("#XGNSS:", 0) == 0) {
    PrintCustomGnssLine(line);
  }
}

void ReadGnssOutput(cpp_bus_driver::HardwareUart& uart,
    std::string& pending_data, GnssParser& parser) {
  const size_t available = uart.GetRxBufferLength();
  if (available == 0) {
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }

  std::vector<uint8_t> buffer(available);
  const int32_t bytes_read =
      uart.Read(buffer.data(), static_cast<uint32_t>(buffer.size()));
  if (bytes_read <= 0) {
    printf("[nRF9151][UART] read failed\n");
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }

  pending_data.append(reinterpret_cast<const char*>(buffer.data()),
      static_cast<size_t>(bytes_read));
  while (true) {
    const size_t line_end = pending_data.find('\n');
    if (line_end == std::string::npos) {
      break;
    }

    ProcessUartLine(pending_data.substr(0, line_end + 1), parser);
    pending_data.erase(0, line_end + 1);
  }

  if (pending_data.size() > kPendingDataLimit) {
    printf("\n[nRF9151][UART] oversized partial frame (%u bytes):\n",
        static_cast<unsigned int>(pending_data.size()));
    fwrite(pending_data.data(), 1, pending_data.size(), stdout);
    printf("\n[nRF9151][UART] partial frame cleared\n");
    pending_data.clear();
  }
}

}  // namespace

void RunNrf9151() {
  auto& driver = common::GetDriver();
  if (!driver.SetNrf9151PowerEnabled(true) ||
      !driver.IsNrf9151Ready() || (driver.chip().nrf9151 == nullptr) ||
      (driver.bus().nrf9151_uart_bus == nullptr)) {
    printf("[nRF9151] power-on or initialization failed\n");
    return;
  }

  auto& nrf9151 = *driver.chip().nrf9151;
  auto& uart = *driver.bus().nrf9151_uart_bus;
  vTaskDelay(pdMS_TO_TICKS(1000));

  // The custom ncs-serial-modem firmware requires raw NMEA output to be
  // enabled before GNSS is started.
  const char* const startup_commands[] = {
      "AT+CFUN=0",
      "AT%XSYSTEMMODE=0,0,1,0",
      "AT+CFUN=31",
      "AT#XGNSSNMEA=1",
      "AT#XGNSS=1,0,1",
  };
  for (size_t index = 0;
       index < (sizeof(startup_commands) / sizeof(startup_commands[0]));
       ++index) {
    if (SendCommand(nrf9151, startup_commands[index])) {
      continue;
    }

    printf("[nRF9151] GNSS startup failed at command: %s\n",
        startup_commands[index]);
    if (index >= 4) {
      SendCommand(nrf9151, "AT#XGNSS=0");
    }
    if (index >= 3) {
      SendCommand(nrf9151, "AT#XGNSSNMEA=0");
    }
    return;
  }

  printf("\n[nRF9151] GNSS continuous navigation started\n");
  printf("[nRF9151] logging raw NMEA, custom #XGNSS fixes/status, and "
         "parsed navigation fields\n");

  GnssParser parser;
  std::string pending_data;
  while (true) {
    ReadGnssOutput(uart, pending_data, parser);
  }
}

}  // namespace gps

#endif
