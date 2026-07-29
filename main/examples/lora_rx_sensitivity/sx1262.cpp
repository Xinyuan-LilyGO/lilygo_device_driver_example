/*
 * @Description: 实现 SX1262 的 LoRa 接收灵敏度测试
 * @Author: LILYGO_L
 * @Date: 2026-07-29 15:09:12
 * @LastEditTime: 2026-07-29 16:55:27
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_rx_sensitivity.h"

#include <array>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace lora_rx_sensitivity {
namespace {

constexpr uint32_t kFrequencyHz = 920000000U;

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
  lora_config.frequency_hz = kFrequencyHz;
  lora_config.spreading_factor =
      static_cast<sx126x_lora_sf_t>(kSpreadingFactor);
  lora_config.bandwidth = SX126X_LORA_BW_125;
  lora_config.coding_rate = SX126X_LORA_CR_4_5;
  lora_config.preamble_length = 8;
  lora_config.sync_word = kPublicSyncWord;
  lora_config.max_payload_length =
      static_cast<uint8_t>(kPayloadLength);
  lora_config.crc_enabled = true;
  lora_config.invert_iq = false;
  lora_config.rx_boosted = true;
  if (!sx1262.Configure(lora_config) || !sx1262.StartReceive()) {
    printf("SX1262 sensitivity-test configuration failed\n");
    return;
  }

  TestSession session("SX1262", kFrequencyHz);
  session.PrintSetup();
  std::array<uint8_t, 255> receive_buffer = {};
  bool button_was_pressed = false;

  while (true) {
    const bool button_pressed = ButtonPressed(tool);
    if (button_pressed && !button_was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(tool)) {
        if (!sx1262.ClearIrqStatus(SX126X_IRQ_ALL) ||
            !sx1262.StartReceive()) {
          printf("SX1262 button restart receive failed\n");
          return;
        }
        session.Restart(esp_log_timestamp());
      }
    }

    bool restart_receive = false;
    if (xl9535.GpioRead(common::board::gpio::xl9535::kRadioDio1) == 1) {
      const uint32_t current_time = esp_log_timestamp();
      sx126x_irq_mask_t irq_status = SX126X_IRQ_NONE;
      if (!sx1262.GetIrqStatus(irq_status) ||
          !sx1262.ClearIrqStatus(irq_status)) {
        session.RecordDriverError();
        restart_receive = true;
      } else if ((irq_status & SX126X_IRQ_HEADER_ERROR) != 0) {
        session.RecordPacketError(
            PacketError::kHeader, current_time);
      } else if ((irq_status & SX126X_IRQ_CRC_ERROR) != 0) {
        session.RecordPacketError(PacketError::kCrc, current_time);
      } else if ((irq_status & SX126X_IRQ_RX_DONE) != 0) {
        uint8_t received_size = 0;
        usp_cpp_bus_driver::Sx126x::PacketMetrics metrics;
        if (sx1262.ReadPacket(receive_buffer.data(),
                receive_buffer.size(), received_size, &metrics)) {
          session.RecordPacket(receive_buffer.data(), received_size,
              {
                  .packet_rssi_dbm =
                      static_cast<float>(metrics.rssi_dbm),
                  .signal_rssi_dbm =
                      static_cast<float>(metrics.signal_rssi_dbm),
                  .snr_db = static_cast<float>(metrics.snr_db),
                  .has_signal_rssi = true,
              },
              current_time);
        } else {
          session.RecordPacketError(PacketError::kRead, current_time);
          restart_receive = true;
        }
      } else if ((irq_status & SX126X_IRQ_TIMEOUT) != 0) {
        session.RecordDriverError();
        restart_receive = true;
      }
    }

    if (restart_receive && !sx1262.StartReceive()) {
      printf("SX1262 restart receive failed\n");
      return;
    }

    session.Poll(esp_log_timestamp());
    button_was_pressed = button_pressed;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace lora_rx_sensitivity

#endif
