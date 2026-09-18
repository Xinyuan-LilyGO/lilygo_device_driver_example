/*
 * @Description: AXP517 充电、电池状态、ADC 与中断监测实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-03 16:57:00
 * @License: GPL 3.0
 */
#include "battery_management.h"
#include "common.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    (defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
        defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2))

const char* ChargeStatusName(cpp_bus_driver::Axp517::ChargeStatus status) {
  switch (status) {
    case cpp_bus_driver::Axp517::ChargeStatus::kTrickleCharge:
      return "trickle charge";
    case cpp_bus_driver::Axp517::ChargeStatus::kPrecharge:
      return "precharge";
    case cpp_bus_driver::Axp517::ChargeStatus::kConstantCurrent:
      return "constant current";
    case cpp_bus_driver::Axp517::ChargeStatus::kConstantVoltage:
      return "constant voltage";
    case cpp_bus_driver::Axp517::ChargeStatus::kChargeDone:
      return "charge done";
    case cpp_bus_driver::Axp517::ChargeStatus::kNotCharging:
      return "not charging";
    default:
      return "invalid";
  }
}

const char* BatteryCurrentDirectionName(
    cpp_bus_driver::Axp517::BatteryCurrentDirection direction) {
  switch (direction) {
    case cpp_bus_driver::Axp517::BatteryCurrentDirection::kStandby:
      return "standby";
    case cpp_bus_driver::Axp517::BatteryCurrentDirection::kCharge:
      return "charge";
    case cpp_bus_driver::Axp517::BatteryCurrentDirection::kDischarge:
      return "discharge";
    default:
      return "invalid";
  }
}

const char* BcDetectResultName(cpp_bus_driver::Axp517::BcDetectResult result) {
  switch (result) {
    case cpp_bus_driver::Axp517::BcDetectResult::kSdp:
      return "SDP";
    case cpp_bus_driver::Axp517::BcDetectResult::kCdp:
      return "CDP";
    case cpp_bus_driver::Axp517::BcDetectResult::kDcp:
      return "DCP";
    default:
      return "invalid";
  }
}

const char* YesNo(bool value) {
  return value ? "yes" : "no";
}

void PrintChipStatus(cpp_bus_driver::Axp517& axp517) {
  cpp_bus_driver::Axp517::ChipStatus0 status0;
  if (axp517.GetChipStatus0(status0)) {
    BatteryLogPrintf("\nchip status0:\n");
    BatteryLogPrintf("  current limit: %s\n", YesNo(status0.current_limit_status));
    BatteryLogPrintf("  thermal regulation: %s\n",
        YesNo(status0.thermal_regulation_status));
    BatteryLogPrintf("  battery active mode: %s\n",
        YesNo(status0.battery_in_active_mode));
    BatteryLogPrintf("  battery present: %s\n",
        YesNo(status0.battery_present_status));
    BatteryLogPrintf("  batfet on: %s\n", YesNo(status0.batfet_status));
    BatteryLogPrintf("  vbus good: %s\n", YesNo(status0.vbus_good_indication));
  } else {
    BatteryLogPrintf("\nchip status0: read failed\n");
  }

  cpp_bus_driver::Axp517::ChipStatus1 status1;
  if (axp517.GetChipStatus1(status1)) {
    BatteryLogPrintf("\nchip status1:\n");
    BatteryLogPrintf("  charging status: %s\n",
        ChargeStatusName(status1.charging_status));
    BatteryLogPrintf("  vindpm status: %s\n", YesNo(status1.vindpm_status));
    BatteryLogPrintf("  system status indication: %s\n",
        YesNo(status1.system_status_indication));
    BatteryLogPrintf("  battery current direction: %s\n",
        BatteryCurrentDirectionName(status1.battery_current_direction));
  } else {
    BatteryLogPrintf("\nchip status1: read failed\n");
  }
}

void PrintIrqStatus(cpp_bus_driver::Axp517& axp517) {
  cpp_bus_driver::Axp517::IrqStatus0 irq0;
  cpp_bus_driver::Axp517::IrqStatus1 irq1;
  cpp_bus_driver::Axp517::IrqStatus2 irq2;
  cpp_bus_driver::Axp517::IrqStatus3 irq3;
  if (!axp517.GetIrqStatus(irq0, irq1, irq2, irq3)) {
    BatteryLogPrintf("\nlatched irq status: read failed\n");
    return;
  }

  BatteryLogPrintf("\nlatched irq status0:\n");
  BatteryLogPrintf("  vbus fault: %s\n", YesNo(irq0.vbus_fault_flag));
  BatteryLogPrintf("  vbus over voltage: %s\n",
      YesNo(irq0.vbus_over_voltage_flag));
  BatteryLogPrintf("  boost over voltage: %s\n",
      YesNo(irq0.boost_over_voltage_flag));
  BatteryLogPrintf("  charge to normal: %s\n", YesNo(irq0.charge_to_normal_flag));
  BatteryLogPrintf("  gauge new soc: %s\n", YesNo(irq0.gauge_new_soc_flag));
  BatteryLogPrintf("  soc drop to shutdown level: %s\n",
      YesNo(irq0.soc_drop_to_shutdown_level_flag));
  BatteryLogPrintf("  soc drop to warning level: %s\n",
      YesNo(irq0.soc_drop_to_warning_level_flag));

  BatteryLogPrintf("\nlatched irq status1:\n");
  BatteryLogPrintf("  power key positive edge: %s\n",
      YesNo(irq1.pwr_on_positive_edge_flag));
  BatteryLogPrintf("  power key negative edge: %s\n",
      YesNo(irq1.pwr_on_negative_edge_flag));
  BatteryLogPrintf("  power key long press: %s\n",
      YesNo(irq1.pwr_on_long_press_flag));
  BatteryLogPrintf("  power key short press: %s\n",
      YesNo(irq1.pwr_on_short_press_flag));
  BatteryLogPrintf("  battery removed: %s\n", YesNo(irq1.battery_remove_flag));
  BatteryLogPrintf("  battery inserted: %s\n", YesNo(irq1.battery_insert_flag));
  BatteryLogPrintf("  vbus removed: %s\n", YesNo(irq1.vbus_remove_flag));
  BatteryLogPrintf("  vbus inserted: %s\n", YesNo(irq1.vbus_insert_flag));

  BatteryLogPrintf("\nlatched irq status2:\n");
  BatteryLogPrintf("  battery over voltage: %s\n",
      YesNo(irq2.battery_over_voltage_flag));
  BatteryLogPrintf("  charger safety timer expired: %s\n",
      YesNo(irq2.charger_safety_timer_expire_flag));
  BatteryLogPrintf("  die over temperature level1: %s\n",
      YesNo(irq2.die_over_temperature_level1_flag));
  BatteryLogPrintf("  charger started: %s\n", YesNo(irq2.charger_start_flag));
  BatteryLogPrintf("  battery charge done: %s\n",
      YesNo(irq2.battery_charge_done_flag));
  BatteryLogPrintf("  batfet over current: %s\n",
      YesNo(irq2.batfet_over_current_flag));
  BatteryLogPrintf("  watchdog expired: %s\n", YesNo(irq2.watchdog_expire_flag));

  BatteryLogPrintf("\nlatched irq status3:\n");
  BatteryLogPrintf("  battery under temperature work: %s\n",
      YesNo(irq3.battery_under_temperature_work_flag));
  BatteryLogPrintf("  battery over temperature work: %s\n",
      YesNo(irq3.battery_over_temperature_work_flag));
  BatteryLogPrintf("  battery under temperature charge: %s\n",
      YesNo(irq3.battery_under_temperature_charge_flag));
  BatteryLogPrintf("  battery over temperature charge: %s\n",
      YesNo(irq3.battery_over_temperature_charge_flag));
  BatteryLogPrintf("  battery over temperature quit: %s\n",
      YesNo(irq3.battery_over_temperature_quit_flag));
  BatteryLogPrintf("  bc1.2 result changed: %s\n",
      YesNo(irq3.bc1_2_detect_result_change_flag));
  BatteryLogPrintf("  bc1.2 detect finished: %s\n",
      YesNo(irq3.bc1_2_detect_finished_flag));

  if (!axp517.ClearAllIrq()) {
    BatteryLogPrintf("  clear latched irq failed\n");
  }
}

void PrintBatteryGauge(cpp_bus_driver::Axp517& axp517) {
  BatteryLogPrintf("\nbattery gauge:\n");
  BatteryLogPrintf("  level: %u %%\n", axp517.GetBatteryLevel());
  BatteryLogPrintf("  health: %u %%\n", axp517.GetBatteryHealth());
  BatteryLogPrintf("  temperature: %d C\n",
      axp517.GetBatteryTemperatureCelsius());
}

void PrintAdcInfo(cpp_bus_driver::Axp517& axp517,
    cpp_bus_driver::Axp517::BatteryCurrentDirection direction) {
  BatteryLogPrintf("\nadc data:\n");
  BatteryLogPrintf("  battery voltage: %u mV\n", axp517.GetBatteryVoltage());
  BatteryLogPrintf("  battery current: %.2f mA\n", axp517.GetBatteryCurrent());
  BatteryLogPrintf("  ts voltage: %.2f mV\n", axp517.GetTsVoltage());
  BatteryLogPrintf("  vbus voltage: %u mV\n", axp517.GetVbusVoltage());
  BatteryLogPrintf("  vbus current: %u mA\n", axp517.GetVbusCurrent());

  if (axp517.SetAdcDataSelect(
          cpp_bus_driver::Axp517::AdcData::kSystemVoltage)) {
    BatteryLogPrintf("  system voltage: %u mV\n", axp517.GetSystemVoltage());
  } else {
    BatteryLogPrintf("  system voltage: select failed\n");
  }

  if (axp517.SetAdcDataSelect(
          cpp_bus_driver::Axp517::AdcData::kChipTemperatureCelsius)) {
    BatteryLogPrintf("  chip die junction temperature: %.2f C\n",
        axp517.GetChipDieJunctionTemperatureCelsius());
  } else {
    BatteryLogPrintf("  chip die junction temperature: select failed\n");
  }

  switch (direction) {
    case cpp_bus_driver::Axp517::BatteryCurrentDirection::kCharge:
      if (axp517.SetAdcDataSelect(
              cpp_bus_driver::Axp517::AdcData::kChargingCurrent)) {
        BatteryLogPrintf("  charging current: %.2f mA\n", axp517.GetChargingCurrent());
      } else {
        BatteryLogPrintf("  charging current: select failed\n");
      }
      break;
    case cpp_bus_driver::Axp517::BatteryCurrentDirection::kDischarge:
      if (axp517.SetAdcDataSelect(
              cpp_bus_driver::Axp517::AdcData::kDischargeCurrent)) {
        BatteryLogPrintf("  discharging current: %.2f mA\n",
            axp517.GetDischargingCurrent());
      } else {
        BatteryLogPrintf("  discharging current: select failed\n");
      }
      break;
    case cpp_bus_driver::Axp517::BatteryCurrentDirection::kStandby:
      BatteryLogPrintf("  charge/discharge current: standby\n");
      break;
    default:
      BatteryLogPrintf("  charge/discharge current: invalid direction\n");
      break;
  }
}

void PrintBc12Info(cpp_bus_driver::Axp517& axp517) {
  BatteryLogPrintf("\nbc1.2 detection:\n");
  cpp_bus_driver::Axp517::BcDetectResult result;
  if (axp517.GetBc12DetectResult(result)) {
    BatteryLogPrintf("  bc1.2 detect result: %s\n", BcDetectResultName(result));
  } else {
    BatteryLogPrintf("  bc1.2 detect result: invalid or read failed\n");
  }
}

void PrintPowerInfo(cpp_bus_driver::Axp517& axp517) {
  BatteryLogPrintf("\n========== AXP517 power info ==========\n");
  BatteryLogPrintf("\nchip information:\n  chip id: %#X\n", axp517.GetChipId());
  PrintChipStatus(axp517);
  PrintBatteryGauge(axp517);

  cpp_bus_driver::Axp517::ChipStatus1 status1;
  if (axp517.GetChipStatus1(status1)) {
    PrintAdcInfo(axp517, status1.battery_current_direction);
  } else {
    PrintAdcInfo(
        axp517, cpp_bus_driver::Axp517::BatteryCurrentDirection::kInvalid);
  }

  PrintBc12Info(axp517);
  PrintIrqStatus(axp517);
}

void RunAxp517Example() {
  BatteryLogPrintf("AXP517 battery management example\n");

  auto& driver = common::GetDriver();
  if (!driver.IsAxp517Ready()) {
    BatteryLogPrintf("AXP517 init failed\n");
    return;
  }
  auto& axp517 = driver.chip().axp517;

  cpp_bus_driver::Axp517::AdcChannel adc_channel = {
      .vbus_current_measure = true,
      .battery_discharge_current_measure = true,
      .battery_charge_current_measure = true,
      .chip_temperature_measure = true,
      .system_voltage_measure = true,
      .vbus_voltage_measure = true,
      .ts_value_measure = true,
      .battery_voltage_measure = true,
  };
  if (!axp517->SetAdcChannel(adc_channel) ||
      !axp517->SetBc12DetectEnable(true) || !axp517->ClearAllIrq()) {
    BatteryLogPrintf("AXP517 measurement configuration failed\n");
    return;
  }

  while (1) {
    BatteryLogBeginSnapshot();
    PrintPowerInfo(*axp517);
    BatteryLogEndSnapshot();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

#endif
