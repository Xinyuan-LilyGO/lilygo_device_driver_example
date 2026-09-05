/*
 * @Description: NRF24L01 Enhanced ShockBurst TX/RX on the P4 keyboard expansion
 * @License: GPL 3.0
 */
#include "common.h"

#include <algorithm>
#include <array>

namespace {

using Radio = cpp_bus_driver::Nrf24l01x;
using DeviceDriver = common::DeviceDriver;
using PlatformHal = cpp_bus_driver::PlatformHal;

// Defaults match lilygobox-espidf's EnhancedShockBurstRadioConfig.
constexpr uint8_t kChannel = 0;
constexpr std::array<uint8_t, 5> kAddress = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
constexpr bool kAutoAckEnabled = false;
constexpr bool kDynamicPayloadEnabled = false;
constexpr std::array<uint8_t, 9> kTestPayload = {1, 2, 3, 4, 5, 6, 7, 8, 9};
static_assert(kChannel <= 125);
static_assert(kTestPayload.size() <= Radio::kMaximumPayloadLength);
static_assert(!kDynamicPayloadEnabled || kAutoAckEnabled,
    "Dynamic payloads require auto ACK on both peers");

bool ButtonPressed(PlatformHal& platform_hal) {
  return !platform_hal.GpioRead(common::BootButtonGpio());
}

const char* TransmitResultName(Radio::TransmitResult result) {
  switch (result) {
    case Radio::TransmitResult::kSuccess:
      return "completed";
    case Radio::TransmitResult::kMaximumRetransmit:
      return "maximum retransmit";
    case Radio::TransmitResult::kTimeout:
      return "timeout";
    case Radio::TransmitResult::kInvalidArgument:
      return "invalid argument";
    case Radio::TransmitResult::kBusError:
      return "bus or GPIO error";
  }
  return "unknown";
}

bool ReceivePackets(Radio& radio) {
  std::array<uint8_t, Radio::kMaximumPayloadLength> buffer{};
  // The hardware FIFO holds three packets; bound each pass so BOOT stays responsive.
  for (size_t packet = 0; packet < 3; ++packet) {
    bool fifo_empty = true;
    if (!radio.RxFifoEmpty(&fifo_empty)) {
      printf("NRF24L01 RX FIFO status read failed\n");
      return false;
    }
    if (fifo_empty) {
      return true;
    }

    size_t received_size = 0;
    uint8_t pipe = 0;
    if (!radio.ReadRxPayload(buffer.data(), buffer.size(), &received_size, &pipe) ||
        !radio.RxFifoEmpty(&fifo_empty) ||
        (fifo_empty && !radio.ClearIrqFlag(Radio::IrqSource::kRxDataReady))) {
      printf("NRF24L01 packet read or RX IRQ clear failed\n");
      return false;
    }

    printf("NRF24L01 RX: %zu bytes, pipe %u\n", received_size,
        static_cast<unsigned>(pipe));
    printf("Data:");
    for (size_t index = 0; index < received_size; ++index) {
      printf(" %02X", static_cast<unsigned>(buffer[index]));
    }
    printf("\n");
  }
  return true;
}

}  // namespace

extern "C" void app_main(void) {
  printf("NRF24L01 Enhanced ShockBurst TX/RX example on %s\n", common::kBoardName);
  auto& driver = common::GetDriver();
  if (!common::InitMinimalDriver()) {
    printf("Minimal device driver initialization failed\n");
    return;
  }
  if (!driver.InitKeyboardExpansion()) {
    printf("Some keyboard expansion peripherals failed to initialize\n");
  }
  if (!driver.IsXl9555Ready() || !driver.IsNrf24l01Ready()) {
    printf("Keyboard expansion NRF24L01 is unavailable\n");
    return;
  }

  PlatformHal platform_hal;
  if (!platform_hal.SetGpioMode(common::BootButtonGpio(),
          PlatformHal::GpioMode::kInput, PlatformHal::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    return;
  }

  auto& radio = *driver.chip().nrf24l01;
  Radio::Config config;
  config.operation_mode = Radio::OperationMode::kPrimaryReceiver;
  config.power_mode = Radio::PowerMode::kPowerUp;
  config.crc_mode = Radio::CrcMode::k16Bit;
  config.output_power = Radio::OutputPower::kZeroDbm;
  config.data_rate = Radio::DataRate::k250Kbps;
  config.address_width = Radio::AddressWidth::k5Bytes;
  config.rf_channel = kChannel;
  config.retransmit_count = kAutoAckEnabled ? 3 : 0;
  config.retransmit_delay_us = 750;
  config.enabled_pipe_mask = 0x01;
  config.auto_ack_pipe_mask = kAutoAckEnabled ? 0x01 : 0;
  config.dynamic_payload_enabled = kDynamicPayloadEnabled;
  config.dynamic_payload_pipe_mask = kDynamicPayloadEnabled ? 0x01 : 0;
  config.rx_payload_width[0] =
      kDynamicPayloadEnabled ? 0 : Radio::kMaximumPayloadLength;
  if (!driver.SetNrf24l01OperatingMode(
          DeviceDriver::Nrf24l01OperatingMode::kStandby) ||
      !radio.Configure(config) ||
      !radio.SetAddress(Radio::Address::kPipe0, kAddress.data(), kAddress.size()) ||
      !radio.SetAddress(Radio::Address::kTransmit, kAddress.data(), kAddress.size())) {
    printf("NRF24L01 Enhanced ShockBurst configuration failed\n");
    driver.SetNrf24l01OperatingMode(DeviceDriver::Nrf24l01OperatingMode::kSleep);
    return;
  }

  printf("Enhanced ShockBurst: channel %u (%u MHz), 250 kbps, "
         "0 dBm, CRC 16-bit, auto ACK %s, payload %s\n",
      static_cast<unsigned>(kChannel), 2400U + kChannel,
      kAutoAckEnabled ? "enabled" : "disabled",
      kDynamicPayloadEnabled ? "dynamic" : "fixed 32 bytes");
  printf("Address (SPI byte order):");
  for (const auto byte : kAddress) {
    printf(" %02X", static_cast<unsigned>(byte));
  }
  printf("\n");

  // Static payload mode sends the full width, with unused bytes padded with zero.
  std::array<uint8_t, Radio::kMaximumPayloadLength> transmit_buffer{};
  std::copy(kTestPayload.begin(), kTestPayload.end(), transmit_buffer.begin());
  const size_t transmit_size =
      kDynamicPayloadEnabled ? kTestPayload.size() : transmit_buffer.size();
  bool button_was_pressed = false;
  bool receiving = radio.StartReceive();
  if (receiving) {
    printf("NRF24L01 receive started\n");
  }
  while (receiving) {
    if (!ReceivePackets(radio)) {
      break;
    }

    const bool button_pressed = ButtonPressed(platform_hal);
    if (button_pressed && !button_was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(platform_hal)) {
        // EN_AA controls broadcast mode; no_ack=true would require EN_DYN_ACK.
        const auto result =
            radio.Transmit(transmit_buffer.data(), transmit_size, false, 250);
        receiving = radio.StartReceive();
        printf("NRF24L01 TX: %s (%zu bytes)\n", TransmitResultName(result),
            transmit_size);
      }
    }
    button_was_pressed = button_pressed;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (!receiving) {
    printf("NRF24L01 receive start/restart failed\n");
  }
  if (!driver.SetNrf24l01OperatingMode(DeviceDriver::Nrf24l01OperatingMode::kSleep)) {
    printf("NRF24L01 sleep failed\n");
  }
}
