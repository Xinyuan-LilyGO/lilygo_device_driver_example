#pragma once

#include "iot_usbh_cdc.h"
#include "ethernet.h"

namespace common::usb_ethernet::internal {

/**
 * @brief 安装按板载 Hub 分支筛选 RTL8152B 的 CDC 驱动
 * @param config CDC 驱动配置
 * @param adapter 网卡所在分支
 * @return 成功返回 ESP_OK，否则返回错误码
 */
esp_err_t InstallCdc(const usbh_cdc_driver_config_t* config, Adapter adapter);

/**
 * @brief 网络接口绑定并启动后，放行 CDC 设备通知
 */
void EnableDeviceEvents();

/**
 * @brief 配置软件 MAC 所需的混杂接收，须在安装 ECM 驱动前调用
 * @param enabled 是否启用混杂接收
 */
void EnableEcmPromiscuousMode(bool enabled);

}  // namespace common::usb_ethernet::internal
