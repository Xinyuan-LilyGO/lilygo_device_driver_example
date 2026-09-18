/*
 * @Description: BQ27220 电池状态监测实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-03 16:57:00
 * @License: GPL 3.0
 */
#include "battery_management.h"
#include "common.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)

namespace {
void PrintSection(const char* title) {
  BatteryLogPrintf("\n%s:\n", title);
}

const char* SecurityModeToString(
    cpp_bus_driver::Bq27220::SecurityMode mode) {
  switch (mode) {
    case cpp_bus_driver::Bq27220::SecurityMode::kFullAccess:
      return "full-access";
    case cpp_bus_driver::Bq27220::SecurityMode::kUnsealed:
      return "unsealed";
    case cpp_bus_driver::Bq27220::SecurityMode::kSealed:
      return "sealed";
    case cpp_bus_driver::Bq27220::SecurityMode::kUnknown:
    default:
      return "unknown";
  }
}
}  // namespace

void RunBq27220Example() {
  BatteryLogPrintf("BQ27220 battery management example\n");

  auto& driver = common::GetDriver();

  if (!driver.IsBq27220Ready()) {
    BatteryLogPrintf("BQ27220 is not ready\n");
    return;
  }
  auto& bq27220 = driver.chip().bq27220;

  while (true) {
    cpp_bus_driver::Bq27220::BatteryStatus battery_status;
    cpp_bus_driver::Bq27220::OperationStatus operation_status;
    const bool battery_status_ok = bq27220->GetBatteryStatus(battery_status);
    const bool operation_status_ok =
        bq27220->GetOperationStatus(operation_status);
    const int16_t current_ma = bq27220->GetCurrent();

    BatteryLogBeginSnapshot();
    BatteryLogPrintf("\nBQ27220 snapshot\n");
    PrintSection("Chip information");
    BatteryLogPrintf("  Chip ID: 0x%04X\n", bq27220->GetChipId());
    BatteryLogPrintf("  Firmware version: 0x%04X\n", bq27220->GetFirmwareVersion());
    BatteryLogPrintf("  Hardware version: 0x%04X\n", bq27220->GetHardwareVersion());

    PrintSection("Operation status");
    if (operation_status_ok) {
      BatteryLogPrintf("  Security mode: %s\n",
          SecurityModeToString(operation_status.security));
      BatteryLogPrintf("  Calibration mode: %d\n",
          operation_status.flag.calibration_mode);
      BatteryLogPrintf("  Config update: %d\n",
          operation_status.flag.config_update_mode);
      BatteryLogPrintf("  Init complete: %d\n",
          operation_status.flag.initialization_complete);
      BatteryLogPrintf("  EDV2 reached: %d\n", operation_status.flag.edv2_reached);
      BatteryLogPrintf("  Valid discharge qualified: %d\n",
          operation_status.flag.valid_discharge_qualified);
      BatteryLogPrintf("  Smoothing active: %d\n",
          operation_status.flag.smoothing_active);
      BatteryLogPrintf("  Battery trip point interrupt: %d\n",
          operation_status.flag.battery_trip_point_interrupt);
    } else {
      BatteryLogPrintf("  Operation status: read failed\n");
    }

    PrintSection("Battery capacity");
    BatteryLogPrintf("  Design capacity: %u mAh\n", bq27220->GetDesignCapacity());
    BatteryLogPrintf("  Remaining capacity: %u mAh\n",
        bq27220->GetRemainingCapacity());
    BatteryLogPrintf("  Full charge capacity: %u mAh\n",
        bq27220->GetFullChargeCapacity());
    BatteryLogPrintf("  State of charge: %u%%\n", bq27220->GetStatusOfCharge());
    BatteryLogPrintf("  State of health: %u%%\n", bq27220->GetStatusOfHealth());
    BatteryLogPrintf("  Cycle count: %u\n", bq27220->GetCycleCount());
    BatteryLogPrintf("  Raw coulomb count: %d c\n", bq27220->GetRawCoulombCount());

    PrintSection("Voltage and current");
    BatteryLogPrintf("  Voltage: %u mV\n", bq27220->GetVoltage());
    BatteryLogPrintf("  Current: %d mA\n", current_ma);
    BatteryLogPrintf("  Average current: %d mA\n", bq27220->GetAverageCurrent());
    BatteryLogPrintf("  Average power: %d mW\n", bq27220->GetAveragePower());
    BatteryLogPrintf("  Charging voltage request: %u mV\n",
        bq27220->GetChargingVoltage());
    BatteryLogPrintf("  Charging current request: %u mA\n",
        bq27220->GetChargingCurrent());
    BatteryLogPrintf("  Standby current: %d mA\n", bq27220->GetStandbyCurrent());
    BatteryLogPrintf("  Max load current: %d mA\n", bq27220->GetMaxLoadCurrent());

    PrintSection("Temperature");
    BatteryLogPrintf("  Gauge temperature: %.2f C\n", bq27220->GetTemperatureCelsius());
    BatteryLogPrintf("  Internal temperature: %.2f C\n",
        bq27220->GetChipTemperatureCelsius());

    PrintSection("Runtime estimates");
    bq27220->SetAtRate(current_ma);
    BatteryLogPrintf("  AtRate: %d mA\n", bq27220->GetAtRate());
    BatteryLogPrintf("  AtRate time to empty: %u min\n",
        bq27220->GetAtRateTimeToEmpty());
    BatteryLogPrintf("  Time to empty: %u min\n", bq27220->GetTimeToEmpty());
    BatteryLogPrintf("  Time to full: %u min\n", bq27220->GetTimeToFull());
    BatteryLogPrintf("  Standby time to empty: %u min\n",
        bq27220->GetStandbyTimeToEmpty());
    BatteryLogPrintf("  Max load time to empty: %u min\n",
        bq27220->GetMaxLoadTimeToEmpty());

    PrintSection("Battery status");
    if (battery_status_ok) {
      BatteryLogPrintf("  Discharging: %d\n", battery_status.flag.discharging);
      BatteryLogPrintf("  Battery present: %d\n", battery_status.flag.battery_present);
      BatteryLogPrintf("  Authentication good: %d\n",
          battery_status.flag.authentication_good);
      BatteryLogPrintf("  Open circuit voltage good: %d\n",
          battery_status.flag.open_circuit_voltage_good);
      BatteryLogPrintf("  Open circuit voltage failed: %d\n",
          battery_status.flag.open_circuit_voltage_failed);
      BatteryLogPrintf("  Open circuit voltage complete: %d\n",
          battery_status.flag.open_circuit_voltage_complete);
      BatteryLogPrintf("  Full charged: %d\n", battery_status.flag.full_charged);
      BatteryLogPrintf("  Full discharged: %d\n", battery_status.flag.full_discharged);
      BatteryLogPrintf("  Charge inhibit: %d\n", battery_status.flag.charge_inhibit);
      BatteryLogPrintf("  Charge overtemperature: %d\n",
          battery_status.flag.over_temperature_charge);
      BatteryLogPrintf("  Discharge overtemperature: %d\n",
          battery_status.flag.over_temperature_discharge);
      BatteryLogPrintf("  Sleep: %d\n", battery_status.flag.sleep_mode);
      BatteryLogPrintf("  Terminate charge alarm: %d\n",
          battery_status.flag.terminate_charge_alarm);
      BatteryLogPrintf("  Terminate discharge alarm: %d\n",
          battery_status.flag.terminate_discharge_alarm);
      BatteryLogPrintf("  System down: %d\n", battery_status.flag.system_down);
    } else {
      BatteryLogPrintf("  Battery status: read failed\n");
    }

    BatteryLogEndSnapshot();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

#endif
