/*
 * @Description: GPS/GNSS NMEA 数据解析与日志输出的公共实现
 * @Author: LILYGO_L
 * @Date: 2026-07-29 00:22:40
 * @LastEditTime: 2026-07-29 00:22:40
 * @License: GPL 3.0
 */
#include "gps.h"

#include <cstdio>

namespace gps {
namespace {

bool IsUpdatedFloat(float value) { return value >= 0.0F; }

bool HasCoordinate(const GnssParser::Coordinate& coordinate) {
  return coordinate.update_flag || coordinate.direction_update_flag;
}

bool HasLocation(const GnssParser::Location& location) {
  return HasCoordinate(location.lat) || HasCoordinate(location.lon);
}

void PrintCoordinate(const char* source, const char* sentence,
    const char* name, const GnssParser::Coordinate& coordinate) {
  if (!HasCoordinate(coordinate)) {
    return;
  }

  const char* direction =
      coordinate.direction_update_flag ? coordinate.direction.c_str() : "-";
  double signed_degrees = coordinate.degrees_minutes;
  if (coordinate.direction_update_flag &&
      ((coordinate.direction == "S") || (coordinate.direction == "W"))) {
    signed_degrees = -signed_degrees;
  }

  if (coordinate.update_flag) {
    printf("[%s][%s] %s: %u deg %.10f min, decimal %.10lf, direction %s\n",
        source, sentence, name,
        static_cast<unsigned int>(coordinate.degrees), coordinate.minutes,
        signed_degrees, direction);
  } else {
    printf(
        "[%s][%s] %s direction: %s\n", source, sentence, name, direction);
  }
}

void PrintLocation(const char* source, const char* sentence,
    const GnssParser::Location& location) {
  PrintCoordinate(source, sentence, "latitude", location.lat);
  PrintCoordinate(source, sentence, "longitude", location.lon);
}

void PrintUtc(const char* source, const char* sentence,
    const GnssParser::Utc& utc) {
  if (!utc.update_flag) {
    return;
  }

  printf("[%s][%s] UTC: %02u:%02u:%06.3f\n", source, sentence,
      static_cast<unsigned int>(utc.hour),
      static_cast<unsigned int>(utc.minute), utc.second);

  const unsigned int local_hour_total =
      static_cast<unsigned int>(utc.hour) + 8U;
  printf("[%s][%s] UTC+08:00: %02u:%02u:%06.3f (day +%u)\n", source,
      sentence, local_hour_total % 24U,
      static_cast<unsigned int>(utc.minute), utc.second,
      local_hour_total / 24U);
}

void PrintDate(const char* source, const char* sentence,
    const GnssParser::Date& date, bool two_digit_year) {
  if (!date.update_flag) {
    return;
  }

  unsigned int year = static_cast<unsigned int>(date.year);
  if (two_digit_year && (year < 100U)) {
    year += 2000U;
  }
  printf("[%s][%s] date: %04u-%02u-%02u\n", source, sentence, year,
      static_cast<unsigned int>(date.month),
      static_cast<unsigned int>(date.day));
}

bool HasRmcData(const GnssParser::Rmc& rmc) {
  return rmc.utc.update_flag || rmc.location_status_update_flag ||
      HasLocation(rmc.location) ||
      IsUpdatedFloat(rmc.speed_over_ground_knots) ||
      IsUpdatedFloat(rmc.course_over_ground_degree) || rmc.data.update_flag ||
      IsUpdatedFloat(rmc.magnetic_variation) ||
      !rmc.magnetic_variation_direction.empty() ||
      !rmc.mode_indicator.empty() || !rmc.navigational_status.empty();
}

void PrintRmc(const char* source, const GnssParser::Rmc& rmc) {
  if (!HasRmcData(rmc)) {
    return;
  }

  printf("\n[%s][RMC] recommended minimum navigation data\n", source);
  if (rmc.location_status_update_flag) {
    printf("[%s][RMC] status: %s\n", source, rmc.location_status.c_str());
  }
  PrintUtc(source, "RMC", rmc.utc);
  PrintDate(source, "RMC", rmc.data, true);
  PrintLocation(source, "RMC", rmc.location);
  if (IsUpdatedFloat(rmc.speed_over_ground_knots)) {
    printf("[%s][RMC] speed over ground: %.3f kn, %.3f km/h\n", source,
        rmc.speed_over_ground_knots,
        rmc.speed_over_ground_knots * 1.852F);
  }
  if (IsUpdatedFloat(rmc.course_over_ground_degree)) {
    printf("[%s][RMC] course over ground: %.3f deg\n", source,
        rmc.course_over_ground_degree);
  }
  if (IsUpdatedFloat(rmc.magnetic_variation) ||
      !rmc.magnetic_variation_direction.empty()) {
    printf("[%s][RMC] magnetic variation: %.3f deg %s\n", source,
        rmc.magnetic_variation, rmc.magnetic_variation_direction.c_str());
  }
  if (!rmc.mode_indicator.empty()) {
    printf(
        "[%s][RMC] mode indicator: %s\n", source, rmc.mode_indicator.c_str());
  }
  if (!rmc.navigational_status.empty()) {
    printf("[%s][RMC] navigational status: %s\n", source,
        rmc.navigational_status.c_str());
  }
}

bool HasGgaData(const GnssParser::Gga& gga) {
  return gga.utc.update_flag || HasLocation(gga.location) ||
      (gga.gps_mode_status != 0xFFU) ||
      (gga.online_satellite_count != 0xFFU) ||
      IsUpdatedFloat(gga.hdop) || !gga.altitude_unit.empty() ||
      !gga.geoid_separation_unit.empty() ||
      IsUpdatedFloat(gga.differential_age) ||
      !gga.differential_station_id.empty();
}

void PrintGga(const char* source, const GnssParser::Gga& gga) {
  if (!HasGgaData(gga)) {
    return;
  }

  printf("\n[%s][GGA] fix data\n", source);
  PrintUtc(source, "GGA", gga.utc);
  PrintLocation(source, "GGA", gga.location);
  if (gga.gps_mode_status != 0xFFU) {
    printf("[%s][GGA] fix quality: %u\n", source,
        static_cast<unsigned int>(gga.gps_mode_status));
  }
  if (gga.online_satellite_count != 0xFFU) {
    printf("[%s][GGA] satellites used: %u\n", source,
        static_cast<unsigned int>(gga.online_satellite_count));
  }
  if (IsUpdatedFloat(gga.hdop)) {
    printf("[%s][GGA] HDOP: %.3f\n", source, gga.hdop);
  }
  if (!gga.altitude_unit.empty()) {
    printf("[%s][GGA] altitude: %.3f %s\n", source, gga.altitude,
        gga.altitude_unit.c_str());
  }
  if (!gga.geoid_separation_unit.empty()) {
    printf("[%s][GGA] geoid separation: %.3f %s\n", source,
        gga.geoid_separation, gga.geoid_separation_unit.c_str());
  }
  if (IsUpdatedFloat(gga.differential_age)) {
    printf("[%s][GGA] differential age: %.3f s\n", source,
        gga.differential_age);
  }
  if (!gga.differential_station_id.empty()) {
    printf("[%s][GGA] differential station ID: %s\n", source,
        gga.differential_station_id.c_str());
  }
}

void PrintGsv(const char* source, const GnssParser::Gsv& gsv) {
  if (!gsv.update_flag) {
    return;
  }

  printf("\n[%s][GSV] satellites in view\n", source);
  printf("[%s][GSV] talker: %s, sentence: %u/%u, satellites in view: %u",
      source, gsv.talker_id.c_str(),
      static_cast<unsigned int>(gsv.sentence_number),
      static_cast<unsigned int>(gsv.total_sentence_count),
      static_cast<unsigned int>(gsv.total_satellite_count));
  if (gsv.signal_id != 0xFFU) {
    printf(", signal ID: %u", static_cast<unsigned int>(gsv.signal_id));
  }
  printf("\n");

  for (const auto& satellite : gsv.satellites) {
    if (satellite.id != 0xFFFFU) {
      printf("[%s][GSV] SV %u", source,
          static_cast<unsigned int>(satellite.id));
    } else {
      printf("[%s][GSV] SV unknown", source);
    }
    if (!satellite.talker_id.empty()) {
      printf(", talker %s", satellite.talker_id.c_str());
    }
    if (satellite.elevation >= 0) {
      printf(", elevation %d deg", static_cast<int>(satellite.elevation));
    }
    if (satellite.azimuth >= 0) {
      printf(", azimuth %d deg", static_cast<int>(satellite.azimuth));
    }
    if (satellite.cn0 >= 0) {
      printf(", C/N0 %d dB-Hz", static_cast<int>(satellite.cn0));
    }
    if (satellite.signal_id != 0xFFU) {
      printf(", signal ID %u",
          static_cast<unsigned int>(satellite.signal_id));
    }
    printf("\n");
  }
}

void PrintGsa(const char* source, const GnssParser::Gsa& gsa) {
  if (!gsa.update_flag) {
    return;
  }

  printf("\n[%s][GSA] active satellites and DOP\n", source);
  for (const auto& sentence : gsa.sentences) {
    printf("[%s][GSA] talker: %s, selection: %s", source,
        sentence.talker_id.c_str(), sentence.selection_mode.c_str());
    if (sentence.fix_mode != 0xFFU) {
      printf(", fix mode: %u",
          static_cast<unsigned int>(sentence.fix_mode));
    }
    if (sentence.system_id != 0xFFU) {
      printf(", system ID: %u",
          static_cast<unsigned int>(sentence.system_id));
    }
    printf("\n[%s][GSA] satellites used:", source);
    if (sentence.satellite_ids.empty()) {
      printf(" none");
    } else {
      for (uint16_t satellite_id : sentence.satellite_ids) {
        printf(" %u", static_cast<unsigned int>(satellite_id));
      }
    }
    printf("\n");

    if (IsUpdatedFloat(sentence.pdop)) {
      printf("[%s][GSA] PDOP: %.3f\n", source, sentence.pdop);
    }
    if (IsUpdatedFloat(sentence.hdop)) {
      printf("[%s][GSA] HDOP: %.3f\n", source, sentence.hdop);
    }
    if (IsUpdatedFloat(sentence.vdop)) {
      printf("[%s][GSA] VDOP: %.3f\n", source, sentence.vdop);
    }
  }
}

void PrintVtg(const char* source, const GnssParser::Vtg& vtg) {
  if (!vtg.update_flag) {
    return;
  }

  printf("\n[%s][VTG] course and ground speed\n", source);
  if (IsUpdatedFloat(vtg.course_true_degree)) {
    printf("[%s][VTG] true course: %.3f deg\n", source,
        vtg.course_true_degree);
  }
  if (IsUpdatedFloat(vtg.course_magnetic_degree)) {
    printf("[%s][VTG] magnetic course: %.3f deg\n", source,
        vtg.course_magnetic_degree);
  }
  if (IsUpdatedFloat(vtg.speed_knots)) {
    printf("[%s][VTG] speed: %.3f kn\n", source, vtg.speed_knots);
  }
  if (IsUpdatedFloat(vtg.speed_kmh)) {
    printf("[%s][VTG] speed: %.3f km/h\n", source, vtg.speed_kmh);
  }
  if (!vtg.mode_indicator.empty()) {
    printf(
        "[%s][VTG] mode indicator: %s\n", source, vtg.mode_indicator.c_str());
  }
}

void PrintGll(const char* source, const GnssParser::Gll& gll) {
  if (!gll.update_flag) {
    return;
  }

  printf("\n[%s][GLL] geographic position\n", source);
  PrintLocation(source, "GLL", gll.location);
  PrintUtc(source, "GLL", gll.utc);
  if (!gll.location_status.empty()) {
    printf(
        "[%s][GLL] status: %s\n", source, gll.location_status.c_str());
  }
  if (!gll.mode_indicator.empty()) {
    printf(
        "[%s][GLL] mode indicator: %s\n", source, gll.mode_indicator.c_str());
  }
}

void PrintTxt(const char* source, const GnssParser::Txt& txt) {
  if (!txt.update_flag) {
    return;
  }

  printf("\n[%s][TXT] receiver text messages\n", source);
  for (const auto& sentence : txt.sentences) {
    printf("[%s][TXT] %u/%u, text ID %u: %s\n", source,
        static_cast<unsigned int>(sentence.sentence_number),
        static_cast<unsigned int>(sentence.total_sentence_count),
        static_cast<unsigned int>(sentence.text_id), sentence.text.c_str());
  }
}

void PrintZda(const char* source, const GnssParser::Zda& zda) {
  if (!zda.update_flag) {
    return;
  }

  printf("\n[%s][ZDA] date and time\n", source);
  PrintUtc(source, "ZDA", zda.utc);
  PrintDate(source, "ZDA", zda.date, false);
  printf("[%s][ZDA] local zone: %+d:%02d\n", source,
      static_cast<int>(zda.local_hour), static_cast<int>(zda.local_minute));
}

}  // namespace

void PrintGnssInfo(const char* source, const GnssParser::Info& info) {
  PrintRmc(source, info.rmc);
  PrintGga(source, info.gga);
  PrintGsv(source, info.gsv);
  PrintGsa(source, info.gsa);
  PrintVtg(source, info.vtg);
  PrintGll(source, info.gll);
  PrintTxt(source, info.txt);
  PrintZda(source, info.zda);
}

void PrintRawBlock(
    const char* source, const uint8_t* data, size_t length) {
  printf("\n[%s][RAW] begin (%u bytes)\n", source,
      static_cast<unsigned int>(length));
  if ((data != nullptr) && (length > 0)) {
    fwrite(data, 1, length, stdout);
    if (data[length - 1] != '\n') {
      printf("\n");
    }
  }
  printf("[%s][RAW] end\n", source);
}

}  // namespace gps
