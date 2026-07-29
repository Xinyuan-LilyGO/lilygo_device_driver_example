/*
 * @Description: SD 卡 MP3 音频输出适配接口声明
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

bool InitSdMp3Audio();
bool ConfigureSdMp3Audio(uint32_t sample_rate);
bool WriteSdMp3Pcm(const void* data, size_t byte_count);
