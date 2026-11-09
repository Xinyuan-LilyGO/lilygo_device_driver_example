/*
 * @Description: RTC 示例调用接口
 * @Author: LILYGO_L
 * @Date: 2026-09-10 14:55:31
 * @LastEditTime: 2026-11-09 10:22:56
 */
#pragma once

// 轮询 BOOT 约一秒，消抖后的按下事件返回 true。
bool WaitForRtcReset();
void RunPcf8563RtcExample();
void RunRx8130ceRtcExample();
void RunInternalRtcExample();
