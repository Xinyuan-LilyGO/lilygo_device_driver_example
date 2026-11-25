/*
 * @Description: T-Glasses-P4 充电和电量监测实现
 */
#include <cstdint>

#include "battery_management.h"

#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)

namespace {

using Bq2589x = cpp_bus_driver::Bq2589x;
using Bq27220 = cpp_bus_driver::Bq27220;

void PrintSection(const char* title) { BatteryLogPrintf("\n%s:\n", title); }

const char* YesNo(bool value) { return value ? "yes" : "no"; }

const char* GetChipModelName(Bq2589x::ChipModel model) {
  switch (model) {
    case Bq2589x::ChipModel::kBq25890:
      return "BQ25890";
    case Bq2589x::ChipModel::kBq25890h:
      return "BQ25890H";
    case Bq2589x::ChipModel::kBq25892:
      return "BQ25892";
    case Bq2589x::ChipModel::kBq25895:
      return "BQ25895";
    case Bq2589x::ChipModel::kBq25895m:
      return "BQ25895M";
    case Bq2589x::ChipModel::kBq25896:
      return "BQ25896";
    case Bq2589x::ChipModel::kBq25898:
      return "BQ25898";
    case Bq2589x::ChipModel::kBq25898c:
      return "BQ25898C";
    case Bq2589x::ChipModel::kBq25898d:
      return "BQ25898D";
    case Bq2589x::ChipModel::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

const char* GetVindpmModeName(Bq2589x::VindpmMode mode) {
  switch (mode) {
    case Bq2589x::VindpmMode::kRelative:
      return "Relative";
    case Bq2589x::VindpmMode::kAbsolute:
      return "Absolute";
  }
  return "Unknown";
}

const char* GetSecurityModeName(Bq27220::SecurityMode mode) {
  switch (mode) {
    case Bq27220::SecurityMode::kFullAccess:
      return "Full access";
    case Bq27220::SecurityMode::kUnsealed:
      return "Unsealed";
    case Bq27220::SecurityMode::kSealed:
      return "Sealed";
    case Bq27220::SecurityMode::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

void PrintDuration(const char* label, uint16_t minutes) {
  if (minutes == UINT16_MAX) {
    BatteryLogPrintf("  %s: unavailable\n", label);
  } else {
    BatteryLogPrintf("  %s: %hu min\n", label, minutes);
  }
}

const char* GetInputName(Bq2589x::VbusStatus status) {
  switch (status) {
    case Bq2589x::VbusStatus::kNoInput:
      return "None";
    case Bq2589x::VbusStatus::kUsbHostSdp:
      return "USB SDP";
    case Bq2589x::VbusStatus::kUsbCdp:
      return "USB CDP";
    case Bq2589x::VbusStatus::kUsbDcp:
      return "USB DCP";
    case Bq2589x::VbusStatus::kAdjustableHighVoltageDcp:
      return "HVDCP";
    case Bq2589x::VbusStatus::kUnknownAdapter:
      return "Unknown adapter";
    case Bq2589x::VbusStatus::kNonStandardAdapter:
      return "Non-standard adapter";
    case Bq2589x::VbusStatus::kAdapter:
      return "Adapter";
    case Bq2589x::VbusStatus::kOtg:
      return "OTG";
    case Bq2589x::VbusStatus::kUnknown:
      break;
  }
  return "Unknown";
}

const char* GetChargeStateName(Bq2589x::ChargeStatus status) {
  switch (status) {
    case Bq2589x::ChargeStatus::kNotCharging:
      return "Not charging";
    case Bq2589x::ChargeStatus::kPrecharge:
      return "Precharge";
    case Bq2589x::ChargeStatus::kFastCharge:
      return "Fast charge";
    case Bq2589x::ChargeStatus::kTerminationDone:
      return "Charge complete";
  }
  return "Unknown";
}

const char* GetChargeFaultName(Bq2589x::ChargeFault fault) {
  switch (fault) {
    case Bq2589x::ChargeFault::kNormal:
      return "None";
    case Bq2589x::ChargeFault::kInputFault:
      return "Input fault";
    case Bq2589x::ChargeFault::kThermalShutdown:
      return "Thermal shutdown";
    case Bq2589x::ChargeFault::kSafetyTimerExpired:
      return "Safety timer expired";
  }
  return "Unknown";
}

const char* GetNtcFaultName(Bq2589x::NtcFault fault) {
  switch (fault) {
    case Bq2589x::NtcFault::kNormal:
      return "Normal";
    case Bq2589x::NtcFault::kWarm:
      return "Warm";
    case Bq2589x::NtcFault::kCool:
      return "Cool";
    case Bq2589x::NtcFault::kCold:
      return "Cold";
    case Bq2589x::NtcFault::kHot:
      return "Hot";
    case Bq2589x::NtcFault::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

void PrintChargerStatus(Bq2589x& charger) {
  PrintSection("Power status");
  Bq2589x::ChipStatus status;
  if (charger.GetChipStatus(status)) {
    BatteryLogPrintf("  Input source: %s\n", GetInputName(status.vbus_status));
    BatteryLogPrintf("  Input power good: %s\n", YesNo(status.power_good));
    BatteryLogPrintf("  Charge stage: %s\n",
        GetChargeStateName(status.charge_status));
    BatteryLogPrintf("  System minimum voltage regulation: %s\n",
        YesNo(status.system_minimum_voltage_regulation));
    BatteryLogPrintf("  Status register: 0x%02hhX\n", status.raw);
  } else {
    BatteryLogPrintf("  Charger status: read failed\n");
  }
  Bq2589x::DpmStatus dpm;
  if (charger.GetDpmStatus(dpm)) {
    BatteryLogPrintf("  Input voltage regulation active: %s\n",
        YesNo(dpm.vindpm_active));
    BatteryLogPrintf("  Input current regulation active: %s\n",
        YesNo(dpm.iindpm_active));
    if (dpm.input_current_limit_valid) {
      BatteryLogPrintf("  DPM input current limit: %hu mA\n",
          dpm.input_current_limit_ma);
    }
  } else {
    BatteryLogPrintf("  DPM status: read failed\n");
  }
}

void PrintChargerSetting(Bq2589x& charger, const char* label, const char* unit,
    bool (Bq2589x::*read)(uint16_t&)) {
  uint16_t value = 0;
  if ((charger.*read)(value)) {
    BatteryLogPrintf("  %s: %hu %s\n", label, value, unit);
  } else {
    BatteryLogPrintf("  %s: read failed\n", label);
  }
}

void PrintChargerFlag(Bq2589x& charger, const char* label,
    bool (Bq2589x::*read)(bool&)) {
  bool enabled = false;
  if ((charger.*read)(enabled)) {
    BatteryLogPrintf("  %s: %s\n", label, YesNo(enabled));
  } else {
    BatteryLogPrintf("  %s: read failed or unsupported\n", label);
  }
}

void PrintChargingConfiguration(Bq2589x& charger) {
  PrintSection("Charging configuration");
  PrintChargerFlag(charger, "Charging enabled", &Bq2589x::GetChargeEnabled);
  PrintChargerFlag(charger, "Input high impedance",
      &Bq2589x::GetHighImpedanceEnabled);
  PrintChargerFlag(charger, "ILIM pin limit enabled",
      &Bq2589x::GetIlimPinEnabled);
  PrintChargerFlag(charger, "Input current optimizer enabled",
      &Bq2589x::GetInputCurrentOptimizerEnabled);
  PrintChargerFlag(charger, "Charge termination enabled",
      &Bq2589x::GetChargeTerminationEnabled);
  PrintChargerSetting(charger, "Input current setting", "mA",
      &Bq2589x::GetInputCurrentLimit);
  Bq2589x::VindpmMode vindpm_mode;
  const bool vindpm_mode_valid = charger.GetVindpmMode(vindpm_mode);
  if (vindpm_mode_valid) {
    BatteryLogPrintf("  Input voltage DPM mode: %s\n",
        GetVindpmModeName(vindpm_mode));
  } else {
    BatteryLogPrintf("  Input voltage DPM mode: read failed\n");
  }
  if (vindpm_mode_valid && vindpm_mode == Bq2589x::VindpmMode::kRelative) {
    uint16_t vindpm_offset_mv = 0;
    if (charger.GetInputVoltageLimitOffset(vindpm_offset_mv)) {
      BatteryLogPrintf("  Relative input voltage limit offset: %hu mV\n",
          vindpm_offset_mv);
    } else {
      BatteryLogPrintf("  Relative input voltage limit offset: read failed\n");
    }
  } else if (vindpm_mode_valid) {
    BatteryLogPrintf("  Relative input voltage limit offset: inactive\n");
  } else {
    BatteryLogPrintf("  Relative input voltage limit offset: unavailable\n");
  }
  PrintChargerSetting(charger, "Input voltage regulation threshold", "mV",
      &Bq2589x::GetVindpmThreshold);
  PrintChargerSetting(charger, "Fast charge current setting", "mA",
      &Bq2589x::GetFastChargeCurrentLimit);
  PrintChargerSetting(charger, "Precharge current setting", "mA",
      &Bq2589x::GetPrechargeCurrentLimit);
  PrintChargerSetting(charger, "Termination current setting", "mA",
      &Bq2589x::GetTerminationCurrentLimit);
  PrintChargerSetting(charger, "Charge voltage setting", "mV",
      &Bq2589x::GetChargeVoltageLimit);
  PrintChargerFlag(charger, "Safety timer enabled",
      &Bq2589x::GetSafetyTimerEnabled);
  PrintChargerSetting(charger, "Fast charge safety timer", "h",
      &Bq2589x::GetFastChargeTimer);
  PrintChargerFlag(charger, "Safety timer slowdown enabled",
      &Bq2589x::GetSafetyTimerSlowdownEnabled);
  PrintChargerSetting(charger, "Watchdog timeout", "s",
      &Bq2589x::GetWatchdogTimer);
}

void PrintChargerAdc(Bq2589x& charger, bool adc_ready) {
  PrintSection("Voltage and current measurements");
  Bq2589x::AdcMeasurements measurements;
  if (!adc_ready || !charger.StartAdcConversion() ||
      !charger.WaitForAdcConversion() ||
      !charger.GetAdcMeasurements(measurements)) {
    BatteryLogPrintf("  ADC: unavailable\n");
    return;
  }
  BatteryLogPrintf("  Battery voltage: %hu mV\n",
      measurements.battery_voltage_mv);
  BatteryLogPrintf("  System voltage: %hu mV\n", measurements.system_voltage_mv);
  if (measurements.vbus_attached) {
    BatteryLogPrintf("  VBUS voltage: %hu mV\n", measurements.vbus_voltage_mv);
  } else {
    BatteryLogPrintf("  VBUS: not detected\n");
  }
  BatteryLogPrintf("  Charging current ADC: %hu mA\n",
      measurements.charge_current_ma);
  BatteryLogPrintf("  Thermal regulation active: %s\n",
      YesNo(measurements.thermal_regulation_active));
  if (measurements.ts_voltage_valid) {
    BatteryLogPrintf("  TS voltage (VTS/VREGN): %.2f %%\n",
        measurements.ts_voltage_percentage);
  }
  float battery_temperature_c = 0.0f;
  if (measurements.ts_voltage_valid &&
      Bq2589x::ConvertTsToBatteryTemperature(
          common::board::device::bq25896::kNtcConfig,
          measurements.ts_voltage_percentage, battery_temperature_c)) {
    BatteryLogPrintf("  Battery temperature: %.1f C (NTC estimate)\n",
        battery_temperature_c);
  } else {
    BatteryLogPrintf("  Battery temperature: unavailable\n");
  }
}

void PrintChargerFaults(Bq2589x& charger) {
  PrintSection("Fault status");
  Bq2589x::FaultStatus latched;
  Bq2589x::FaultStatus current;
  if (!charger.GetFaultStatus(latched, current)) {
    BatteryLogPrintf("  Fault registers: read failed\n");
    return;
  }
  BatteryLogPrintf("  Latched register: 0x%02hhX\n", latched.raw);
  BatteryLogPrintf("  Current register: 0x%02hhX\n", current.raw);
  BatteryLogPrintf("  Charge fault (latched / current): %s / %s\n",
      GetChargeFaultName(latched.charge_fault),
      GetChargeFaultName(current.charge_fault));
  BatteryLogPrintf("  Battery overvoltage (latched / current): %s / %s\n",
      YesNo(latched.battery_overvoltage), YesNo(current.battery_overvoltage));
  BatteryLogPrintf("  Watchdog expired (latched): %s\n",
      YesNo(latched.watchdog_expired));
  if (current.boost_fault_valid) {
    BatteryLogPrintf("  Boost fault (latched / current): %s / %s\n",
        YesNo(latched.boost_fault), YesNo(current.boost_fault));
  }
  if (current.ntc_fault_valid) {
    BatteryLogPrintf("  NTC status: %s\n", GetNtcFaultName(current.ntc_fault));
  }
}

void PrintCharger(Bq2589x& charger, bool adc_ready) {
  PrintSection("Chip information");
  const auto& info = charger.chip_info();
  BatteryLogPrintf("  Model: %s\n", GetChipModelName(info.model));
  BatteryLogPrintf("  Part number: %hhu\n", info.part_number);
  BatteryLogPrintf("  Revision: %hhu\n", info.chip_revision);
  PrintChargerStatus(charger);
  PrintChargingConfiguration(charger);
  PrintChargerAdc(charger, adc_ready);
  PrintChargerFaults(charger);
}

void PrintBatteryGauge(Bq27220& gauge) {
  PrintSection("Chip information");
  BatteryLogPrintf("  Chip ID: 0x%04hX\n", gauge.GetChipId());
  BatteryLogPrintf("  Firmware version: 0x%04hX\n",
      gauge.GetFirmwareVersion());
  BatteryLogPrintf("  Hardware version: 0x%04hX\n",
      gauge.GetHardwareVersion());

  PrintSection("Operation status");
  Bq27220::OperationStatus operation_status;
  if (gauge.GetOperationStatus(operation_status)) {
    BatteryLogPrintf("  Security mode: %s\n",
        GetSecurityModeName(operation_status.security));
    BatteryLogPrintf("  Calibration mode: %s\n",
        YesNo(operation_status.flag.calibration_mode));
    BatteryLogPrintf("  Config update mode: %s\n",
        YesNo(operation_status.flag.config_update_mode));
    BatteryLogPrintf("  Initialization complete: %s\n",
        YesNo(operation_status.flag.initialization_complete));
    BatteryLogPrintf("  EDV2 reached: %s\n",
        YesNo(operation_status.flag.edv2_reached));
    BatteryLogPrintf("  Valid discharge qualified: %s\n",
        YesNo(operation_status.flag.valid_discharge_qualified));
    BatteryLogPrintf("  Smoothing active: %s\n",
        YesNo(operation_status.flag.smoothing_active));
    BatteryLogPrintf("  Battery trip point interrupt: %s\n",
        YesNo(operation_status.flag.battery_trip_point_interrupt));
  } else {
    BatteryLogPrintf("  Operation status: read failed\n");
  }

  PrintSection("Battery status");
  Bq27220::BatteryStatus status;
  if (gauge.GetBatteryStatus(status)) {
    BatteryLogPrintf("  Battery present: %s\n",
        YesNo(status.flag.battery_present));
    BatteryLogPrintf("  Discharging: %s\n", YesNo(status.flag.discharging));
    BatteryLogPrintf("  Fully charged: %s\n", YesNo(status.flag.full_charged));
    BatteryLogPrintf("  Charge inhibited: %s\n", YesNo(status.flag.charge_inhibit));
    BatteryLogPrintf("  Fully discharged: %s\n",
        YesNo(status.flag.full_discharged));
    BatteryLogPrintf("  Authentication good: %s\n",
        YesNo(status.flag.authentication_good));
    BatteryLogPrintf("  Open-circuit voltage good: %s\n",
        YesNo(status.flag.open_circuit_voltage_good));
    BatteryLogPrintf("  Open-circuit voltage failed: %s\n",
        YesNo(status.flag.open_circuit_voltage_failed));
    BatteryLogPrintf("  Open-circuit voltage complete: %s\n",
        YesNo(status.flag.open_circuit_voltage_complete));
    BatteryLogPrintf("  Charge overtemperature: %s\n",
        YesNo(status.flag.over_temperature_charge));
    BatteryLogPrintf("  Discharge overtemperature: %s\n",
        YesNo(status.flag.over_temperature_discharge));
    BatteryLogPrintf("  Sleep mode: %s\n", YesNo(status.flag.sleep_mode));
    BatteryLogPrintf("  System down: %s\n", YesNo(status.flag.system_down));
    BatteryLogPrintf("  Terminate charge alarm: %s\n",
        YesNo(status.flag.terminate_charge_alarm));
    BatteryLogPrintf("  Terminate discharge alarm: %s\n",
        YesNo(status.flag.terminate_discharge_alarm));
  } else {
    BatteryLogPrintf("  Battery status: read failed\n");
  }

  PrintSection("Battery capacity");
  BatteryLogPrintf("  Board rated capacity: %hu mAh\n",
      common::board::device::bq27220::kBatteryCapacityMah);
  const uint16_t design_capacity_mah = gauge.GetDesignCapacity();
  if (design_capacity_mah != 0) {
    BatteryLogPrintf("  Gauge design capacity: %hu mAh\n", design_capacity_mah);
  } else {
    BatteryLogPrintf("  Gauge design capacity: unavailable\n");
  }
  BatteryLogPrintf("  Gauge full charge capacity: %hu mAh\n",
      gauge.GetFullChargeCapacity());
  BatteryLogPrintf("  Remaining capacity: %hu mAh\n", gauge.GetRemainingCapacity());
  BatteryLogPrintf("  State of charge: %hu %%\n", gauge.GetStatusOfCharge());
  BatteryLogPrintf("  State of health: %hu %%\n", gauge.GetStatusOfHealth());
  BatteryLogPrintf("  Cycle count: %hu\n", gauge.GetCycleCount());
  BatteryLogPrintf("  Raw coulomb count: %d c\n", gauge.GetRawCoulombCount());

  PrintSection("Voltage and current measurements");
  BatteryLogPrintf("  Battery voltage: %hu mV\n", gauge.GetVoltage());
  BatteryLogPrintf("  Battery current: %d mA\n", gauge.GetCurrent());
  BatteryLogPrintf("  Average battery current: %d mA\n", gauge.GetAverageCurrent());
  BatteryLogPrintf("  Average power: %d mW\n", gauge.GetAveragePower());
  BatteryLogPrintf("  Charging voltage request: %hu mV\n",
      gauge.GetChargingVoltage());
  BatteryLogPrintf("  Charging current request: %hu mA\n",
      gauge.GetChargingCurrent());
  BatteryLogPrintf("  Standby current estimate: %d mA\n",
      gauge.GetStandbyCurrent());
  BatteryLogPrintf("  Maximum load current estimate: %d mA\n",
      gauge.GetMaxLoadCurrent());

  PrintSection("Temperature");
  BatteryLogPrintf("  Gauge temperature: %.2f C\n",
      gauge.GetTemperatureCelsius());
  BatteryLogPrintf("  Fuel gauge die temperature: %.2f C\n",
      gauge.GetChipTemperatureCelsius());

  PrintSection("Runtime estimates");
  const int16_t current_ma = gauge.GetCurrent();
  if (gauge.SetAtRate(current_ma)) {
    BatteryLogPrintf("  AtRate current: %d mA\n", gauge.GetAtRate());
    PrintDuration("AtRate time to empty", gauge.GetAtRateTimeToEmpty());
  } else {
    BatteryLogPrintf("  AtRate estimate: update failed\n");
  }
  PrintDuration("Time to empty", gauge.GetTimeToEmpty());
  PrintDuration("Time to full", gauge.GetTimeToFull());
  PrintDuration("Standby time to empty", gauge.GetStandbyTimeToEmpty());
  PrintDuration("Maximum-load time to empty", gauge.GetMaxLoadTimeToEmpty());
}

}  // namespace

void RunBq25896Example() {
  auto& driver = common::GetDriver();
  auto* charger =
      driver.IsBq25896Ready() ? driver.chip().bq25896.get() : nullptr;
  auto* gauge =
      driver.IsBq27220Ready() ? driver.chip().bq27220.get() : nullptr;
  if (gauge == nullptr) {
    BatteryLogPrintf("BQ27220 is not ready\n");
  }
  if (charger == nullptr) {
    BatteryLogPrintf("BQ25896 is not ready\n");
  }
  if (charger == nullptr && gauge == nullptr) {
    return;
  }
  const bool adc_ready = charger != nullptr &&
      charger->SetAdcConversionMode(Bq2589x::AdcConversionMode::kOneShot);

  while (true) {
    BatteryLogPrintf("\nBQ27220 fuel gauge snapshot\n");
    if (gauge != nullptr) {
      PrintBatteryGauge(*gauge);
    } else {
      BatteryLogPrintf("  Not ready\n");
    }
    BatteryLogPrintf("\nBQ25896 charger snapshot\n");
    if (charger != nullptr) {
      PrintCharger(*charger, adc_ready);
    } else {
      BatteryLogPrintf("  Not ready\n");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

#endif
