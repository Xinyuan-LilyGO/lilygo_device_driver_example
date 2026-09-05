/*
 * @Description: CC1101 GFSK TX/RX on the P4 keyboard expansion
 * @License: GPL 3.0
 */
#include "common.h"

#include <array>
#include <atomic>

namespace {

using Radio = cpp_bus_driver::Cc1101;
using DeviceDriver = common::DeviceDriver;
using PlatformHal = cpp_bus_driver::PlatformHal;
namespace keyboard_gpio = common::board::keyboard_expansion::gpio;

// Defaults match lilygobox-espidf's GfskRadioConfig.
constexpr uint32_t kFrequencyHz = 868000000U;
constexpr size_t kMaximumPacketLength = 60;
constexpr std::array<uint8_t, 9> kTestPayload = {1, 2, 3, 4, 5, 6, 7, 8, 9};
static_assert((kFrequencyHz >= 300000000U && kFrequencyHz <= 348000000U) ||
                  (kFrequencyHz >= 387000000U && kFrequencyHz <= 464000000U) ||
                  (kFrequencyHz >= 779000000U && kFrequencyHz <= 928000000U),
    "Select a frequency supported by the keyboard expansion RF paths");
static_assert(kTestPayload.size() <= kMaximumPacketLength);
static_assert(std::atomic<bool>::is_always_lock_free);

std::atomic<bool> g_receive_pending{false};

void ReceiveInterrupt(void*) {
  g_receive_pending.store(true, std::memory_order_release);
}

constexpr DeviceDriver::Cc1101RfSwitch RfSwitch() {
  if (kFrequencyHz <= 348000000U) {
    return DeviceDriver::Cc1101RfSwitch::k315Mhz;
  }
  if (kFrequencyHz <= 464000000U) {
    return DeviceDriver::Cc1101RfSwitch::k434Mhz;
  }
  return DeviceDriver::Cc1101RfSwitch::k868_915Mhz;
}

bool ButtonPressed(PlatformHal& platform_hal) {
  return !platform_hal.GpioRead(common::BootButtonGpio());
}

}  // namespace

extern "C" void app_main(void) {
  printf("CC1101 GFSK TX/RX example on %s\n", common::kBoardName);
  auto& driver = common::GetDriver();
  if (!common::InitMinimalDriver()) {
    printf("Minimal device driver initialization failed\n");
    return;
  }
  if (!driver.InitKeyboardExpansion()) {
    printf("Some keyboard expansion peripherals failed to initialize\n");
  }
  if (!driver.IsXl9555Ready() || !driver.IsCc1101Ready()) {
    printf("Keyboard expansion CC1101 is unavailable\n");
    return;
  }

  PlatformHal platform_hal;
  if (!platform_hal.SetGpioMode(common::BootButtonGpio(),
          PlatformHal::GpioMode::kInput, PlatformHal::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    return;
  }

  auto& radio = *driver.chip().cc1101;
  Radio::Config config;
  config.frequency_mhz = static_cast<double>(kFrequencyHz) / 1000000.0;
  config.data_rate_kbaud = 4.8;
  config.frequency_deviation_khz = 5.0;
  config.receive_bandwidth_khz = 58.036;
  config.output_power_dbm = 10;
  config.preamble_length_bits = 32;
  config.sync_word_high = 0x12;
  config.sync_word_low = 0xAD;
  config.modulation = Radio::Modulation::kGfsk;
  config.encoding = Radio::Encoding::kNrz;
  config.maximum_packet_length = kMaximumPacketLength;
  config.packet_length_mode = Radio::PacketLengthMode::kVariable;
  config.crc_enabled = true;
  config.crc_autoflush = true;
  config.append_status = true;
  config.fec_enabled = false;
  if (!driver.SetCc1101RfSwitch(RfSwitch()) ||
      !driver.SetCc1101OperatingMode(DeviceDriver::Cc1101OperatingMode::kStandby) ||
      !radio.Configure(config)) {
    printf("CC1101 GFSK configuration failed\n");
    driver.SetCc1101OperatingMode(DeviceDriver::Cc1101OperatingMode::kSleep);
    return;
  }

  g_receive_pending.store(false, std::memory_order_relaxed);
  if (!platform_hal.InitGpioInterrupt(keyboard_gpio::t_mix_rf::cc1101::kGdo0,
          PlatformHal::InterruptMode::kFalling, ReceiveInterrupt, nullptr,
          PlatformHal::GpioStatus::kDisable)) {
    printf("CC1101 receive interrupt initialization failed\n");
    driver.SetCc1101OperatingMode(DeviceDriver::Cc1101OperatingMode::kSleep);
    return;
  }

  printf("GFSK: %.3f MHz, %.1f kBaud, deviation %.1f kHz, "
         "RX bandwidth %.3f kHz, sync 0x%02X%02X, power %d dBm\n",
      config.frequency_mhz, config.data_rate_kbaud,
      config.frequency_deviation_khz, config.receive_bandwidth_khz,
      static_cast<unsigned>(config.sync_word_high),
      static_cast<unsigned>(config.sync_word_low), config.output_power_dbm);

  std::array<uint8_t, kMaximumPacketLength> receive_buffer{};
  bool button_was_pressed = false;
  bool receiving = radio.StartReceive();
  if (receiving) {
    printf("CC1101 receive started\n");
  }
  while (receiving) {
    if (g_receive_pending.exchange(false, std::memory_order_acq_rel)) {
      size_t received_size = 0;
      Radio::PacketMetrics metrics;
      const bool received = radio.ReadReceivedPacket(receive_buffer.data(),
          receive_buffer.size(), &received_size, &metrics);
      receiving = radio.StartReceive();
      if (received) {
        printf("CC1101 RX: %zu bytes, RSSI %.2f dBm, LQI %u, CRC %s\n",
            received_size, static_cast<double>(metrics.rssi_dbm),
            static_cast<unsigned>(metrics.lqi),
            metrics.crc_valid ? "valid" : "invalid");
        printf("Data:");
        for (size_t index = 0; index < received_size; ++index) {
          printf(" %02X", static_cast<unsigned>(receive_buffer[index]));
        }
        printf("\n");
      } else {
        printf("CC1101 packet read failed or packet rejected\n");
      }
      if (!receiving) {
        break;
      }
    }

    const bool button_pressed = ButtonPressed(platform_hal);
    if (button_pressed && !button_was_pressed) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (ButtonPressed(platform_hal)) {
        g_receive_pending.store(false, std::memory_order_relaxed);
        const bool transmitted =
            radio.Transmit(kTestPayload.data(), kTestPayload.size());
        // TX completion also produces a GDO0 falling edge; discard it before RX.
        g_receive_pending.store(false, std::memory_order_relaxed);
        receiving = radio.StartReceive();
        printf("CC1101 TX: %s (%zu bytes)\n",
            transmitted ? "completed" : "failed", kTestPayload.size());
      }
    }
    button_was_pressed = button_pressed;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  printf("CC1101 receive start/restart failed\n");
  if (!platform_hal.DeinitGpioInterrupt(keyboard_gpio::t_mix_rf::cc1101::kGdo0)) {
    printf("CC1101 receive interrupt cleanup failed\n");
  }
  if (!driver.SetCc1101OperatingMode(DeviceDriver::Cc1101OperatingMode::kSleep)) {
    printf("CC1101 sleep failed\n");
  }
}
