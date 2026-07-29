/*
 * @Description: GPS/GNSS 定位示例的公共接口声明
 * @Author: LILYGO_L
 * @Date: 2026-07-29 00:22:40
 * @LastEditTime: 2026-07-29 00:22:40
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "lilygo_device_driver_library.h"

namespace gps {

using GnssParser = cpp_bus_driver::GnssParser;

void PrintGnssInfo(const char* source, const GnssParser::Info& info);
void PrintRawBlock(
    const char* source, const uint8_t* data, size_t length);

void RunL76k();
void RunNrf9151();

}  // namespace gps
