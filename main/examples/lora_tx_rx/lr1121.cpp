/*
 * @Description: LR1121 LoRa 数据发送与接收实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-29 18:00:58
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_tx_rx.h"

#include <array>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)

namespace lora_tx_rx {
namespace {

constexpr lr11xx_radio_lora_bw_t kLoraBandwidth =
    kUseHighFrequencyPath ? LR11XX_RADIO_LORA_BW_200
                          : LR11XX_RADIO_LORA_BW_125;
constexpr uint32_t kRadioIrqMask =
    LR11XX_SYSTEM_IRQ_TX_DONE | LR11XX_SYSTEM_IRQ_RX_DONE |
    LR11XX_SYSTEM_IRQ_HEADER_ERROR | LR11XX_SYSTEM_IRQ_CRC_ERROR |
    LR11XX_SYSTEM_IRQ_TIMEOUT;

lr11xx_radio_pkt_params_lora_t MakePacketConfig(uint8_t payload_length) {
  return {
      .preamble_len_in_symb = 8,
      .header_type = LR11XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = payload_length,
      .crc = LR11XX_RADIO_LORA_CRC_ON,
      .iq = LR11XX_RADIO_LORA_IQ_STANDARD,
  };
}

bool SetPayloadLength(
    usp_cpp_bus_driver::Lr11xx& lr1121, uint8_t payload_length) {
  const lr11xx_radio_pkt_params_lora_t packet_config =
      MakePacketConfig(payload_length);
  return lr1121.Invoke(lr11xx_radio_set_lora_pkt_params, &packet_config) ==
         LR11XX_STATUS_OK;
}

bool ReadAndClearIrq(usp_cpp_bus_driver::Lr11xx& lr1121,
    lr11xx_system_irq_mask_t& irq_status) {
  return lr1121.Invoke(
             lr11xx_system_get_and_clear_irq_status, &irq_status) ==
         LR11XX_STATUS_OK;
}

bool WaitForIrq(cpp_bus_driver::Tool& tool,
    usp_cpp_bus_driver::Lr11xx& lr1121,
    lr11xx_system_irq_mask_t& irq_status, uint32_t timeout_ms) {
  const int64_t deadline_ms = tool.GetSystemTimeMs() + timeout_ms;
  while (tool.GetSystemTimeMs() < deadline_ms) {
    if (tool.GpioRead(common::board::gpio::lr1121::kInt)) {
      return ReadAndClearIrq(lr1121, irq_status);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return false;
}

bool StartReceive(usp_cpp_bus_driver::Lr11xx& lr1121) {
  return SetPayloadLength(lr1121, 255) && lr1121.StartReceive(0);
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
  const usp_cpp_bus_driver::Lr11xx::LoraConfig lora_config = {
      .frequency_hz = kFrequencyHz,
      .modulation =
          {
              .sf = LR11XX_RADIO_LORA_SF12,
              .bw = kLoraBandwidth,
              .cr = LR11XX_RADIO_LORA_CR_4_5,
              .ldro = 1,
          },
      .packet =
          {
              .preamble_len_in_symb = 8,
              .header_type = LR11XX_RADIO_LORA_PKT_EXPLICIT,
              .pld_len_in_bytes = 255,
              .crc = LR11XX_RADIO_LORA_CRC_ON,
              .iq = LR11XX_RADIO_LORA_IQ_STANDARD,
          },
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
    printf("LR1121 receive initialization failed\n");
    return;
  }

  printf("LR1121 LoRa receive started\n");
  bool button_was_pressed = false;
  while (true) {
    const bool button_pressed = ButtonPressed(tool);
    if (button_pressed && !button_was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(tool)) {
        printf("LR1121 send started\n");
        lr11xx_system_irq_mask_t irq_status = LR11XX_SYSTEM_IRQ_NONE;
        const bool transmit_started =
            lr1121.Invoke(lr11xx_system_clear_irq_status,
                LR11XX_SYSTEM_IRQ_ALL_MASK) == LR11XX_STATUS_OK &&
            SetPayloadLength(
                lr1121, static_cast<uint8_t>(kTestPayload.size())) &&
            lr1121.WriteBuffer(kTestPayload.data(), kTestPayload.size()) &&
            lr1121.StartTransmit(5000);
        if (!transmit_started ||
            !WaitForIrq(tool, lr1121, irq_status, 6000) ||
            (irq_status & LR11XX_SYSTEM_IRQ_TX_DONE) == 0) {
          printf("LR1121 send failed (IRQ: 0x%08lX)\n",
              static_cast<unsigned long>(irq_status));
        } else {
          printf("LR1121 send completed\n");
        }

        if (!StartReceive(lr1121)) {
          printf("LR1121 restart receive failed\n");
          return;
        }
      }
    }
    button_was_pressed = button_pressed;

    if (tool.GpioRead(common::board::gpio::lr1121::kInt)) {
      lr11xx_system_irq_mask_t irq_status = LR11XX_SYSTEM_IRQ_NONE;
      if (!ReadAndClearIrq(lr1121, irq_status)) {
        printf("LR1121 read IRQ failed\n");
      } else if ((irq_status & LR11XX_SYSTEM_IRQ_RX_DONE) != 0 &&
                 (irq_status & (LR11XX_SYSTEM_IRQ_HEADER_ERROR |
                                   LR11XX_SYSTEM_IRQ_CRC_ERROR)) == 0) {
        lr11xx_radio_rx_buffer_status_t buffer_status = {};
        lr11xx_radio_pkt_status_lora_t packet_status = {};
        std::array<uint8_t, 255> receive_buffer = {};
        if (lr1121.Invoke(
                lr11xx_radio_get_rx_buffer_status, &buffer_status) ==
                LR11XX_STATUS_OK &&
            buffer_status.pld_len_in_bytes > 0 &&
            lr1121.ReadBuffer(buffer_status.buffer_start_pointer,
                receive_buffer.data(), buffer_status.pld_len_in_bytes) &&
            lr1121.Invoke(
                lr11xx_radio_get_lora_pkt_status, &packet_status) ==
                LR11XX_STATUS_OK) {
          printf("LR1121 receive RSSI: %d dBm, SNR: %d dB\n",
              packet_status.rssi_pkt_in_dbm, packet_status.snr_pkt_in_db);
          for (size_t index = 0;
               index < static_cast<size_t>(
                           buffer_status.pld_len_in_bytes);
               ++index) {
            printf("LR1121 data[%u]: %u\n",
                static_cast<unsigned int>(index),
                static_cast<unsigned int>(receive_buffer[index]));
          }
        } else {
          printf("LR1121 receive packet failed\n");
        }
      } else if ((irq_status & (LR11XX_SYSTEM_IRQ_HEADER_ERROR |
                                   LR11XX_SYSTEM_IRQ_CRC_ERROR)) != 0) {
        printf("LR1121 receive packet error (IRQ: 0x%08lX)\n",
            static_cast<unsigned long>(irq_status));
      } else if ((irq_status & LR11XX_SYSTEM_IRQ_TIMEOUT) != 0) {
        printf("LR1121 radio timeout\n");
      }

      if (!StartReceive(lr1121)) {
        printf("LR1121 restart receive failed\n");
        return;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace lora_tx_rx

#endif
