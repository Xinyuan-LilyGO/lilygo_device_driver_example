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

namespace {
const char* ChargeName(cpp_bus_driver::Axp517::ChargeStatus value) {
  using S = cpp_bus_driver::Axp517::ChargeStatus;
  switch (value) {
    case S::kTrickleCharge: return "trickle";
    case S::kPrecharge: return "precharge";
    case S::kConstantCurrent: return "constant-current";
    case S::kConstantVoltage: return "constant-voltage";
    case S::kChargeDone: return "done";
    case S::kNotCharging: return "not-charging";
    default: return "invalid";
  }
}

const char* DirectionName(cpp_bus_driver::Axp517::BatteryCurrentDirection value) {
  using D = cpp_bus_driver::Axp517::BatteryCurrentDirection;
  switch (value) {
    case D::kStandby: return "standby";
    case D::kCharge: return "charge";
    case D::kDischarge: return "discharge";
    default: return "invalid";
  }
}

void PrintAxp517(cpp_bus_driver::Axp517& chip) {
  cpp_bus_driver::Axp517::ChipId id;
  cpp_bus_driver::Axp517::Status status;
  cpp_bus_driver::Axp517::FaultStatus fault;
  uint8_t soc = 0, soh = 0;
  uint16_t vbat = 0, vsys = 0, vbus = 0, cycle = 0;
  float ibat = 0, ichg = 0, idchg = 0, ibus = 0, ts = 0, die = 0, temp = 0;
  uint64_t irq = 0;
  uint16_t pd_alert = 0;

  BatteryLogPrintf("\n========== AXP517 official driver status ==========\n");
  if (chip.GetChipId(id))
    BatteryLogPrintf("chip id: 0x%02X  extended id: 0x%02X\n", id.chip_id, id.extended_id);
  else
    BatteryLogPrintf("chip id: read failed\n");
  if (chip.GetStatus(status)) {
    BatteryLogPrintf("status: battery=%s  vbus=%s  system=%s\n",
        status.battery_present ? "present" : "absent",
        status.vbus_good ? "good" : "absent",
        status.system_on ? "on" : "off");
    BatteryLogPrintf("charge: %s  battery-current-direction: %s\n",
        ChargeName(status.charge), DirectionName(status.current_direction));
  } else {
    BatteryLogPrintf("status: read failed\n");
  }
  if (chip.GetFaultStatus(fault)) {
    BatteryLogPrintf("fault: ntc=%u  vsys-overvoltage=%s  vbat-undervoltage=%s\n",
        static_cast<unsigned>(fault.ntc), fault.system_overvoltage ? "yes" : "no",
        fault.battery_undervoltage ? "yes" : "no");
  }
  if (chip.GetBatteryLevel(soc)) BatteryLogPrintf("gauge soc: %u %%\n", soc);
  if (chip.GetBatterySoh(soh)) BatteryLogPrintf("gauge soh: %u %%\n", soh);
  if (chip.GetCycleCount(cycle)) BatteryLogPrintf("cycle count: %u\n", cycle);
  if (chip.GetBatteryVoltage(vbat)) BatteryLogPrintf("vbat: %u mV\n", vbat);
  if (chip.GetSystemVoltage(vsys)) BatteryLogPrintf("vsys: %u mV\n", vsys);
  if (chip.GetVbusVoltage(vbus)) BatteryLogPrintf("vbus: %u mV\n", vbus);
  if (chip.GetBatteryCurrent(ibat)) BatteryLogPrintf("ibat: %.2f mA\n", ibat);
  if (chip.GetChargingCurrent(ichg)) BatteryLogPrintf("ichg adc: %.2f mA\n", ichg);
  if (chip.GetDischargingCurrent(idchg)) BatteryLogPrintf("idchg adc: %.2f mA\n", idchg);
  if (chip.GetVbusCurrent(ibus)) BatteryLogPrintf("ibus: %.2f mA\n", ibus);
  if (chip.GetTsVoltage(ts)) BatteryLogPrintf("ts voltage: %.2f mV\n", ts);
  if (chip.GetDieTemperature(die)) BatteryLogPrintf("die temperature: %.1f C\n", die);
  if (chip.GetBatteryTemperature(temp)) BatteryLogPrintf("battery temperature: %.1f C\n", temp);
  if (chip.GetIrqStatus(irq)) BatteryLogPrintf("power irq: 0x%010llX\n", static_cast<unsigned long long>(irq));
  if (chip.GetPdAlerts(pd_alert)) BatteryLogPrintf("pd alert: 0x%04X\n", pd_alert);
}
}  // namespace

void RunAxp517Example() {
  BatteryLogPrintf("AXP517 battery management example\n");
  auto& driver = common::GetDriver();
  if (!driver.IsAxp517Ready()) {
    BatteryLogPrintf("AXP517 init failed\n");
    return;
  }
  auto& chip = driver.chip().axp517;
  if (chip == nullptr) {
    BatteryLogPrintf("AXP517 driver unavailable\n");
    return;
  }
  constexpr uint8_t kAdcChannels =
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kBatteryVoltage) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kTs) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kVbusVoltage) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kSystemVoltage) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kDieTemperature) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kChargeCurrent) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kDischargeCurrent) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kVbusCurrent);
  if (!chip->SetAdcChannels(kAdcChannels)) {
    BatteryLogPrintf("AXP517 ADC configuration failed\n");
    return;
  }
  while (true) {
    BatteryLogBeginSnapshot();
    PrintAxp517(*chip);
    BatteryLogEndSnapshot();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

#endif
