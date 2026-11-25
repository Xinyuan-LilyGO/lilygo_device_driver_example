/*
 * @Description: 电池管理示例的芯片测试入口声明
 */
#pragma once

// 示例日志输出到串口，T-Display-P4 和 T-Display-P4-Air 同步显示到屏幕。
void BatteryLogPrintf(const char* format, ...)
    __attribute__((format(printf, 1, 2)));

// 屏幕日志按采样轮次更新，并保留启动信息。
void BatteryLogBeginSnapshot(bool keyboard = false);
void BatteryLogEndSnapshot();
void BatteryLogResetScroll(bool keyboard);

void RunBq27220Example();
void RunAxp517Example();
void RunBq25896Example();
