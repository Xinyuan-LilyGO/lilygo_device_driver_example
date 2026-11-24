/*
 * @Description: SD 卡 MP3 音频输出适配接口声明
 */
#pragma once
#include <cstddef>
#include <cstdint>

bool InitSdMp3Audio();
void DeinitSdMp3Audio();
bool ConfigureSdMp3Audio(uint32_t sample_rate);
bool WriteSdMp3Pcm(const void* data, size_t byte_count);
