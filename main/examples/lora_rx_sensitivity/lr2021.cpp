/*
 * @Description: 实现 LR2021 的 LoRa 接收灵敏度测试
 * @Author: LILYGO_L
 * @Date: 2026-07-29 15:09:12
 * @LastEditTime: 2026-08-18 17:08:53
 * @License: GPL 3.0
 */
#include "common.h"
#include "lora_rx_sensitivity.h"

#include <array>

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)

namespace lora_rx_sensitivity {
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
constexpr uint8_t kUnusedLfPaDutyCycle = 6;
constexpr uint8_t kLfPaDutyCycle = 7;
constexpr uint8_t kLfPaSlices = 7;
constexpr uint8_t kUnusedHfPaDutyCycle = 16;
constexpr uint8_t kHfPaDutyCycle5Dbm = 31;
constexpr int8_t kLfOutputPower22Dbm = 44;
constexpr int8_t kHfOutputPower5Dbm = 15;
constexpr lr20xx_system_irq_mask_t kRadioIrqMask =
    LR20XX_SYSTEM_IRQ_RX_DONE |
    LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR |
    LR20XX_SYSTEM_IRQ_CRC_ERROR | LR20XX_SYSTEM_IRQ_LEN_ERROR |
    LR20XX_SYSTEM_IRQ_TIMEOUT;

lr20xx_radio_lora_pkt_params_t MakePacketConfig() {
  return {
      .preamble_len_in_symb = 8,
      .pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
      .pld_len_in_bytes = static_cast<uint8_t>(kPayloadLength),
      .crc = LR20XX_RADIO_LORA_CRC_ENABLED,
      .iq = LR20XX_RADIO_LORA_IQ_STANDARD,
  };
}

bool SetPacketConfig(usp_cpp_bus_driver::Lr20xx& lr2021) {
  const lr20xx_radio_lora_pkt_params_t packet_config =
      MakePacketConfig();
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
  return lr2021.Invoke(lr20xx_radio_fifo_clear_rx) ==
             LR20XX_STATUS_OK &&
         SetPacketConfig(lr2021) && lr2021.StartReceive(0);
}

bool CalibrateFrontEnd(usp_cpp_bus_driver::Lr20xx& lr2021) {
  constexpr lr20xx_radio_common_front_end_calibration_value_t
      kCalibration = {
          .rx_path = kReceivePath,
          .frequency_in_hertz = kFrequencyHz,
      };
  for (uint8_t attempt = 0; attempt < 10; ++attempt) {
    if (lr2021.Invoke(lr20xx_radio_common_calibrate_front_end_helper,
            &kCalibration, static_cast<uint8_t>(1)) ==
        LR20XX_STATUS_OK) {
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
      !driver.SetLr2021OperatingMode(
          common::DeviceDriver::Lr2021OperatingMode::kStandby)) {
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
  const auto spreading_factor =
      static_cast<lr20xx_radio_lora_sf_t>(kSpreadingFactor);
  const usp_cpp_bus_driver::Lr20xx::LoraConfig lora_config = {
      .frequency_hz = kFrequencyHz,
      .modulation =
          {
              .sf = spreading_factor,
              .bw = kLoraBandwidth,
              .cr = LR20XX_RADIO_LORA_CR_4_5,
              .ppm =
                  lr20xx_radio_lora_get_recommended_ppm_offset(
                      spreading_factor, kLoraBandwidth),
          },
      .packet = MakePacketConfig(),
      .sync_word = kSyncWord,
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
                  kUseHighFrequencyPath ? kUnusedLfPaDutyCycle
                                        : kLfPaDutyCycle,
              .pa_lf_slices = kLfPaSlices,
              // T-Display-P4的LR2021在1 GHz以上最大只允许+5 dBm。
              // 官方HF PA表中+5 dBm对应duty cycle 31。
              .pa_hf_duty_cycle =
                  kApplyBoardHighFrequencyPowerLimit
                      ? kHfPaDutyCycle5Dbm
                      : kUnusedHfPaDutyCycle,
          },
      // HF PA的+5 dBm表项要求SetTxParams写入15个半dBm单位。
      .output_power_half_dbm =
          kApplyBoardHighFrequencyPowerLimit ? kHfOutputPower5Dbm
                                             : kLfOutputPower22Dbm,
      .ramp_time = LR20XX_RADIO_COMMON_RAMP_48_US,
  };

  if (!CalibrateFrontEnd(lr2021) ||
      !lr2021.Configure(lora_config) ||
      lr2021.Invoke(lr20xx_system_clear_irq_status,
          LR20XX_SYSTEM_IRQ_ALL_MASK) != LR20XX_STATUS_OK ||
      lr2021.Invoke(lr20xx_system_set_dio_irq_cfg,
          LR20XX_SYSTEM_DIO_11, kRadioIrqMask) !=
          LR20XX_STATUS_OK ||
      !StartReceive(lr2021)) {
    printf("LR2021 sensitivity-test configuration failed\n");
    return;
  }

  TestSession session("LR2021", kFrequencyHz);
  session.PrintSetup();
  std::array<uint8_t, 255> receive_buffer = {};
  bool button_was_pressed = false;

  while (true) {
    const bool button_pressed = ButtonPressed(tool);
    if (button_pressed && !button_was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(tool)) {
        if (lr2021.Invoke(lr20xx_system_clear_irq_status,
                LR20XX_SYSTEM_IRQ_ALL_MASK) != LR20XX_STATUS_OK ||
            !StartReceive(lr2021)) {
          printf("LR2021 button restart receive failed\n");
          return;
        }
        session.Restart(esp_log_timestamp());
      }
    }

    if (xl9535.GpioRead(common::board::gpio::xl9535::kRadioDio1) == 1) {
      const uint32_t current_time = esp_log_timestamp();
      lr20xx_system_irq_mask_t irq_status = LR20XX_SYSTEM_IRQ_NONE;
      if (!ReadAndClearIrq(lr2021, irq_status)) {
        session.RecordDriverError();
      } else if ((irq_status &
                     LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR) != 0) {
        session.RecordPacketError(
            PacketError::kHeader, current_time);
      } else if ((irq_status & LR20XX_SYSTEM_IRQ_CRC_ERROR) != 0) {
        session.RecordPacketError(PacketError::kCrc, current_time);
      } else if ((irq_status & LR20XX_SYSTEM_IRQ_LEN_ERROR) != 0) {
        session.RecordPacketError(
            PacketError::kLength, current_time);
      } else if ((irq_status & LR20XX_SYSTEM_IRQ_RX_DONE) != 0) {
        lr20xx_radio_lora_packet_status_t packet_status = {};
        if (lr2021.Invoke(
                lr20xx_radio_lora_get_packet_status, &packet_status) ==
                LR20XX_STATUS_OK &&
            packet_status.packet_length_bytes > 0 &&
            packet_status.packet_length_bytes <= receive_buffer.size() &&
            lr2021.ReadBuffer(receive_buffer.data(),
                packet_status.packet_length_bytes)) {
          const float packet_rssi =
              static_cast<float>(packet_status.rssi_pkt_in_dbm) -
              static_cast<float>(
                  packet_status.rssi_pkt_half_dbm_count) *
                  0.5f;
          const float signal_rssi =
              static_cast<float>(
                  packet_status.rssi_signal_pkt_in_dbm) -
              static_cast<float>(
                  packet_status.rssi_signal_pkt_half_dbm_count) *
                  0.5f;
          const float snr =
              static_cast<float>(packet_status.snr_pkt_raw) * 0.25f;
          session.RecordPacket(receive_buffer.data(),
              packet_status.packet_length_bytes,
              {
                  .packet_rssi_dbm = packet_rssi,
                  .signal_rssi_dbm = signal_rssi,
                  .snr_db = snr,
                  .has_signal_rssi = true,
              },
              current_time);
        } else {
          session.RecordPacketError(PacketError::kRead, current_time);
        }
      } else if ((irq_status & LR20XX_SYSTEM_IRQ_TIMEOUT) != 0) {
        session.RecordDriverError();
      }

      if (!StartReceive(lr2021)) {
        printf("LR2021 restart receive failed\n");
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
