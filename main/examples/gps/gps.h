/*
 * @Description: GPS/GNSS 定位示例的公共接口声明
 */
#pragma once
#include <cstddef>
#include <cstdint>

#include "lilygo_device_driver.h"

namespace gps {

using NmeaParser = cpp_bus_driver::NmeaParser;

void PrintNmeaUpdate(const char* source, const NmeaParser::Update& update);

void PrintNmeaDiagnostics(
    const char* source, const NmeaParser::FeedResult& result);
void PrintRawBlock(const char* source, const uint8_t* data, size_t length);

void RunL76k();
void RunNrf9151();

}  // namespace gps
