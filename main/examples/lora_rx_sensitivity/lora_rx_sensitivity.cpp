/*
 * @Description: 实现 LoRa 接收灵敏度测试会话、数据校验与丢包率统计
 * @Author: LILYGO_L
 * @Date: 2026-07-29 15:09:12
 * @LastEditTime: 2026-07-29 16:13:45
 * @License: GPL 3.0
 */
#include "lora_rx_sensitivity.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lora_rx_sensitivity {
namespace {

constexpr double kPassingPerPercent = 1.0;

const char* PacketErrorToString(PacketError error) {
  switch (error) {
    case PacketError::kHeader:
      return "header";
    case PacketError::kCrc:
      return "CRC";
    case PacketError::kLength:
      return "length";
    case PacketError::kRead:
      return "read";
    default:
      return "unknown";
  }
}

}  // namespace

void TestSession::MetricStatistics::Add(float value) {
  if (count == 0) {
    minimum = value;
    maximum = value;
  } else {
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }
  sum += value;
  ++count;
}

float TestSession::MetricStatistics::Average() const {
  return count == 0 ? 0.0f : sum / static_cast<float>(count);
}

TestSession::TestSession(const char* radio_name, uint32_t frequency_hz)
    : radio_name_(radio_name), frequency_hz_(frequency_hz) {}

void TestSession::PrintSetup() const {
  printf("\n");
  printf("============================================================\n");
  printf("LoRa RX sensitivity test\n");
  printf("Radio: %s\n", radio_name_);
  printf("Frequency: %lu Hz\n",
      static_cast<unsigned long>(frequency_hz_));
  printf("Modulation: LoRa, SF%u, BW 125 kHz, CR 4/5\n",
      static_cast<unsigned int>(kSpreadingFactor));
  printf("Packet: explicit header, preamble 8, payload 64 bytes\n");
  printf("Payload CRC: on, IQ: standard, public sync word: 0x34\n");
  printf("Low data rate optimization: on, receiver gain: boosted\n");
  printf("Expected RF generator packet count: %lu\n",
      static_cast<unsigned long>(kExpectedPacketCount));
  printf("Pass criterion: PER <= %.1f%%\n", kPassingPerPercent);
  printf("RF generator data source: All 1\n");
  printf("Expected payload data:\n");
  for (size_t index = 0; index < kExpectedPayload.size(); ++index) {
    printf("%02X%s", static_cast<unsigned int>(kExpectedPayload[index]),
        ((index + 1) % 16 == 0) ? "\n" : " ");
  }
  printf("Receiver is ready. Transmit exactly %lu packets at each\n",
      static_cast<unsigned long>(kExpectedPacketCount));
  printf("input-power point; the first packet starts the test.\n");
  printf("Press BOOT once to clear statistics and restart the test.\n");
  printf("============================================================\n\n");
}

void TestSession::Start(uint32_t current_time_ms) {
  running_ = true;
  start_time_ms_ = current_time_ms;
  last_activity_time_ms_ = current_time_ms;
  last_progress_time_ms_ = current_time_ms;
  progress_timer_started_ = true;
  activity_seen_ = false;
  good_packets_ = 0;
  header_errors_ = 0;
  crc_errors_ = 0;
  length_errors_ = 0;
  payload_errors_ = 0;
  read_errors_ = 0;
  driver_errors_ = 0;
  packet_rssi_ = {};
  signal_rssi_ = {};
  snr_ = {};

  printf("\n[PER] RF packet stream detected, test started\n");
  printf("[PER] expecting exactly %lu packets\n",
      static_cast<unsigned long>(kExpectedPacketCount));
}

void TestSession::Finish(
    uint32_t current_time_ms, const char* reason) {
  if (!running_) {
    return;
  }
  running_ = false;
  last_progress_time_ms_ = current_time_ms;
  progress_timer_started_ = true;

  const uint32_t observed_bad = ObservedBadPackets();
  const uint32_t observed = good_packets_ + observed_bad;
  const uint32_t bounded_observed =
      std::min(observed, kExpectedPacketCount);
  const uint32_t missing_packets =
      kExpectedPacketCount - bounded_observed;
  const uint32_t bounded_good =
      std::min(good_packets_, kExpectedPacketCount);
  const uint32_t failed_packets =
      kExpectedPacketCount - bounded_good;
  const double per_percent =
      static_cast<double>(failed_packets) * 100.0 /
      static_cast<double>(kExpectedPacketCount);
  const uint32_t elapsed_ms = current_time_ms - start_time_ms_;

  printf("\n");
  printf("============================================================\n");
  printf("[PER] FINAL RESULT\n");
  printf("Radio: %s\n", radio_name_);
  printf("Finish reason: %s\n", reason);
  printf("Elapsed: %lu ms\n", static_cast<unsigned long>(elapsed_ms));
  printf("Expected packets: %lu\n",
      static_cast<unsigned long>(kExpectedPacketCount));
  printf("Correct packets: %lu\n",
      static_cast<unsigned long>(bounded_good));
  printf("Observed bad packets: %lu\n",
      static_cast<unsigned long>(
          std::min(observed_bad, kExpectedPacketCount)));
  printf("Unobserved/missing packets: %lu\n",
      static_cast<unsigned long>(missing_packets));
  printf("Header errors: %lu\n",
      static_cast<unsigned long>(header_errors_));
  printf("CRC errors: %lu\n",
      static_cast<unsigned long>(crc_errors_));
  printf("Length errors: %lu\n",
      static_cast<unsigned long>(length_errors_));
  printf("Payload content errors: %lu\n",
      static_cast<unsigned long>(payload_errors_));
  printf("Packet read errors: %lu\n",
      static_cast<unsigned long>(read_errors_));
  printf("Driver/transport errors: %lu\n",
      static_cast<unsigned long>(driver_errors_));
  printf("Failed packets: %lu\n",
      static_cast<unsigned long>(failed_packets));
  printf("PER: %.3f%%\n", per_percent);
  printf("Result at this input level: %s\n",
      per_percent <= kPassingPerPercent ? "PASS" : "FAIL");

  if (packet_rssi_.count > 0) {
    printf("Packet RSSI min/avg/max: %.2f / %.2f / %.2f dBm\n",
        packet_rssi_.minimum, packet_rssi_.Average(),
        packet_rssi_.maximum);
  }
  if (signal_rssi_.count > 0) {
    printf("Signal RSSI min/avg/max: %.2f / %.2f / %.2f dBm\n",
        signal_rssi_.minimum, signal_rssi_.Average(),
        signal_rssi_.maximum);
  }
  if (snr_.count > 0) {
    printf("SNR min/avg/max: %.2f / %.2f / %.2f dB\n",
        snr_.minimum, snr_.Average(), snr_.maximum);
  }

  printf("Sensitivity is the lowest calibrated DUT input level that\n");
  printf("still produces PER <= %.1f%%.\n", kPassingPerPercent);
  printf("The next RF packet stream starts a new input-power point.\n");
  printf("============================================================\n\n");
}

void TestSession::Poll(uint32_t current_time_ms) {
  if (!progress_timer_started_) {
    last_progress_time_ms_ = current_time_ms;
    progress_timer_started_ = true;
  }

  if (!running_) {
    if ((current_time_ms - last_progress_time_ms_) >=
        kProgressPrintIntervalMs) {
      printf("[PER] waiting for the first RF packet; receiver is running\n");
      last_progress_time_ms_ = current_time_ms;
    }
    return;
  }

  if (!activity_seen_) {
    return;
  }

  if ((current_time_ms - last_activity_time_ms_) >=
      kIdleFinishTimeMs) {
    Finish(current_time_ms, "RF packet stream ended");
    return;
  }

  if ((current_time_ms - last_progress_time_ms_) >=
      kProgressPrintIntervalMs) {
    PrintProgress(current_time_ms);
    last_progress_time_ms_ = current_time_ms;
  }
}

void TestSession::Restart(uint32_t current_time_ms) {
  running_ = false;
  start_time_ms_ = 0;
  last_activity_time_ms_ = current_time_ms;
  last_progress_time_ms_ = current_time_ms;
  progress_timer_started_ = true;
  activity_seen_ = false;
  good_packets_ = 0;
  header_errors_ = 0;
  crc_errors_ = 0;
  length_errors_ = 0;
  payload_errors_ = 0;
  read_errors_ = 0;
  driver_errors_ = 0;
  packet_rssi_ = {};
  signal_rssi_ = {};
  snr_ = {};

  printf("\n[PER] test restarted by BOOT button\n");
  printf("[PER] waiting for a new %lu-packet RF stream\n",
      static_cast<unsigned long>(kExpectedPacketCount));
}

void TestSession::RecordPacket(const uint8_t* payload,
    size_t payload_length, const PacketMetrics& metrics,
    uint32_t current_time_ms) {
  if (!running_) {
    Start(current_time_ms);
  }

  activity_seen_ = true;
  last_activity_time_ms_ = current_time_ms;
  if (payload == nullptr || payload_length != kExpectedPayload.size()) {
    ++payload_errors_;
  } else if (std::memcmp(
                 payload, kExpectedPayload.data(), kExpectedPayload.size()) !=
             0) {
    ++payload_errors_;
  } else {
    ++good_packets_;
    packet_rssi_.Add(metrics.packet_rssi_dbm);
    snr_.Add(metrics.snr_db);
    if (metrics.has_signal_rssi) {
      signal_rssi_.Add(metrics.signal_rssi_dbm);
    }
  }

  CheckForCompletion(current_time_ms);
}

void TestSession::RecordPacketError(
    PacketError error, uint32_t current_time_ms) {
  if (!running_) {
    Start(current_time_ms);
  }

  activity_seen_ = true;
  last_activity_time_ms_ = current_time_ms;
  switch (error) {
    case PacketError::kHeader:
      ++header_errors_;
      break;
    case PacketError::kCrc:
      ++crc_errors_;
      break;
    case PacketError::kLength:
      ++length_errors_;
      break;
    case PacketError::kRead:
      ++read_errors_;
      break;
  }

  const uint32_t observed = ObservedPackets();
  if (observed <= 10) {
    printf("[PER] observed %s error, packet event %lu/%lu\n",
        PacketErrorToString(error), static_cast<unsigned long>(observed),
        static_cast<unsigned long>(kExpectedPacketCount));
  }
  CheckForCompletion(current_time_ms);
}

void TestSession::RecordDriverError() {
  if (running_) {
    ++driver_errors_;
  }
}

void TestSession::PrintProgress(uint32_t current_time_ms) const {
  const uint32_t observed_bad = ObservedBadPackets();
  const uint32_t observed = ObservedPackets();
  const uint32_t remaining_packets =
      kExpectedPacketCount - std::min(observed, kExpectedPacketCount);
  const uint32_t elapsed_ms = current_time_ms - start_time_ms_;
  const double completion_percent =
      static_cast<double>(observed) * 100.0 /
      static_cast<double>(kExpectedPacketCount);
  const double observed_error_percent =
      observed == 0
          ? 0.0
          : static_cast<double>(observed_bad) * 100.0 /
                static_cast<double>(observed);

  printf("\n");
  printf("============================================================\n");
  printf("[PER] INTERIM RESULT\n");
  printf("Radio: %s\n", radio_name_);
  printf("Elapsed: %lu ms\n", static_cast<unsigned long>(elapsed_ms));
  printf("Expected packets: %lu\n",
      static_cast<unsigned long>(kExpectedPacketCount));
  printf("Observed packets: %lu\n",
      static_cast<unsigned long>(observed));
  printf("Remaining to expected count: %lu\n",
      static_cast<unsigned long>(remaining_packets));
  printf("Correct packets: %lu\n",
      static_cast<unsigned long>(good_packets_));
  printf("Observed bad packets: %lu\n",
      static_cast<unsigned long>(observed_bad));
  printf("Header errors: %lu\n",
      static_cast<unsigned long>(header_errors_));
  printf("CRC errors: %lu\n",
      static_cast<unsigned long>(crc_errors_));
  printf("Length errors: %lu\n",
      static_cast<unsigned long>(length_errors_));
  printf("Payload content errors: %lu\n",
      static_cast<unsigned long>(payload_errors_));
  printf("Packet read errors: %lu\n",
      static_cast<unsigned long>(read_errors_));
  printf("Driver/transport errors: %lu\n",
      static_cast<unsigned long>(driver_errors_));
  printf("Completion: %.2f%%\n", completion_percent);
  printf("Observed packet error ratio: %.3f%%\n",
      observed_error_percent);

  if (packet_rssi_.count > 0) {
    printf("Packet RSSI min/avg/max: %.2f / %.2f / %.2f dBm\n",
        packet_rssi_.minimum, packet_rssi_.Average(),
        packet_rssi_.maximum);
  }
  if (signal_rssi_.count > 0) {
    printf("Signal RSSI min/avg/max: %.2f / %.2f / %.2f dBm\n",
        signal_rssi_.minimum, signal_rssi_.Average(),
        signal_rssi_.maximum);
  }
  if (snr_.count > 0) {
    printf("SNR min/avg/max: %.2f / %.2f / %.2f dB\n",
        snr_.minimum, snr_.Average(), snr_.maximum);
  }

  printf("Final PER is calculated only after the test ends.\n");
  printf("============================================================\n\n");
}

void TestSession::CheckForCompletion(uint32_t current_time_ms) {
  if (running_ && ObservedPackets() >= kExpectedPacketCount) {
    Finish(current_time_ms, "expected packet events reached");
  }
}

uint32_t TestSession::ObservedBadPackets() const {
  return header_errors_ + crc_errors_ + length_errors_ +
         payload_errors_ + read_errors_;
}

uint32_t TestSession::ObservedPackets() const {
  return good_packets_ + ObservedBadPackets();
}

}  // namespace lora_rx_sensitivity
