/*
 * @Description: 网络 MP3 音频输出适配接口声明
 */
#pragma once
#include <cstddef>

bool InitWifiMp3Audio();
bool WriteWifiMp3Pcm(const void* data, size_t byte_count);
