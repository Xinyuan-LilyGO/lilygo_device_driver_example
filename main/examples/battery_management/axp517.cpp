/*
 * @Description: AXP517 充电、电池状态、ADC 与中断监测实现
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-03 16:57:00
 * @License: GPL 3.0
 */
#include "battery_management.h"
#include "chip/i2c/axp517.h"
#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    (defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) &&    \
        defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2))

namespace {

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)

// 将协商状态转换为串口与屏幕共用的文本。
const char* SinkStateName(cpp_bus_driver::Axp517Sink::State state) {
  using State = cpp_bus_driver::Axp517Sink::State;
  switch (state) {
    case State::kDisabled:
      return "disabled (waiting for Type-C attach / battery)";
    case State::kWaitingCapabilities:
      return "waiting for source capabilities";
    case State::kWaitingAccept:
      return "waiting for Accept";
    case State::kWaitingPowerReady:
      return "waiting for PS_RDY";
    case State::kWaitingVoltageStable:
      return "waiting for PMIC VBUS to settle";
    case State::kReady:
      return "contract ready";
    case State::kError:
      return "failed; conservative charging (reconnect or request restart)";
  }
  return "unknown";
}

const char* PsReadyResultName(cpp_bus_driver::Axp517Sink::PsReadyResult result) {
  using Result = cpp_bus_driver::Axp517Sink::PsReadyResult;
  switch (result) {
    case Result::kNotSeen:
      return "not seen";
    case Result::kUnexpectedState:
      return "unexpected state";
    case Result::kTcpcReadFailed:
      return "TCPC voltage read failed";
    case Result::kVoltageMismatch:
      return "PMIC VBUS mismatch after settling";
    case Result::kVoltageSettling:
      return "waiting for PMIC VBUS to settle";
    case Result::kInputVoltageConfigFailed:
      return "input voltage config failed";
    case Result::kInputCurrentConfigFailed:
      return "input current config failed";
    case Result::kChargeCurrentConfigFailed:
      return "charge current config failed";
    case Result::kReady:
      return "contract ready";
  }
  return "unknown";
}

// 状态复制期间短暂加锁，格式化输出不阻塞协议任务。
void PrintSink() {
  common::Axp517PdSnapshot snapshot;
  if (!common::GetAxp517PdSnapshot(snapshot)) {
    BatteryLogPrintf("\nBattery PD / PPS: service unavailable\n");
    return;
  }
  const auto& status = snapshot.status;
  BatteryLogPrintf("\nBattery PD / PPS:\n");
  BatteryLogPrintf("  Selected battery: %s\n",
      snapshot.external_battery_selected ? "external" : "internal");
  BatteryLogPrintf("  PD service running: %s\n",
      snapshot.service_running ? "yes" : "no");
  BatteryLogPrintf("  PD enabled by board: %s, battery present: %s\n",
      status.enabled ? "yes" : "no",
      status.battery_present ? "yes" : "no");
  BatteryLogPrintf("  Type-C attached: %s\n", status.attached ? "yes" : "no");
  BatteryLogPrintf("  State: %s\n", SinkStateName(status.state));
  if (status.state == cpp_bus_driver::Axp517Sink::State::kError) {
    BatteryLogPrintf("  Failed from: %s\n",
        SinkStateName(status.failure_stage));
  }
  BatteryLogPrintf("  Last PD alerts: 0x%04X\n", status.last_pd_alerts);
  BatteryLogPrintf("  PD RX: %u, SourceCaps: %u, Request TX: %u\n",
      status.rx_count, status.source_caps_count, status.request_count);
  BatteryLogPrintf("  Accept: %u, PS_RDY: %u, TX failed: %u\n",
      status.accept_count, status.ps_ready_count, status.tx_failed_count);
  BatteryLogPrintf("  TCPC faults: %u, last fault: 0x%02X\n",
      status.fault_count, status.last_fault_status);
  BatteryLogPrintf("  Last request: PDO%u %s, %u mV, %u mA\n",
      status.requested_pdo, status.requested_pps ? "PPS" : "fixed",
      status.requested_voltage_mv, status.requested_current_ma);
  BatteryLogPrintf("  First PS_RDY: %s (PDO%u, target %u mV)\n",
      PsReadyResultName(status.first_ps_ready_result),
      status.first_ps_ready_pdo, status.first_ps_ready_target_mv);
  if (status.first_ps_ready_seen) {
    if (status.first_ps_ready_pmic_valid) {
      BatteryLogPrintf("  At PS_RDY: PMIC %u mV, TCPC diag %u mV\n",
          status.first_ps_ready_pmic_mv, status.first_ps_ready_tcpc_mv);
    } else {
      BatteryLogPrintf("  At PS_RDY: PMIC read failed, TCPC diag %u mV\n",
          status.first_ps_ready_tcpc_mv);
    }
    if (status.first_ps_ready_result !=
        cpp_bus_driver::Axp517Sink::PsReadyResult::kVoltageSettling) {
      BatteryLogPrintf("  After settling: PMIC %s %u mV, TCPC diag %s %u mV\n",
          status.settled_pmic_valid ? "" : "read failed /",
          status.settled_pmic_mv,
          status.settled_tcpc_valid ? "" : "read failed /",
          status.settled_tcpc_mv);
    }
  }
  BatteryLogPrintf("  Contract: %s, %u mV, %u mA\n",
      status.pps ? "PPS" : "fixed / none", status.voltage_mv,
      status.current_ma);
  if (status.charge_current_managed) {
    BatteryLogPrintf("  PD charge current limit: %u mA\n",
        status.charge_current_ma);
  } else {
    BatteryLogPrintf("  PD charge current limit: managed by caller\n");
  }
}

#endif

// 分类之间保留空行，分类中的项目统一缩进两个空格。
void PrintSection(const char* title) {
  BatteryLogPrintf("\n%s:\n", title);
}

// 将充电阶段转换为显示文本。
const char* ChargeName(cpp_bus_driver::Axp517::ChargeStatus value) {
  using Value = cpp_bus_driver::Axp517::ChargeStatus;
  switch (value) {
    case Value::kTrickleCharge:
      return "trickle";
    case Value::kPrecharge:
      return "precharge";
    case Value::kConstantCurrent:
      return "constant current";
    case Value::kConstantVoltage:
      return "constant voltage";
    case Value::kChargeDone:
      return "done";
    case Value::kNotCharging:
      return "not charging";
    default:
      return "unknown";
  }
}

// 将电池电流方向转换为显示文本。
const char* DirectionName(
    cpp_bus_driver::Axp517::BatteryCurrentDirection value) {
  using Value = cpp_bus_driver::Axp517::BatteryCurrentDirection;
  switch (value) {
    case Value::kStandby:
      return "standby";
    case Value::kCharge:
      return "charge";
    case Value::kDischarge:
      return "discharge";
    default:
      return "unknown";
  }
}

// 将电池健康状态转换为显示文本。
const char* HealthName(cpp_bus_driver::Axp517::BatteryHealth value) {
  using Value = cpp_bus_driver::Axp517::BatteryHealth;
  switch (value) {
    case Value::kUnknown:
      return "unknown";
    case Value::kGood:
      return "good";
    case Value::kCold:
      return "cold";
    case Value::kOverheat:
      return "overheat";
    case Value::kOvervoltage:
      return "overvoltage";
    case Value::kSafetyTimerExpired:
      return "safety timer expired";
    default:
      return "unknown";
  }
}

// 将电池电量等级转换为显示文本。
const char* CapacityName(cpp_bus_driver::Axp517::CapacityLevel value) {
  using Value = cpp_bus_driver::Axp517::CapacityLevel;
  switch (value) {
    case Value::kCritical:
      return "critical";
    case Value::kLow:
      return "low";
    case Value::kNormal:
      return "normal";
    case Value::kHigh:
      return "high";
    case Value::kFull:
      return "full";
    default:
      return "unknown";
  }
}

// 将 USB 输入类型转换为显示文本。
const char* Bc12Name(cpp_bus_driver::Axp517::Bc12Result value) {
  using Value = cpp_bus_driver::Axp517::Bc12Result;
  switch (value) {
    case Value::kUnknown:
      return "unknown";
    case Value::kSdp:
      return "SDP";
    case Value::kCdp:
      return "CDP";
    case Value::kDcp:
      return "DCP";
    default:
      return "unknown";
  }
}

// 将 CC 引脚状态转换为显示文本。
const char* CcName(cpp_bus_driver::Axp517::CcState value) {
  using Value = cpp_bus_driver::Axp517::CcState;
  switch (value) {
    case Value::kOpen:
      return "open";
    case Value::kRa:
      return "Ra";
    case Value::kRd:
      return "Rd";
    case Value::kRpDefault:
      return "Rp default current";
    case Value::kRp1500Ma:
      return "Rp 1.5 A";
    case Value::kRp3000Ma:
      return "Rp 3.0 A";
    default:
      return "unknown";
  }
}

// 将电池温度故障转换为显示文本。
const char* NtcFaultName(cpp_bus_driver::Axp517::NtcFault value) {
  using Value = cpp_bus_driver::Axp517::NtcFault;
  switch (value) {
    case Value::kNormal:
      return "normal";
    case Value::kColdCharge:
      return "cold during charging";
    case Value::kHotCharge:
      return "hot during charging";
    case Value::kColdWork:
      return "cold during operation";
    case Value::kHotWork:
      return "hot during operation";
    case Value::kUnknown:
      return "unknown";
    default:
      return "unknown";
  }
}

// 只读取并展示状态，不在采样过程中调整充电参数或清除中断。
void PrintAxp517(cpp_bus_driver::Axp517& chip) {
  BatteryLogPrintf("\nAXP517 snapshot\n");
  PrintSection("Chip information");
  cpp_bus_driver::Axp517::ChipId id;
  if (chip.GetChipId(id)) {
    BatteryLogPrintf("  Chip ID: 0x%02X\n", id.chip_id);
    BatteryLogPrintf("  Extended ID: 0x%02X\n", id.extended_id);
  } else {
    BatteryLogPrintf("  Chip ID: read failed\n");
  }
  cpp_bus_driver::Axp517::TcpcId tcpc_id;
  if (chip.GetTcpcId(tcpc_id)) {
    BatteryLogPrintf("  TCPC vendor ID: 0x%04X\n", tcpc_id.vendor_id);
    BatteryLogPrintf("  TCPC product ID: 0x%04X\n", tcpc_id.product_id);
    BatteryLogPrintf("  TCPC device revision: 0x%04X\n",
        tcpc_id.device_revision);
    BatteryLogPrintf("  Type-C revision: 0x%04X\n", tcpc_id.type_c_revision);
    BatteryLogPrintf("  TCPC PD revision: 0x%04X\n", tcpc_id.pd_revision);
    BatteryLogPrintf("  TCPC interface revision: 0x%04X\n",
        tcpc_id.interface_revision);
  } else {
    BatteryLogPrintf("  TCPC ID: read failed\n");
  }

  PrintSection("Power status");
  cpp_bus_driver::Axp517::Status status;
  if (chip.GetStatus(status)) {
    BatteryLogPrintf("  Battery present: %s\n",
        status.battery_present ? "yes" : "no");
    BatteryLogPrintf("  Battery active: %s\n",
        status.battery_active ? "yes" : "no");
    BatteryLogPrintf("  VBUS good: %s\n",
        status.vbus_good ? "yes" : "no");
    BatteryLogPrintf("  System on: %s\n",
        status.system_on ? "yes" : "no");
    BatteryLogPrintf("  BATFET on: %s\n",
        status.batfet_on ? "yes" : "no");
    BatteryLogPrintf("  Input current limited: %s\n",
        status.current_limited ? "yes" : "no");
    BatteryLogPrintf("  Input voltage regulation active: %s\n",
        status.vindpm_active ? "yes" : "no");
    BatteryLogPrintf("  Thermal regulation active: %s\n",
        status.thermal_regulation ? "yes" : "no");
    BatteryLogPrintf("  Charge stage: %s\n", ChargeName(status.charge));
    BatteryLogPrintf("  Battery current direction: %s\n",
        DirectionName(status.current_direction));
  } else {
    BatteryLogPrintf("  Power status: read failed\n");
  }
  cpp_bus_driver::Axp517::Bc12Result bc12;
  if (chip.GetBc12Result(bc12)) {
    BatteryLogPrintf("  USB BC1.2 type: %s\n", Bc12Name(bc12));
  } else {
    BatteryLogPrintf("  USB BC1.2 type: read failed\n");
  }

  PrintSection("Charging configuration");
  uint16_t value = 0;
  if (chip.GetInputCurrentLimit(value)) {
    BatteryLogPrintf("  Input current limit: %u mA\n", value);
  } else {
    BatteryLogPrintf("  Input current limit: read failed\n");
  }
  if (chip.GetInputVoltageLimit(value)) {
    BatteryLogPrintf("  Input voltage limit: %u mV\n", value);
  } else {
    BatteryLogPrintf("  Input voltage limit: read failed\n");
  }
  if (chip.GetChargeCurrent(value)) {
    BatteryLogPrintf("  Charge current setting: %u mA\n", value);
  } else {
    BatteryLogPrintf("  Charge current setting: read failed\n");
  }
  if (chip.GetChargeVoltage(value)) {
    BatteryLogPrintf("  Charge voltage setting: %u mV\n", value);
  } else {
    BatteryLogPrintf("  Charge voltage setting: read failed\n");
  }
  if (chip.GetBoostVoltage(value)) {
    BatteryLogPrintf("  Boost voltage setting: %u mV\n", value);
  } else {
    BatteryLogPrintf("  Boost voltage setting: read failed\n");
  }

  PrintSection("Battery gauge");
  uint8_t percent = 0;
  if (chip.GetBatteryLevel(percent)) {
    BatteryLogPrintf("  Displayed state of charge: %u %%\n", percent);
  } else {
    BatteryLogPrintf("  Displayed state of charge: read failed\n");
  }
  if (chip.GetGaugeSoc(percent)) {
    BatteryLogPrintf("  Gauge state of charge: %u %%\n", percent);
  } else {
    BatteryLogPrintf("  Gauge state of charge: read failed\n");
  }
  if (chip.GetBatterySoh(percent)) {
    BatteryLogPrintf("  State of health: %u %%\n", percent);
  } else {
    BatteryLogPrintf("  State of health: read failed\n");
  }
  if (chip.GetCycleCount(value)) {
    BatteryLogPrintf("  Cycle count: %u\n", value);
  } else {
    BatteryLogPrintf("  Cycle count: read failed\n");
  }
  cpp_bus_driver::Axp517::BatteryHealth health;
  if (chip.GetBatteryHealth(health)) {
    BatteryLogPrintf("  Battery health: %s\n", HealthName(health));
  } else {
    BatteryLogPrintf("  Battery health: read failed\n");
  }
  cpp_bus_driver::Axp517::CapacityLevel capacity;
  if (chip.GetCapacityLevel(capacity)) {
    BatteryLogPrintf("  Capacity level: %s\n", CapacityName(capacity));
  } else {
    BatteryLogPrintf("  Capacity level: read failed\n");
  }
  uint8_t warning = 0;
  uint8_t shutdown = 0;
  if (chip.GetGaugeThresholds(warning, shutdown)) {
    BatteryLogPrintf("  Low battery warning threshold: %u %%\n", warning);
    BatteryLogPrintf("  Shutdown threshold: %u %%\n", shutdown);
  } else {
    BatteryLogPrintf("  Gauge thresholds: read failed\n");
  }
  bool model_updated = false;
  if (chip.IsBatteryModelUpdated(model_updated)) {
    BatteryLogPrintf("  Battery model updated: %s\n",
        model_updated ? "yes" : "no");
  } else {
    BatteryLogPrintf("  Battery model updated: read failed\n");
  }

  PrintSection("Voltage and current measurements");
  if (chip.GetBatteryVoltage(value)) {
    BatteryLogPrintf("  Battery voltage: %u mV\n", value);
  } else {
    BatteryLogPrintf("  Battery voltage: read failed\n");
  }
  if (chip.GetSystemVoltage(value)) {
    BatteryLogPrintf("  System voltage: %u mV\n", value);
  } else {
    BatteryLogPrintf("  System voltage: read failed\n");
  }
  if (chip.GetVbusVoltage(value)) {
    BatteryLogPrintf("  VBUS voltage: %u mV\n", value);
  } else {
    BatteryLogPrintf("  VBUS voltage: read failed\n");
  }
  float measurement = 0.0f;
  if (chip.GetBatteryCurrent(measurement)) {
    BatteryLogPrintf("  Battery current: %.2f mA\n", measurement);
  } else {
    BatteryLogPrintf("  Battery current: read failed\n");
  }
  if (chip.GetBatteryAverageCurrent(measurement)) {
    BatteryLogPrintf("  Average battery current: %.2f mA\n", measurement);
  } else {
    BatteryLogPrintf("  Average battery current: read failed\n");
  }
  if (chip.GetChargingCurrent(measurement)) {
    BatteryLogPrintf("  Charging current ADC: %.2f mA\n", measurement);
  } else {
    BatteryLogPrintf("  Charging current ADC: read failed\n");
  }
  if (chip.GetDischargingCurrent(measurement)) {
    BatteryLogPrintf("  Discharging current ADC: %.2f mA\n", measurement);
  } else {
    BatteryLogPrintf("  Discharging current ADC: read failed\n");
  }
  if (chip.GetVbusCurrent(measurement)) {
    BatteryLogPrintf("  VBUS current: %.2f mA\n", measurement);
  } else {
    BatteryLogPrintf("  VBUS current: read failed\n");
  }

  PrintSection("Temperature");
  if (chip.GetTsVoltage(measurement)) {
    BatteryLogPrintf("  TS voltage: %.2f mV\n", measurement);
  } else {
    BatteryLogPrintf("  TS voltage: read failed\n");
  }
  if (chip.GetDieTemperature(measurement)) {
    BatteryLogPrintf("  Die temperature: %.1f C\n", measurement);
  } else {
    BatteryLogPrintf("  Die temperature: read failed\n");
  }
  if (chip.GetBatteryTemperature(measurement)) {
    BatteryLogPrintf("  Battery temperature: %.1f C\n", measurement);
  } else {
    BatteryLogPrintf("  Battery temperature: unavailable or read failed\n");
  }

  PrintSection("Type-C status");
  cpp_bus_driver::Axp517::CcStatus cc;
  if (chip.GetCcStatus(cc)) {
    BatteryLogPrintf("  CC1: %s\n", CcName(cc.cc1));
    BatteryLogPrintf("  CC2: %s\n", CcName(cc.cc2));
    BatteryLogPrintf("  Looking for connection: %s\n",
        cc.looking_for_connection ? "yes" : "no");
    BatteryLogPrintf("  Attached as Sink: %s\n",
        cc.sink_attached ? "yes" : "no");
    BatteryLogPrintf("  Attached as Source: %s\n",
        cc.source_attached ? "yes" : "no");
    BatteryLogPrintf("  Audio accessory: %s\n",
        cc.audio_accessory ? "yes" : "no");
    BatteryLogPrintf("  Debug accessory: %s\n",
        cc.debug_accessory ? "yes" : "no");
  } else {
    BatteryLogPrintf("  CC status: read failed\n");
  }
  cpp_bus_driver::Axp517::TcpcStatus tcpc;
  if (chip.GetTcpcStatus(tcpc)) {
    BatteryLogPrintf("  TCPC VBUS present: %s\n",
        tcpc.vbus_present ? "yes" : "no");
    BatteryLogPrintf("  Sourcing VBUS: %s\n",
        tcpc.sourcing_vbus ? "yes" : "no");
    BatteryLogPrintf("  Sinking VBUS: %s\n",
        tcpc.sinking_vbus ? "yes" : "no");
    BatteryLogPrintf("  VCONN present: %s\n",
        tcpc.vconn_present ? "yes" : "no");
    BatteryLogPrintf("  VBUS safe 0 V: %s\n",
        tcpc.vbus_safe0v ? "yes" : "no");
    BatteryLogPrintf("  TCPC power status: 0x%02X\n", tcpc.power);
    BatteryLogPrintf("  TCPC fault status: 0x%02X\n", tcpc.fault);
    BatteryLogPrintf("  TCPC extended status: 0x%02X\n", tcpc.extended_status);
    BatteryLogPrintf("  TCPC extended alert: 0x%02X\n", tcpc.extended_alert);
  } else {
    BatteryLogPrintf("  TCPC status: read failed\n");
  }
  if (chip.GetTcpcVbusVoltage(value)) {
    BatteryLogPrintf("  TCPC VBUS voltage: %u mV\n", value);
  } else {
    BatteryLogPrintf("  TCPC VBUS voltage: read failed\n");
  }

  PrintSection("Interrupts and faults");
  cpp_bus_driver::Axp517::FaultStatus fault;
  if (chip.GetFaultStatus(fault)) {
    BatteryLogPrintf("  NTC fault: %s\n", NtcFaultName(fault.ntc));
    BatteryLogPrintf("  System overvoltage: %s\n",
        fault.system_overvoltage ? "yes" : "no");
    BatteryLogPrintf("  Battery undervoltage: %s\n",
        fault.battery_undervoltage ? "yes" : "no");
  } else {
    BatteryLogPrintf("  Fault status: read failed\n");
  }
  uint64_t irq = 0;
  if (chip.GetIrqStatus(irq)) {
    BatteryLogPrintf("  Power IRQ status (latched): 0x%010llX\n",
        static_cast<unsigned long long>(irq));
  } else {
    BatteryLogPrintf("  Power IRQ status (latched): read failed\n");
  }
  if (chip.GetIrqEnable(irq)) {
    BatteryLogPrintf("  Power IRQ enable mask: 0x%010llX\n",
        static_cast<unsigned long long>(irq));
  } else {
    BatteryLogPrintf("  Power IRQ enable mask: read failed\n");
  }
  if (chip.GetPdAlerts(value)) {
    BatteryLogPrintf("  PD alerts (latched): 0x%04X\n", value);
  } else {
    BatteryLogPrintf("  PD alerts (latched): read failed\n");
  }
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
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  BatteryLogPrintf("External charge current target: %u mA\n",
      kExternalChargeCurrentMa);
#endif
  constexpr uint8_t kAdcChannels =
      static_cast<uint8_t>(
          cpp_bus_driver::Axp517::AdcChannel::kBatteryVoltage) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kTs) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kVbusVoltage) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kSystemVoltage) |
      static_cast<uint8_t>(
          cpp_bus_driver::Axp517::AdcChannel::kDieTemperature) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kChargeCurrent) |
      static_cast<uint8_t>(
          cpp_bus_driver::Axp517::AdcChannel::kDischargeCurrent) |
      static_cast<uint8_t>(cpp_bus_driver::Axp517::AdcChannel::kVbusCurrent);
  if (!chip->SetAdcChannels(kAdcChannels)) {
    BatteryLogPrintf("AXP517 ADC configuration failed\n");
    return;
  }
  while (true) {
    BatteryLogBeginSnapshot();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
    PrintSink();
#endif
    PrintAxp517(*chip);
    BatteryLogEndSnapshot();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

#endif
