/*
 * @Description: 电池管理示例的芯片测试入口声明
 */
#pragma once

// 示例日志同时输出到串口和 LVGL；仅从 app_main 所在任务调用。
void BatteryLogPrintf(const char* format, ...)
    __attribute__((format(printf, 1, 2)));

// 每轮采样统一更新屏幕，保留启动信息，避免历史日志无限增长。
void BatteryLogBeginSnapshot(bool keyboard = false);
void BatteryLogEndSnapshot();
void BatteryLogResetScroll(bool keyboard);

void RunBq27220Example();
void RunAxp517Example();
