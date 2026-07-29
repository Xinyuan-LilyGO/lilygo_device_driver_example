/*
 * @Description: 实现 LR1121 的 LoRa 接收灵敏度测试
 * @Author: LILYGO_L
 * @Date: 2026-07-29 15:09:12
 * @LastEditTime: 2026-07-29 18:00:58
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_rx_sensitivity.h"

#include <array>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

namespace lora_rx_sensitivity {
namespace {

constexpr lr11xx_radio_lora_bw_t kLoraBandwidth =
    kUseHighFrequencyPath ? LR11XX_RADIO_LORA_BW_200
                          : LR11XX_RADIO_LORA_BW_125;
constexpr uint32_t kRadioIrqMask =
    LR11XX_SYSTEM_IRQ_RX_DONE | LR11XX_SYSTEM_IRQ_HEADER_ERROR |
    LR11XX_SYSTEM_IRQ_CRC_ERROR | LR11XX_SYSTEM_IRQ_TIMEOUT;

lr11xx_radio_pkt_params_lora_t MakePacketConfig() {
  return {
      .preamble_len_in_symb = 8,
      .header_type = LR11XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = static_cast<uint8_t>(kPayloadLength),
      .crc = LR11XX_RADIO_LORA_CRC_ON,
      .iq = LR11XX_RADIO_LORA_IQ_STANDARD,
  };
}

bool SetPacketConfig(usp_cpp_bus_driver::Lr11xx& lr1121) {
  const lr11xx_radio_pkt_params_lora_t packet_config =
      MakePacketConfig();
  return lr1121.Invoke(lr11xx_radio_set_lora_pkt_params,
             &packet_config) == LR11XX_STATUS_OK;
}

bool StartReceive(usp_cpp_bus_driver::Lr11xx& lr1121) {
  return SetPacketConfig(lr1121) && lr1121.StartReceive(0);
}

bool ReadAndClearIrq(usp_cpp_bus_driver::Lr11xx& lr1121,
    lr11xx_system_irq_mask_t& irq_status) {
  return lr1121.Invoke(
             lr11xx_system_get_and_clear_irq_status, &irq_status) ==
         LR11XX_STATUS_OK;
}

bool ButtonPressed(cpp_bus_driver::Tool& tool) {
  return tool.GpioRead(common::BootButtonGpio()) == 0;
}

}  // namespace

void RunLr1121() {
  auto& driver = common::GetDriver();
  if (!driver.IsLr1121Ready() ||
      !driver.SetLr1121PowerState(
          common::DeviceDriver::Lr1121PowerState::kStandby)) {
    printf("LR1121 initialization or wake-up failed\n");
    return;
  }

  cpp_bus_driver::Tool tool;
  if (!tool.SetGpioMode(common::BootButtonGpio(),
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    return;
  }

  auto& lr1121 = *driver.chip().lr1121;
  constexpr auto kLoraSpreadingFactor =
      static_cast<lr11xx_radio_lora_sf_t>(kSpreadingFactor);
  constexpr uint8_t kLdroEnabled =
      kSpreadingFactor >= (kUseHighFrequencyPath ? 12 : 11) ? 1 : 0;
  const usp_cpp_bus_driver::Lr11xx::LoraConfig lora_config = {
      .frequency_hz = kFrequencyHz,
      .modulation =
          {
              .sf = kLoraSpreadingFactor,
              .bw = kLoraBandwidth,
              .cr = LR11XX_RADIO_LORA_CR_4_5,
              .ldro = kLdroEnabled,
          },
      .packet = MakePacketConfig(),
      .sync_word = kPublicSyncWord,
      .rx_boosted = true,
      .pa =
          {
              .pa_sel =
                  kUseHighFrequencyPath ? LR11XX_RADIO_PA_SEL_HF
                                        : LR11XX_RADIO_PA_SEL_HP,
              .pa_reg_supply =
                  kUseHighFrequencyPath
                      ? LR11XX_RADIO_PA_REG_SUPPLY_VREG
                      : LR11XX_RADIO_PA_REG_SUPPLY_VBAT,
              .pa_duty_cycle =
                  kUseHighFrequencyPath ? 0x00 : 0x04,
              .pa_hp_sel =
                  kUseHighFrequencyPath ? 0x00 : 0x07,
          },
      .output_power_dbm = kUseHighFrequencyPath ? 13 : 22,
      .ramp_time = LR11XX_RADIO_RAMP_48_US,
  };

  if (!lr1121.Configure(lora_config) ||
      lr1121.Invoke(lr11xx_system_clear_irq_status,
          LR11XX_SYSTEM_IRQ_ALL_MASK) != LR11XX_STATUS_OK ||
      lr1121.Invoke(lr11xx_system_set_dio_irq_params, kRadioIrqMask,
          LR11XX_SYSTEM_IRQ_NONE) != LR11XX_STATUS_OK ||
      !StartReceive(lr1121)) {
    printf("LR1121 sensitivity-test configuration failed\n");
    return;
  }

  TestSession session("LR1121", kFrequencyHz);
  session.PrintSetup();
  std::array<uint8_t, 255> receive_buffer = {};
  bool button_was_pressed = false;

  while (true) {
    const bool button_pressed = ButtonPressed(tool);
    if (button_pressed && !button_was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(tool)) {
        if (lr1121.Invoke(lr11xx_system_clear_irq_status,
                LR11XX_SYSTEM_IRQ_ALL_MASK) != LR11XX_STATUS_OK ||
            !StartReceive(lr1121)) {
          printf("LR1121 button restart receive failed\n");
          return;
        }
        session.Restart(esp_log_timestamp());
      }
    }

    if (tool.GpioRead(common::board::gpio::lr1121::kInt)) {
      const uint32_t current_time = esp_log_timestamp();
      lr11xx_system_irq_mask_t irq_status = LR11XX_SYSTEM_IRQ_NONE;
      if (!ReadAndClearIrq(lr1121, irq_status)) {
        session.RecordDriverError();
      } else if ((irq_status & LR11XX_SYSTEM_IRQ_HEADER_ERROR) != 0) {
        session.RecordPacketError(
            PacketError::kHeader, current_time);
      } else if ((irq_status & LR11XX_SYSTEM_IRQ_CRC_ERROR) != 0) {
        session.RecordPacketError(PacketError::kCrc, current_time);
      } else if ((irq_status & LR11XX_SYSTEM_IRQ_RX_DONE) != 0) {
        lr11xx_radio_rx_buffer_status_t buffer_status = {};
        lr11xx_radio_pkt_status_lora_t packet_status = {};
        if (lr1121.Invoke(
                lr11xx_radio_get_rx_buffer_status, &buffer_status) ==
                LR11XX_STATUS_OK &&
            buffer_status.pld_len_in_bytes > 0 &&
            buffer_status.pld_len_in_bytes <= receive_buffer.size() &&
            lr1121.ReadBuffer(buffer_status.buffer_start_pointer,
                receive_buffer.data(),
                buffer_status.pld_len_in_bytes) &&
            lr1121.Invoke(
                lr11xx_radio_get_lora_pkt_status, &packet_status) ==
                LR11XX_STATUS_OK) {
          session.RecordPacket(receive_buffer.data(),
              buffer_status.pld_len_in_bytes,
              {
                  .packet_rssi_dbm = static_cast<float>(
                      packet_status.rssi_pkt_in_dbm),
                  .signal_rssi_dbm = static_cast<float>(
                      packet_status.signal_rssi_pkt_in_dbm),
                  .snr_db =
                      static_cast<float>(packet_status.snr_pkt_in_db),
                  .has_signal_rssi = true,
              },
              current_time);
        } else {
          session.RecordPacketError(PacketError::kRead, current_time);
        }
      } else if ((irq_status & LR11XX_SYSTEM_IRQ_TIMEOUT) != 0) {
        session.RecordDriverError();
      }

      if (!StartReceive(lr1121)) {
        printf("LR1121 restart receive failed\n");
        return;
      }
    }

    session.Poll(esp_log_timestamp());
    button_was_pressed = button_pressed;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace lora_rx_sensitivity

#endif
