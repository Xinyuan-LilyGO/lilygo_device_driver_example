/*
 * @Description: 应用公共 AXP517 PD 服务接口
 * @Author: LILYGO_L
 * @Date: 2026-09-22 14:14:36
 * @LastEditTime: 2026-09-22 14:14:36
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "chip/i2c/axp517.h"

namespace common {

struct Axp517PdSnapshot {
  cpp_bus_driver::Axp517Sink::Status status;
  bool external_battery_selected = false;
  bool service_running = false;
};

/**
 * @brief 启动应用层 AXP517 PD 轮询任务；重复调用不会创建第二个任务。
 * @param external_charge_current_ma 外置电池充电电流，单位 mA。
 * @return 任务已启动返回 true，否则返回 false。
 */
bool StartAxp517PdService(uint16_t external_charge_current_ma = 1024);

/**
 * @brief 停止应用层 PD 任务；关闭 AXP517 电源前必须先调用。
 * @return 任务已停止返回 true，否则返回 false。
 */
bool StopAxp517PdService();

/**
 * @brief 请求应用层 PD 任务在错误后重新发起一次协商，不自动循环重试。
 * @return 错误状态下已接受请求返回 true，否则返回 false。
 */
bool RequestAxp517PdRestart();

/**
 * @brief 读取应用层 PD 状态快照。
 * @param snapshot 用于接收状态快照。
 * @return 服务已初始化返回 true，否则返回 false。
 */
bool GetAxp517PdSnapshot(Axp517PdSnapshot& snapshot);

}  // namespace common
