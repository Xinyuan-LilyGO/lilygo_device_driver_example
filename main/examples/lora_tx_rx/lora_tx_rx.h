/*
 * @Description: LoRa 数据发送与接收测试入口声明
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-29 18:00:58
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <cstdint>

#include "lr20xx_radio_lora_types.h"

namespace lora_tx_rx {

// 常用LoRa载波频率预设
inline constexpr uint32_t kFrequency433MHz = 433000000U;
inline constexpr uint32_t kFrequency868MHz = 868000000U;
inline constexpr uint32_t kFrequency915MHz = 915000000U;
inline constexpr uint32_t kFrequency2400MHz = 2400000000U;
// LR2021、SX1262和LR1121统一使用的默认载波频率，修改此处即可切换
inline constexpr uint32_t kFrequencyHz = kFrequency433MHz;
static_assert(kFrequencyHz == kFrequency433MHz ||
        kFrequencyHz == kFrequency868MHz ||
        kFrequencyHz == kFrequency915MHz ||
        kFrequencyHz == kFrequency2400MHz,
    "Select one of the predefined LoRa frequencies");
// 2.4 GHz使用HF射频通路和203 kHz带宽，Sub-GHz使用LF通路和125 kHz
inline constexpr bool kUseHighFrequencyPath =
    kFrequencyHz == kFrequency2400MHz;
inline constexpr uint32_t kBandwidthKhz =
    kUseHighFrequencyPath ? 203U : 125U;
// LoRa公共网络同步字
inline constexpr uint8_t kSyncWord =
    LR20XX_RADIO_LORA_SYNCWORD_LORAWAN_PUBLIC_NETWORK;
// LoRa发送与接收测试载荷
inline constexpr std::array<uint8_t, 9> kTestPayload = {
    1, 2, 3, 4, 5, 6, 7, 8, 9};

void RunLr1121();
void RunLr2021();
void RunSx1262();

}  // namespace lora_tx_rx
