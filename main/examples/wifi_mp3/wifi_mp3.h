/*
 * @Description: 网络 MP3 音频输出适配接口声明
 */
#pragma once
#include <cstddef>

#include "esp_audio_dec.h"

bool InitWifiMp3Audio(const esp_audio_dec_info_t& stream_info);
void DeinitWifiMp3Audio();
bool WriteWifiMp3Pcm(const void* data, size_t byte_count);
