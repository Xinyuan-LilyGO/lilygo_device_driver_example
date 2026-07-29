/*
 * @Description: 网络 MP3 音频输出适配接口声明
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

bool InitWifiMp3Audio();
bool WriteWifiMp3Pcm(const void* data, size_t byte_count);
