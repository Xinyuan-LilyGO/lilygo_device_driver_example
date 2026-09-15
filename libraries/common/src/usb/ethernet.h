#pragma once

#include "esp_err.h"
#include "usb/usb_host.h"

namespace common::usb_ethernet {

// T-Display-P4 V2.0 板载 Hub 上的网卡位置。
enum class Adapter {
  kOnboard,
  kExternal,
};

struct Config {
  Adapter adapter = Adapter::kOnboard;
  // 软件 MAC 同时启用混杂接收，避免芯片内部 MAC 不一致导致丢包。
  bool use_software_mac = false;
};

/**
 * @brief 为 RTL8152B 选择 ECM 配置 2，其他 USB 设备选择配置 1
 * @param descriptor USB 设备描述符
 * @param configuration_value 输出要使用的 USB 配置编号
 * @return 配置可用返回 true，否则返回 false
 */
bool SelectUsbConfiguration(
    const usb_device_desc_t* descriptor, uint8_t* configuration_value);

/**
 * @brief 安装单个 RTL8152B 网卡并绑定网络接口，自动处理链路和 DHCP
 * @param config 网卡位置与 MAC 来源配置
 * @return 成功返回 ESP_OK，否则返回错误码
 * @note 调用前须初始化 ESP-NETIF、默认事件循环和 USB Host，并使用
 * SelectUsbConfiguration 作为枚举回调。每次启动仅调用一次，失败时由应用终止启动。
 */
esp_err_t Init(const Config& config);

/**
 * @brief 阻塞当前任务，直到本网卡获取 IPv4 地址；须在 Init 成功后调用
 */
void WaitForIp();

}  // namespace common::usb_ethernet
