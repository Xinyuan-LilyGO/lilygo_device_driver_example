/*
 * @Description: 电池管理示例的芯片测试入口声明
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#pragma once

// 示例日志同时输出到串口和 LVGL；仅从 app_main 所在任务调用。
void BatteryLogPrintf(const char* format, ...)
    __attribute__((format(printf, 1, 2)));

// 每轮采样统一更新屏幕，保留启动信息，避免历史日志无限增长。
void BatteryLogBeginSnapshot();
void BatteryLogEndSnapshot();

void RunBq27220Example();
void RunAxp517Example();
