/*
 * @Description: SX1262 LoRa 数据发送与接收实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_send_receive.h"

#include <array>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace lora_send_receive {
namespace {

const char* ChipModeToString(sx126x_chip_modes_t mode) {
  switch (mode) {
    case SX126X_CHIP_MODE_UNUSED:
      return "unused";
    case SX126X_CHIP_MODE_RFU:
      return "reserved for future use";
    case SX126X_CHIP_MODE_STBY_RC:
      return "standby RC";
    case SX126X_CHIP_MODE_STBY_XOSC:
      return "standby XOSC";
    case SX126X_CHIP_MODE_FS:
      return "frequency synthesis";
    case SX126X_CHIP_MODE_RX:
      return "receive";
    case SX126X_CHIP_MODE_TX:
      return "transmit";
    default:
      return "unknown";
  }
}

const char* CommandStatusToString(sx126x_cmd_status_t status) {
  switch (status) {
    case SX126X_CMD_STATUS_RESERVED:
      return "reserved";
    case SX126X_CMD_STATUS_RFU:
      return "reserved for future use";
    case SX126X_CMD_STATUS_DATA_AVAILABLE:
      return "data available";
    case SX126X_CMD_STATUS_CMD_TIMEOUT:
      return "command timeout";
    case SX126X_CMD_STATUS_CMD_PROCESS_ERROR:
      return "command processing error";
    case SX126X_CMD_STATUS_CMD_EXEC_FAILURE:
      return "command execution failure";
    case SX126X_CMD_STATUS_CMD_TX_DONE:
      return "transmit done";
    default:
      return "unknown";
  }
}

bool ButtonPressed(cpp_bus_driver::Tool& tool) {
  return tool.GpioRead(common::BootButtonGpio()) == 0;
}

}  // namespace

void RunSx1262() {
  auto& driver = common::GetDriver();
  if (!driver.IsXl9535Ready() || !driver.IsSx1262Ready() ||
      !driver.SetSx1262PowerState(
          common::DeviceDriver::Sx1262PowerState::kStandby)) {
    printf("SX1262 initialization or wake-up failed\n");
    return;
  }

  cpp_bus_driver::Tool tool;
  if (!tool.SetGpioMode(common::BootButtonGpio(),
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    return;
  }

  auto& xl9535 = *driver.chip().xl9535;
  auto& sx1262 = *driver.chip().sx1262;
  usp_cpp_bus_driver::Sx126x::LoraConfig lora_config;
  lora_config.frequency_hz = 920000000;
  lora_config.spreading_factor = SX126X_LORA_SF9;
  lora_config.bandwidth = SX126X_LORA_BW_125;
  lora_config.coding_rate = SX126X_LORA_CR_4_7;
  lora_config.output_power_dbm = 22;
  lora_config.crc_enabled = true;
  if (!sx1262.Configure(lora_config) || !sx1262.StartReceive()) {
    printf("SX1262 LoRa configuration failed\n");
    return;
  }

  std::array<uint8_t, 255> receive_buffer = {};
  bool transmitting = false;
  bool button_was_pressed = false;
  uint32_t status_print_time = 0;
  printf("SX1262 LoRa receive started\n");

  while (true) {
    const bool button_pressed = ButtonPressed(tool);
    if (button_pressed && !button_was_pressed && !transmitting) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(tool)) {
        printf("SX1262 send started\n");
        if (sx1262.StartTransmit(
                kTestPayload.data(), kTestPayload.size())) {
          transmitting = true;
        } else {
          printf("SX1262 send failed\n");
          sx1262.StartReceive();
        }
      }
    }
    button_was_pressed = button_pressed;

    if (xl9535.GpioRead(common::board::gpio::xl9535::kSx1262Dio1) == 1) {
      sx126x_irq_mask_t irq_mask = SX126X_IRQ_NONE;
      if (!sx1262.GetIrqStatus(irq_mask)) {
        printf("SX1262 get IRQ status failed\n");
      } else {
        sx1262.ClearIrqStatus(irq_mask);

        if ((irq_mask & SX126X_IRQ_TX_DONE) != 0) {
          printf("SX1262 send completed\n");
          transmitting = false;
          sx1262.StartReceive();
        } else if ((irq_mask & SX126X_IRQ_TIMEOUT) != 0) {
          printf("SX1262 radio timeout\n");
          transmitting = false;
          sx1262.StartReceive();
        } else if ((irq_mask &
                       (SX126X_IRQ_CRC_ERROR | SX126X_IRQ_HEADER_ERROR)) != 0) {
          printf("SX1262 receive packet error (IRQ: 0x%04lX)\n",
              static_cast<unsigned long>(irq_mask));
          sx1262.StartReceive();
        } else if ((irq_mask & SX126X_IRQ_RX_DONE) != 0) {
          uint8_t received_size = 0;
          usp_cpp_bus_driver::Sx126x::PacketMetrics metrics;
          if (sx1262.ReadPacket(receive_buffer.data(), receive_buffer.size(),
                  received_size, &metrics)) {
            printf("SX1262 receive RSSI: %d dBm, SNR: %d dB\n",
                static_cast<int>(metrics.rssi_dbm),
                static_cast<int>(metrics.snr_db));
            for (uint8_t index = 0; index < received_size; ++index) {
              printf("SX1262 data[%u]: %u\n",
                  static_cast<unsigned int>(index),
                  static_cast<unsigned int>(receive_buffer[index]));
            }
          } else {
            printf("SX1262 receive packet failed\n");
          }
          sx1262.StartReceive();
        }
      }
    }

    const uint32_t current_time = esp_log_timestamp();
    if (current_time >= status_print_time) {
      sx126x_chip_status_t chip_status = {};
      if (sx1262.GetChipStatus(chip_status)) {
        printf("SX1262 chip mode: %s (%d), command status: %s (%d)\n",
            ChipModeToString(chip_status.chip_mode),
            static_cast<int>(chip_status.chip_mode),
            CommandStatusToString(chip_status.cmd_status),
            static_cast<int>(chip_status.cmd_status));
      }
      status_print_time = current_time + 1000;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace lora_send_receive

#endif
