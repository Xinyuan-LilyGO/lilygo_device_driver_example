/*
 * @Description: GPS/GNSS 定位信息输出实现
 * @Author: LILYGO_L
 * @Date: 2026-07-29 00:22:40
 * @LastEditTime: 2026-09-04 17:32:00
 * @License: GPL 3.0
 */
#include "gps.h"

#include <cstdio>

namespace gps {
namespace {

void PrintHeading(const char* source, const char* name) {
  printf("\n[%s][%s]\n", source, name);
}

void PrintUtc(const char* source, const char* sentence,
    const NmeaParser::UtcTime& utc) {
  if (utc.valid) {
    printf("[%s][%s] UTC: %02u:%02u:%06.3f\n", source, sentence,
        static_cast<unsigned int>(utc.hour),
        static_cast<unsigned int>(utc.minute), utc.second);
  }
}

void PrintDate(const char* source, const char* sentence,
    const NmeaParser::UtcDate& date) {
  if (!date.valid) {
    return;
  }
  if (date.two_digit_year) {
    printf("[%s][%s] UTC date: %02u-%02u-%02u (two-digit NMEA year)\n",
        source, sentence, static_cast<unsigned int>(date.year),
        static_cast<unsigned int>(date.month),
        static_cast<unsigned int>(date.day));
  } else {
    printf("[%s][%s] UTC date: %04u-%02u-%02u\n", source, sentence,
        static_cast<unsigned int>(date.year),
        static_cast<unsigned int>(date.month),
        static_cast<unsigned int>(date.day));
  }
}

void PrintCoordinate(const char* source, const char* sentence,
    const char* name, const NmeaParser::Coordinate& coordinate) {
  if (!coordinate.valid) {
    return;
  }
  printf("[%s][%s] %s: %.10lf deg (%u deg %.7lf min %c)\n", source,
      sentence, name, coordinate.decimal_degrees,
      static_cast<unsigned int>(coordinate.degrees), coordinate.minutes,
      coordinate.direction);
}

void PrintPosition(const char* source, const char* sentence,
    const NmeaParser::Position& position) {
  PrintCoordinate(source, sentence, "latitude", position.latitude);
  PrintCoordinate(source, sentence, "longitude", position.longitude);
}

template <typename T>
void PrintUnsigned(const char* source, const char* sentence, const char* name,
    const NmeaParser::OptionalValue<T>& field) {
  if (field.valid) {
    printf("[%s][%s] %s: %u\n", source, sentence, name,
        static_cast<unsigned int>(field.value));
  }
}

void PrintFloat(const char* source, const char* sentence, const char* name,
    const NmeaParser::OptionalValue<float>& field, const char* unit = "") {
  if (field.valid) {
    printf("[%s][%s] %s: %.3f%s\n", source, sentence, name, field.value,
        unit);
  }
}

void PrintDouble(const char* source, const char* sentence, const char* name,
    const NmeaParser::OptionalValue<double>& field, const char* unit = "") {
  if (field.valid) {
    printf("[%s][%s] %s: %.6lf%s\n", source, sentence, name, field.value,
        unit);
  }
}

void PrintCharacter(const char* source, const char* sentence,
    const char* name, const NmeaParser::OptionalValue<char>& field) {
  if (field.valid) {
    printf("[%s][%s] %s: %c\n", source, sentence, name, field.value);
  }
}

void PrintString(const char* source, const char* sentence, const char* name,
    const NmeaParser::OptionalValue<NmeaParser::Text>& field) {
  if (field.valid) {
    printf("[%s][%s] %s: %s\n", source, sentence, name,
        field.value.c_str());
  }
}

void PrintDtm(const char* source, const NmeaParser::Dtm& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "DTM datum reference");
  printf("[%s][DTM] local datum: %s, subdivision: %s, reference: %s\n",
      source, value.local_datum.c_str(),
      value.local_datum_subdivision.c_str(), value.reference_datum.c_str());
  PrintDouble(source, "DTM", "latitude offset", value.latitude_offset_minutes,
      " min");
  PrintCharacter(
      source, "DTM", "latitude direction", value.latitude_direction);
  PrintDouble(source, "DTM", "longitude offset",
      value.longitude_offset_minutes, " min");
  PrintCharacter(
      source, "DTM", "longitude direction", value.longitude_direction);
  PrintDouble(source, "DTM", "altitude offset", value.altitude_offset_meters,
      " m");
}

void PrintGbs(const char* source, const NmeaParser::Gbs& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "GBS RAIM fault detection");
  PrintUtc(source, "GBS", value.utc);
  PrintDouble(source, "GBS", "latitude expected error",
      value.latitude_error_meters, " m");
  PrintDouble(source, "GBS", "longitude expected error",
      value.longitude_error_meters, " m");
  PrintDouble(source, "GBS", "altitude expected error",
      value.altitude_error_meters, " m");
  PrintUnsigned(
      source, "GBS", "failed satellite", value.failed_satellite_id);
  PrintDouble(source, "GBS", "missed detection probability",
      value.missed_detection_probability);
  PrintDouble(source, "GBS", "estimated bias", value.estimated_bias_meters,
      " m");
  PrintDouble(source, "GBS", "bias standard deviation",
      value.bias_standard_deviation_meters, " m");
  PrintUnsigned(source, "GBS", "system ID", value.system_id);
  PrintUnsigned(source, "GBS", "signal ID", value.signal_id);
}

void PrintGga(const char* source, const NmeaParser::Gga& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "GGA fix data");
  PrintUtc(source, "GGA", value.utc);
  PrintPosition(source, "GGA", value.position);
  PrintUnsigned(source, "GGA", "fix quality", value.fix_quality);
  PrintUnsigned(source, "GGA", "satellites used", value.satellites_used);
  PrintFloat(source, "GGA", "HDOP", value.hdop);
  PrintDouble(source, "GGA", "altitude", value.altitude_meters, " m");
  PrintDouble(source, "GGA", "geoid separation",
      value.geoid_separation_meters, " m");
  PrintDouble(source, "GGA", "differential age",
      value.differential_age_seconds, " s");
  PrintString(source, "GGA", "differential station ID",
      value.differential_station_id);
}

void PrintGll(const char* source, const NmeaParser::Gll& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "GLL geographic position");
  PrintPosition(source, "GLL", value.position);
  PrintUtc(source, "GLL", value.utc);
  PrintCharacter(source, "GLL", "data status", value.data_status);
  PrintCharacter(
      source, "GLL", "positioning mode", value.positioning_mode);
}

void PrintGns(const char* source, const NmeaParser::Gns& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "GNS multi-GNSS fix data");
  PrintUtc(source, "GNS", value.utc);
  PrintPosition(source, "GNS", value.position);
  PrintString(source, "GNS", "positioning modes", value.positioning_modes);
  PrintUnsigned(source, "GNS", "satellites used", value.satellites_used);
  PrintFloat(source, "GNS", "HDOP", value.hdop);
  PrintDouble(source, "GNS", "altitude", value.altitude_meters, " m");
  PrintDouble(source, "GNS", "geoid separation",
      value.geoid_separation_meters, " m");
  PrintDouble(source, "GNS", "differential age",
      value.differential_age_seconds, " s");
  PrintString(source, "GNS", "differential station ID",
      value.differential_station_id);
  PrintCharacter(source, "GNS", "navigational status",
      value.navigational_status);
}

void PrintGrs(const char* source, const NmeaParser::Grs& value) {
  PrintHeading(source, "GRS range residuals");
  PrintUtc(source, "GRS", value.utc);
  PrintUnsigned(source, "GRS", "residual mode", value.residual_mode);
  for (size_t index = 0; index < value.residuals_meters.size(); ++index) {
    if (value.residuals_meters[index].valid) {
      printf("[%s][GRS] residual[%u]: %.3f m\n", source,
          static_cast<unsigned int>(index),
          value.residuals_meters[index].value);
    }
  }
  PrintUnsigned(source, "GRS", "system ID", value.system_id);
  PrintUnsigned(source, "GRS", "signal ID", value.signal_id);
}

void PrintGsa(const char* source, const NmeaParser::Gsa& value) {
  PrintHeading(source, "GSA active satellites and DOP");
  printf("[%s][GSA] talker: %s\n", source,
      value.metadata.talker_id.c_str());
  PrintCharacter(source, "GSA", "operation mode", value.operation_mode);
  PrintUnsigned(source, "GSA", "navigation mode", value.navigation_mode);
  printf("[%s][GSA] satellites used:", source);
  for (uint16_t satellite_id : value.satellite_ids) {
    printf(" %u", static_cast<unsigned int>(satellite_id));
  }
  printf("\n");
  PrintFloat(source, "GSA", "PDOP", value.pdop);
  PrintFloat(source, "GSA", "HDOP", value.hdop);
  PrintFloat(source, "GSA", "VDOP", value.vdop);
  PrintUnsigned(source, "GSA", "system ID", value.system_id);
}

void PrintGst(const char* source, const NmeaParser::Gst& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "GST pseudorange error statistics");
  PrintUtc(source, "GST", value.utc);
  PrintDouble(source, "GST", "range RMS", value.range_rms_meters, " m");
  PrintDouble(source, "GST", "semi-major deviation",
      value.semi_major_deviation_meters, " m");
  PrintDouble(source, "GST", "semi-minor deviation",
      value.semi_minor_deviation_meters, " m");
  PrintDouble(source, "GST", "semi-major orientation",
      value.semi_major_orientation_degrees, " deg");
  PrintDouble(source, "GST", "latitude deviation",
      value.latitude_deviation_meters, " m");
  PrintDouble(source, "GST", "longitude deviation",
      value.longitude_deviation_meters, " m");
  PrintDouble(source, "GST", "altitude deviation",
      value.altitude_deviation_meters, " m");
}

void PrintGsv(const char* source, const NmeaParser::Gsv& value) {
  PrintHeading(source, "GSV satellites in view");
  printf("[%s][GSV] talker: %s, messages: %u, satellites in view: %u",
      source, value.metadata.talker_id.c_str(),
      static_cast<unsigned int>(value.total_sentence_count),
      static_cast<unsigned int>(value.total_satellite_count));
  if (value.signal_id.valid) {
    printf(", signal ID: %u", static_cast<unsigned int>(value.signal_id.value));
  }
  printf("\n");
  for (const auto& satellite : value.satellites) {
    printf("[%s][GSV] satellite", source);
    if (satellite.id.valid) {
      printf(" %u", static_cast<unsigned int>(satellite.id.value));
    }
    if (satellite.elevation_degrees.valid) {
      printf(", elevation %d deg",
          static_cast<int>(satellite.elevation_degrees.value));
    }
    if (satellite.azimuth_degrees.valid) {
      printf(", azimuth %u deg",
          static_cast<unsigned int>(satellite.azimuth_degrees.value));
    }
    if (satellite.carrier_to_noise_db_hz.valid) {
      printf(", C/N0 %u dB-Hz", static_cast<unsigned int>(
          satellite.carrier_to_noise_db_hz.value));
    }
    printf("\n");
  }
}

void PrintRlm(const char* source, const NmeaParser::Rlm& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "RLM return link message");
  printf("[%s][RLM] beacon ID: %s\n", source, value.beacon_id.c_str());
  PrintUtc(source, "RLM", value.utc);
  PrintCharacter(source, "RLM", "message code", value.message_code);
  printf("[%s][RLM] message body: %s\n", source,
      value.message_body.c_str());
}

void PrintRmc(const char* source, const NmeaParser::Rmc& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "RMC recommended minimum navigation data");
  PrintUtc(source, "RMC", value.utc);
  PrintDate(source, "RMC", value.date);
  PrintCharacter(source, "RMC", "data status", value.data_status);
  PrintPosition(source, "RMC", value.position);
  PrintFloat(source, "RMC", "speed over ground",
      value.speed_over_ground_knots, " kn");
  PrintFloat(source, "RMC", "course over ground",
      value.course_over_ground_degrees, " deg");
  PrintFloat(source, "RMC", "magnetic variation",
      value.magnetic_variation_degrees, " deg");
  PrintCharacter(source, "RMC", "magnetic variation direction",
      value.magnetic_variation_direction);
  PrintCharacter(
      source, "RMC", "positioning mode", value.positioning_mode);
  PrintCharacter(source, "RMC", "navigational status",
      value.navigational_status);
}

void PrintTxt(const char* source, const NmeaParser::Update& update,
    const NmeaParser::Txt& value) {
  PrintHeading(source, "TXT receiver text");
  for (size_t index = 0; index < value.messages.size(); ++index) {
    printf("[%s][TXT] %u/%u", source,
        static_cast<unsigned int>(index + 1),
        static_cast<unsigned int>(value.total_sentence_count));
    if (value.text_id.valid) {
      printf(", text ID %u", static_cast<unsigned int>(value.text_id.value));
    }
    const char* message = update.GetTextMessage(value, index);
    printf(": %s\n", message != nullptr ? message : "[invalid text range]");
  }
}

void PrintVlw(const char* source, const NmeaParser::Vlw& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "VLW distance traveled");
  PrintDouble(source, "VLW", "total water distance",
      value.total_water_distance_nautical_miles, " nmi");
  PrintDouble(source, "VLW", "water distance since reset",
      value.water_distance_nautical_miles, " nmi");
  PrintDouble(source, "VLW", "total ground distance",
      value.total_ground_distance_nautical_miles, " nmi");
  PrintDouble(source, "VLW", "ground distance since reset",
      value.ground_distance_nautical_miles, " nmi");
}

void PrintVtg(const char* source, const NmeaParser::Vtg& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "VTG course and ground speed");
  PrintFloat(source, "VTG", "true course", value.true_course_degrees,
      " deg");
  PrintFloat(source, "VTG", "magnetic course",
      value.magnetic_course_degrees, " deg");
  PrintFloat(source, "VTG", "speed", value.speed_knots, " kn");
  PrintFloat(source, "VTG", "speed", value.speed_kilometers_per_hour,
      " km/h");
  PrintCharacter(
      source, "VTG", "positioning mode", value.positioning_mode);
}

void PrintZda(const char* source, const NmeaParser::Zda& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "ZDA date and time");
  PrintUtc(source, "ZDA", value.utc);
  PrintDate(source, "ZDA", value.date);
  if (value.local_zone_hours.valid && value.local_zone_minutes.valid) {
    printf("[%s][ZDA] local zone: %+d:%02u\n", source,
        static_cast<int>(value.local_zone_hours.value),
        static_cast<unsigned int>(value.local_zone_minutes.value));
  }
}

void PrintPubxPosition(
    const char* source, const NmeaParser::PubxPosition& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "PUBX,00 position");
  PrintUtc(source, "PUBX,00", value.utc);
  PrintPosition(source, "PUBX,00", value.position);
  PrintDouble(source, "PUBX,00", "altitude above user datum",
      value.altitude_above_user_datum_meters, " m");
  PrintString(
      source, "PUBX,00", "navigation status", value.navigation_status);
  PrintFloat(source, "PUBX,00", "horizontal accuracy",
      value.horizontal_accuracy_meters, " m");
  PrintFloat(source, "PUBX,00", "vertical accuracy",
      value.vertical_accuracy_meters, " m");
  PrintFloat(source, "PUBX,00", "speed",
      value.speed_kilometers_per_hour, " km/h");
  PrintFloat(source, "PUBX,00", "course over ground",
      value.course_over_ground_degrees, " deg");
  PrintFloat(source, "PUBX,00", "vertical velocity",
      value.vertical_velocity_meters_per_second, " m/s (positive down)");
  PrintFloat(source, "PUBX,00", "differential age",
      value.differential_age_seconds, " s");
  PrintFloat(source, "PUBX,00", "HDOP", value.hdop);
  PrintFloat(source, "PUBX,00", "VDOP", value.vdop);
  PrintFloat(source, "PUBX,00", "TDOP", value.tdop);
  PrintUnsigned(
      source, "PUBX,00", "satellites used", value.satellites_used);
  if (value.dead_reckoning_used.valid) {
    printf("[%s][PUBX,00] dead reckoning used: %s\n", source,
        value.dead_reckoning_used.value ? "yes" : "no");
  }
}

void PrintPubxSatelliteStatus(
    const char* source, const NmeaParser::PubxSatelliteStatus& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "PUBX,03 satellite status");
  printf("[%s][PUBX,03] satellites: %u\n", source,
      static_cast<unsigned int>(value.reported_satellite_count));
  for (const auto& satellite : value.satellites) {
    printf("[%s][PUBX,03] satellite", source);
    if (satellite.id.valid) {
      printf(" %u", static_cast<unsigned int>(satellite.id.value));
    }
    if (satellite.status.valid) {
      printf(", status %c", satellite.status.value);
    }
    if (satellite.azimuth_degrees.valid) {
      printf(", azimuth %u deg",
          static_cast<unsigned int>(satellite.azimuth_degrees.value));
    }
    if (satellite.elevation_degrees.valid) {
      printf(", elevation %d deg",
          static_cast<int>(satellite.elevation_degrees.value));
    }
    if (satellite.carrier_to_noise_db_hz.valid) {
      printf(", C/N0 %u dB-Hz", static_cast<unsigned int>(
          satellite.carrier_to_noise_db_hz.value));
    }
    if (satellite.carrier_lock_time_seconds.valid) {
      printf(", lock %u s", static_cast<unsigned int>(
          satellite.carrier_lock_time_seconds.value));
    }
    printf("\n");
  }
}

void PrintPubxTime(const char* source, const NmeaParser::PubxTime& value) {
  if (!value.valid) {
    return;
  }
  PrintHeading(source, "PUBX,04 time and clock");
  PrintUtc(source, "PUBX,04", value.utc);
  PrintDate(source, "PUBX,04", value.date);
  PrintDouble(source, "PUBX,04", "UTC time of week",
      value.utc_time_of_week_seconds, " s");
  PrintUnsigned(
      source, "PUBX,04", "UTC week", value.utc_week_number);
  if (value.leap_seconds.valid) {
    printf("[%s][PUBX,04] leap seconds: %d%s\n", source,
        static_cast<int>(value.leap_seconds.value),
        value.leap_seconds_are_default ? " (firmware default)" : "");
  }
  if (value.clock_bias_nanoseconds.valid) {
    printf("[%s][PUBX,04] clock bias: %lld ns\n", source,
        static_cast<long long>(value.clock_bias_nanoseconds.value));
  }
  PrintDouble(source, "PUBX,04", "clock drift",
      value.clock_drift_nanoseconds_per_second, " ns/s");
  if (value.time_pulse_granularity_nanoseconds.valid) {
    printf("[%s][PUBX,04] time pulse granularity: %ld ns\n", source,
        static_cast<long>(value.time_pulse_granularity_nanoseconds.value));
  }
}

}  // namespace

void PrintNmeaUpdate(
    const char* source, const NmeaParser::Update& update) {
  PrintDtm(source, update.dtm);
  PrintGbs(source, update.gbs);
  PrintGga(source, update.gga);
  PrintGll(source, update.gll);
  PrintGns(source, update.gns);
  for (const auto& value : update.grs) {
    PrintGrs(source, value);
  }
  for (const auto& value : update.gsa) {
    PrintGsa(source, value);
  }
  PrintGst(source, update.gst);
  for (const auto& value : update.gsv) {
    PrintGsv(source, value);
  }
  PrintRlm(source, update.rlm);
  PrintRmc(source, update.rmc);
  for (const auto& value : update.txt) {
    PrintTxt(source, update, value);
  }
  PrintVlw(source, update.vlw);
  PrintVtg(source, update.vtg);
  PrintZda(source, update.zda);
  PrintPubxPosition(source, update.pubx_position);
  PrintPubxSatelliteStatus(source, update.pubx_satellite_status);
  PrintPubxTime(source, update.pubx_time);
}

void PrintNmeaDiagnostics(
    const char* source, const NmeaParser::FeedResult& result) {
  if (result.checksum_errors == 0 && result.format_errors == 0 &&
      result.overflow_errors == 0 && result.capacity_errors == 0) {
    return;
  }
  printf("[%s][NMEA] parsed: %u, checksum errors: %u, format errors: %u, "
         "overflow errors: %u, capacity errors: %u, unsupported: %u\n",
      source, static_cast<unsigned int>(result.parsed_sentences),
      static_cast<unsigned int>(result.checksum_errors),
      static_cast<unsigned int>(result.format_errors),
      static_cast<unsigned int>(result.overflow_errors),
      static_cast<unsigned int>(result.capacity_errors),
      static_cast<unsigned int>(result.unsupported_sentences));
}

void PrintRawBlock(const char* source, const uint8_t* data, size_t length) {
  printf("\n[%s][RAW] begin (%u bytes)\n", source,
      static_cast<unsigned int>(length));
  if (data != nullptr && length > 0) {
    fwrite(data, 1, length, stdout);
    if (data[length - 1] != '\n') {
      printf("\n");
    }
  }
  printf("[%s][RAW] end\n", source);
}

}  // namespace gps
