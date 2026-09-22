/*
 * @Description: 电池管理示例的芯片测试入口声明
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-22 15:11:24
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

// T-Display-P4 v2.0 外置电池测试电流：改这里即可，须为 64 mA 的倍数。
// PD 已建立、未建立或失败时均使用此目标；实际电流仍受输入功率和温控限制。
// 提高前须确认电芯和连接线允许的最大充电电流。
inline constexpr uint16_t kExternalChargeCurrentMa = 1024;
static_assert(kExternalChargeCurrentMa >= 64 &&
                  kExternalChargeCurrentMa <= 5120 &&
                  kExternalChargeCurrentMa % 64 == 0,
              "AXP517 charge current must be 64-5120 mA in 64 mA steps");

// 示例日志同时输出到串口和 LVGL；仅从 app_main 所在任务调用。
void BatteryLogPrintf(const char* format, ...)
    __attribute__((format(printf, 1, 2)));

// 每轮采样统一更新屏幕，保留启动信息，避免历史日志无限增长。
void BatteryLogBeginSnapshot();
void BatteryLogEndSnapshot();

void RunBq27220Example();
void RunAxp517Example();
