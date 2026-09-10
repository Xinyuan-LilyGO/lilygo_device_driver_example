#pragma once

// 轮询 BOOT 约一秒，消抖后的按下事件返回 true。
bool WaitForRtcReset();
void RunPcf8563RtcExample();
void RunInternalRtcExample();
