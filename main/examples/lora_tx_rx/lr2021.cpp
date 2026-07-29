/*
 * @Description: LR2021 LoRa 数据发送与接收实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-29 18:22:36
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_tx_rx.h"

#include <array>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace lora_tx_rx {
namespace {

constexpr lr20xx_radio_lora_bw_t kLoraBandwidth =
    kUseHighFrequencyPath ? LR20XX_RADIO_LORA_BW_203
                          : LR20XX_RADIO_LORA_BW_125;
constexpr lr20xx_radio_common_rx_path_t kReceivePath =
    kUseHighFrequencyPath ? LR20XX_RADIO_COMMON_RX_PATH_HF
                          : LR20XX_RADIO_COMMON_RX_PATH_LF;
// T-Display-P4的LR2021在1 GHz及以上必须限制为最大+5 dBm
constexpr bool kApplyBoardHighFrequencyPowerLimit =
    kFrequencyHz >= 1000000000U;
constexpr lr20xx_system_irq_mask_t kRadioIrqMask =
    LR20XX_SYSTEM_IRQ_TX_DONE | LR20XX_SYSTEM_IRQ_RX_DONE |
    LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR | LR20XX_SYSTEM_IRQ_CRC_ERROR |
    LR20XX_SYSTEM_IRQ_LEN_ERROR | LR20XX_SYSTEM_IRQ_TIMEOUT;

lr20xx_radio_lora_pkt_params_t MakePacketConfig(uint8_t payload_length) {
  return {
      .preamble_len_in_symb = 8,
      .pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = payload_length,
      .crc = LR20XX_RADIO_LORA_CRC_ENABLED,
      .iq = LR20XX_RADIO_LORA_IQ_STANDARD,
  };
}

bool SetPayloadLength(
    usp_cpp_bus_driver::Lr20xx& lr2021, uint8_t payload_length) {
  const lr20xx_radio_lora_pkt_params_t packet_config =
      MakePacketConfig(payload_length);
  return lr2021.Invoke(
             lr20xx_radio_lora_set_packet_params, &packet_config) ==
         LR20XX_STATUS_OK;
}

bool ReadAndClearIrq(usp_cpp_bus_driver::Lr20xx& lr2021,
    lr20xx_system_irq_mask_t& irq_status) {
  return lr2021.Invoke(
             lr20xx_system_get_and_clear_irq_status, &irq_status) ==
         LR20XX_STATUS_OK;
}

bool StartReceive(usp_cpp_bus_driver::Lr20xx& lr2021) {
  return lr2021.Invoke(lr20xx_radio_fifo_clear_rx) == LR20XX_STATUS_OK &&
         SetPayloadLength(lr2021, 255) && lr2021.StartReceive(0);
}

bool CalibrateFrontEnd(usp_cpp_bus_driver::Lr20xx& lr2021) {
  constexpr lr20xx_radio_common_front_end_calibration_value_t
      kCalibration = {
          .rx_path = kReceivePath,
          .frequency_in_hertz = kFrequencyHz,
      };
  for (uint8_t attempt = 0; attempt < 10; ++attempt) {
    if (lr2021.Invoke(lr20xx_radio_common_calibrate_front_end_helper,
            &kCalibration, static_cast<uint8_t>(1)) == LR20XX_STATUS_OK) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

bool ButtonPressed(cpp_bus_driver::Tool& tool) {
  return tool.GpioRead(common::BootButtonGpio()) == 0;
}

}  // namespace

void RunLr2021() {
  auto& driver = common::GetDriver();
  if (!driver.IsXl9535Ready() || !driver.IsLr2021Ready() ||
      !driver.SetLr2021PowerState(
          common::DeviceDriver::Lr2021PowerState::kStandby)) {
    printf("LR2021 initialization or wake-up failed\n");
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
  auto& lr2021 = *driver.chip().lr2021;
  const usp_cpp_bus_driver::Lr20xx::LoraConfig lora_config = {
      .frequency_hz = kFrequencyHz,
      .modulation =
          {
              .sf = LR20XX_RADIO_LORA_SF12,
              .bw = kLoraBandwidth,
              .cr = LR20XX_RADIO_LORA_CR_4_5,
              .ppm = LR20XX_RADIO_LORA_PPM_1_4,
          },
      .packet =
          {
              .preamble_len_in_symb = 8,
              .pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
              .pld_len_in_bytes = 255,
              .crc = LR20XX_RADIO_LORA_CRC_ENABLED,
              .iq = LR20XX_RADIO_LORA_IQ_STANDARD,
          },
      .sync_word = kPublicSyncWord,
      .rx_path = kReceivePath,
      .rx_boost_mode =
          LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_7,
      .pa =
          {
              .pa_sel =
                  kUseHighFrequencyPath
                      ? LR20XX_RADIO_COMMON_PA_SEL_HF
                      : LR20XX_RADIO_COMMON_PA_SEL_LF,
              .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
              .pa_lf_duty_cycle =
                  kUseHighFrequencyPath ? 6 : 7,
              .pa_lf_slices = 7,
              // T-Display-P4的LR2021在1 GHz以上最大只允许+5 dBm。
              // 官方HF PA表中+5 dBm对应duty cycle 31。
              .pa_hf_duty_cycle =
                  kApplyBoardHighFrequencyPowerLimit ? 31 : 16,
          },
      // HF PA的+5 dBm表项要求SetTxParams写入15个半dBm单位。
      .output_power_half_dbm =
          kApplyBoardHighFrequencyPowerLimit ? 15 : 44,
      .ramp_time = LR20XX_RADIO_COMMON_RAMP_48_US,
  };

  if (!CalibrateFrontEnd(lr2021) || !lr2021.Configure(lora_config) ||
      lr2021.Invoke(lr20xx_system_clear_irq_status,
          LR20XX_SYSTEM_IRQ_ALL_MASK) != LR20XX_STATUS_OK ||
      lr2021.Invoke(lr20xx_system_set_dio_irq_cfg,
          LR20XX_SYSTEM_DIO_11, kRadioIrqMask) != LR20XX_STATUS_OK ||
      !StartReceive(lr2021)) {
    printf("LR2021 LoRa configuration failed\n");
    return;
  }

  std::array<uint8_t, 255> receive_buffer = {};
  bool transmitting = false;
  bool button_was_pressed = false;
  printf("LR2021 LoRa receive started\n");

  while (true) {
    const bool button_pressed = ButtonPressed(tool);
    if (button_pressed && !button_was_pressed && !transmitting) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(tool)) {
        printf("LR2021 send started\n");
        const bool transmit_started =
            lr2021.Invoke(lr20xx_system_clear_irq_status,
                LR20XX_SYSTEM_IRQ_ALL_MASK) == LR20XX_STATUS_OK &&
            lr2021.Invoke(lr20xx_radio_fifo_clear_tx) ==
                LR20XX_STATUS_OK &&
            SetPayloadLength(
                lr2021, static_cast<uint8_t>(kTestPayload.size())) &&
            lr2021.WriteBuffer(kTestPayload.data(), kTestPayload.size()) &&
            lr2021.StartTransmit(5000);
        if (transmit_started) {
          transmitting = true;
        } else {
          printf("LR2021 send failed\n");
          if (!StartReceive(lr2021)) {
            printf("LR2021 restart receive failed\n");
            return;
          }
        }
      }
    }
    button_was_pressed = button_pressed;

    if (xl9535.GpioRead(common::board::gpio::xl9535::kRadioDio1) == 1) {
      lr20xx_system_irq_mask_t irq_status = 0;
      if (!ReadAndClearIrq(lr2021, irq_status)) {
        printf("LR2021 read IRQ failed\n");
      } else if ((irq_status & LR20XX_SYSTEM_IRQ_TX_DONE) != 0) {
        printf("LR2021 send completed\n");
        transmitting = false;
      } else if ((irq_status &
                     (LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR |
                         LR20XX_SYSTEM_IRQ_CRC_ERROR |
                         LR20XX_SYSTEM_IRQ_LEN_ERROR)) != 0) {
        printf("LR2021 receive packet error (IRQ: 0x%08lX)\n",
            static_cast<unsigned long>(irq_status));
      } else if ((irq_status & LR20XX_SYSTEM_IRQ_RX_DONE) != 0) {
        lr20xx_radio_lora_packet_status_t packet_status = {};
        if (lr2021.Invoke(
                lr20xx_radio_lora_get_packet_status, &packet_status) ==
                LR20XX_STATUS_OK &&
            packet_status.packet_length_bytes > 0 &&
            packet_status.packet_length_bytes <= receive_buffer.size() &&
            lr2021.ReadBuffer(
                receive_buffer.data(), packet_status.packet_length_bytes)) {
          const float packet_rssi =
              static_cast<float>(packet_status.rssi_pkt_in_dbm) -
              static_cast<float>(packet_status.rssi_pkt_half_dbm_count) * 0.5f;
          const float signal_rssi =
              static_cast<float>(packet_status.rssi_signal_pkt_in_dbm) -
              static_cast<float>(
                  packet_status.rssi_signal_pkt_half_dbm_count) *
                  0.5f;
          const float snr =
              static_cast<float>(packet_status.snr_pkt_raw) * 0.25f;
          printf(
              "LR2021 receive RSSI: %.2f dBm, signal RSSI: %.2f dBm, "
              "SNR: %.2f dB\n",
              packet_rssi, signal_rssi, snr);
          for (uint8_t index = 0;
               index < packet_status.packet_length_bytes; ++index) {
            printf("LR2021 data[%u]: %u\n",
                static_cast<unsigned int>(index),
                static_cast<unsigned int>(receive_buffer[index]));
          }
        } else {
          printf("LR2021 receive packet failed\n");
        }
      } else if ((irq_status & LR20XX_SYSTEM_IRQ_TIMEOUT) != 0) {
        printf("LR2021 radio timeout\n");
        transmitting = false;
      }

      if (!transmitting && !StartReceive(lr2021)) {
        printf("LR2021 restart receive failed\n");
        return;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace lora_tx_rx

#endif
