/*
 * @Description: LoRa 数据发送与接收测试入口声明
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <cstdint>

namespace lora_send_receive {

inline constexpr std::array<uint8_t, 9> kTestPayload = {
    1, 2, 3, 4, 5, 6, 7, 8, 9};

void RunLr1121();
void RunLr2021();
void RunSx1262();

}  // namespace lora_send_receive
