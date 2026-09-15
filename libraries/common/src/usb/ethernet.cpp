#include "ethernet.h"

#include <inttypes.h>
#include <cstdlib>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "iot_eth.h"
#include "iot_usbh_ecm.h"
#include "ethernet_transport.h"

namespace common::usb_ethernet {
namespace {

constexpr char kTag[] = "usb_ethernet";
bool use_software_mac = false;

constexpr uint16_t kRtl8152bVid = 0x0BDA;
constexpr uint16_t kRtl8152bPid = 0x8152;
constexpr EventBits_t kGotIpBit = BIT0;
constexpr EventBits_t kNetifReadyBit = BIT1;

EventGroupHandle_t event_group = nullptr;
esp_netif_driver_base_t netif_driver = {};
iot_eth_handle_t eth_handle = nullptr;
bool started = false;
bool link_up = false;

/**
 * @brief 将 ECM 接收缓冲区交给网络栈，由网络栈释放
 */
esp_err_t ReceiveEthernetFrame(iot_eth_handle_t handle,
    uint8_t* buffer, size_t length, void* context) {
  (void)handle;
  return esp_netif_receive(static_cast<esp_netif_t*>(context), buffer,
      length, nullptr);
}

/**
 * @brief 释放 ECM 分配的接收缓冲区
 */
void FreeEthernetFrame(void* handle, void* buffer) {
  (void)handle;
  free(buffer);
}

/**
 * @brief 绑定 ECM 收发接口，链路事件由本模块统一处理
 */
esp_err_t AttachEthernetNetif(esp_netif_t* netif, void* args) {
  auto* driver = static_cast<esp_netif_driver_base_t*>(args);
  driver->netif = netif;
  esp_netif_driver_ifconfig_t config = {};
  config.handle = eth_handle;
  config.transmit = iot_eth_transmit;
  config.driver_free_rx_buffer = FreeEthernetFrame;
  ESP_RETURN_ON_ERROR(esp_netif_set_driver_config(netif, &config), kTag,
      "Set Ethernet network driver failed");
  return iot_eth_update_input_path(
      eth_handle, ReceiveEthernetFrame, netif);
}

/**
 * @brief 验证网卡 MAC 后启动 DHCP，拒绝全零和组播地址
 */
void ConnectEthernetNetif() {
  if (!started || !link_up) {
    return;
  }
  uint8_t mac[6] = {};
  const char* mac_source = use_software_mac ? "ESP32-P4 eFuse" : "ECM";
  const esp_err_t result = use_software_mac
      ? esp_efuse_mac_get_default(mac) : iot_eth_get_addr(eth_handle, mac);
  uint8_t nonzero = 0;
  for (uint8_t byte : mac) {
    nonzero |= byte;
  }
  if (result != ESP_OK || nonzero == 0 || (mac[0] & 1) != 0) {
    ESP_LOGE(kTag, "Cannot start DHCP: %s MAC=%02X:%02X:%02X:%02X:%02X:%02X, "
                  "read=%s",
        mac_source, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        esp_err_to_name(result));
    return;
  }
  if (use_software_mac) {
    // 只派生网络栈使用的本地管理单播地址，不烧写任何芯片的 eFuse。
    mac[0] = static_cast<uint8_t>((mac[0] | 0x02) & 0xFE);
  }
  const esp_err_t mac_result = esp_netif_set_mac(netif_driver.netif, mac);
  if (mac_result != ESP_OK) {
    ESP_LOGE(kTag, "Cannot start DHCP: set MAC failed: %s",
        esp_err_to_name(mac_result));
    return;
  }
  ESP_LOGI(kTag, "Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X (source=%s); "
               "starting DHCP",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac_source);
  esp_netif_action_connected(netif_driver.netif, IOT_ETH_EVENT,
      IOT_ETH_EVENT_CONNECTED, &eth_handle);
#if CONFIG_LWIP_IPV6
  const esp_err_t ipv6_result =
      esp_netif_create_ip6_linklocal(netif_driver.netif);
  if (ipv6_result != ESP_OK) {
    ESP_LOGW(kTag, "Create IPv6 link-local address failed: %s",
        esp_err_to_name(ipv6_result));
  }
#endif
}

void EthernetEventHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data) {
  (void)arg;
  if (event_base == IOT_ETH_EVENT) {
    // ECM 可在安装期间上报链路；等待绑定完成，避免访问未初始化的 netif。
    xEventGroupWaitBits(event_group, kNetifReadyBit, pdFALSE,
        pdTRUE, portMAX_DELAY);
    if (event_data == nullptr ||
        *static_cast<iot_eth_handle_t*>(event_data) != eth_handle) {
      return;
    }
    switch (event_id) {
      case IOT_ETH_EVENT_START:
        ESP_LOGI(kTag, "IOT_ETH_EVENT_START");
        esp_netif_action_start(
            netif_driver.netif, event_base, event_id, event_data);
        started = true;
        ConnectEthernetNetif();
        break;
      case IOT_ETH_EVENT_STOP:
        ESP_LOGI(kTag, "IOT_ETH_EVENT_STOP");
        started = false;
        link_up = false;
        xEventGroupClearBits(event_group, kGotIpBit);
        esp_netif_action_stop(
            netif_driver.netif, event_base, event_id, event_data);
        break;
      case IOT_ETH_EVENT_CONNECTED:
        ESP_LOGI(kTag, "RTL8152B link up");
        link_up = true;
        ConnectEthernetNetif();
        break;
      case IOT_ETH_EVENT_DISCONNECTED:
        ESP_LOGI(kTag, "RTL8152B link down");
        link_up = false;
        xEventGroupClearBits(event_group, kGotIpBit);
        if (started) {
          esp_netif_action_disconnected(
              netif_driver.netif, event_base, event_id, event_data);
        }
        break;
      default:
        ESP_LOGI(kTag, "IOT_ETH_EVENT id=%" PRId32, event_id);
        break;
    }
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP &&
      event_data != nullptr) {
    const ip_event_got_ip_t* event =
        static_cast<const ip_event_got_ip_t*>(event_data);
    if (event->esp_netif != netif_driver.netif) {
      return;
    }
    ESP_LOGI(kTag, "RTL8152B got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(kTag, "Gateway: " IPSTR ", Netmask: " IPSTR,
        IP2STR(&event->ip_info.gw), IP2STR(&event->ip_info.netmask));
    xEventGroupSetBits(event_group, kGotIpBit);
  }
}

esp_err_t InstallEcm() {
  static usb_device_match_id_t dev_match_id[2] = {};
  dev_match_id[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
  dev_match_id[0].idVendor = kRtl8152bVid;
  dev_match_id[0].idProduct = kRtl8152bPid;

  iot_usbh_ecm_config_t ecm_cfg = {
      .match_id_list = dev_match_id,
  };

  iot_eth_driver_t* ecm_driver = nullptr;
  esp_err_t ret = iot_eth_new_usb_ecm(&ecm_cfg, &ecm_driver);
  if (ret != ESP_OK) {
    return ret;
  }

  iot_eth_config_t eth_cfg = {
      .driver = ecm_driver,
      .stack_input = nullptr,
      .stack_input_info = nullptr,
  };
  ESP_RETURN_ON_ERROR(
      iot_eth_install(&eth_cfg, &eth_handle), kTag, "install iot_eth failed");

  esp_netif_inherent_config_t inherent_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
  inherent_cfg.if_key = "USBECM";
  inherent_cfg.if_desc = "rtl8152b";
  inherent_cfg.route_prio = 64;

  esp_netif_config_t netif_cfg = {
      .base = &inherent_cfg,
      .driver = nullptr,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };

  esp_netif_t* ecm_netif = esp_netif_new(&netif_cfg);
  ESP_RETURN_ON_FALSE(
      ecm_netif != nullptr, ESP_FAIL, kTag, "create USB ECM netif failed");
  ESP_RETURN_ON_ERROR(esp_netif_set_default_netif(ecm_netif), kTag,
      "Set default netif failed");

  netif_driver.post_attach = AttachEthernetNetif;
  ESP_RETURN_ON_ERROR(
      esp_netif_attach(ecm_netif, &netif_driver), kTag, "attach netif failed");
  xEventGroupSetBits(event_group, kNetifReadyBit);

  return iot_eth_start(eth_handle);
}

}  // namespace

bool SelectUsbConfiguration(
    const usb_device_desc_t* dev_desc, uint8_t* configuration_value) {
  if (dev_desc == nullptr || configuration_value == nullptr) {
    return false;
  }
  if (dev_desc->idVendor == kRtl8152bVid &&
      dev_desc->idProduct == kRtl8152bPid) {
    if (dev_desc->bNumConfigurations < 2) {
      ESP_LOGE(kTag, "RTL8152B does not advertise ECM configuration 2");
      return false;
    }
    *configuration_value = 2;
    ESP_LOGI(kTag, "USB device VID:%04X PID:%04X use config %u",
        dev_desc->idVendor, dev_desc->idProduct, *configuration_value);
    return true;
  }

  *configuration_value = 1;
  ESP_LOGI(kTag, "USB device VID:%04X PID:%04X use config %u",
      dev_desc->idVendor, dev_desc->idProduct, *configuration_value);
  return true;
}
esp_err_t Init(const Config& config) {
  if (config.adapter != Adapter::kOnboard &&
      config.adapter != Adapter::kExternal) {
    return ESP_ERR_INVALID_ARG;
  }
  if (event_group != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  event_group = xEventGroupCreate();
  if (event_group == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  use_software_mac = config.use_software_mac;
  internal::EnableEcmPromiscuousMode(use_software_mac);

  ESP_RETURN_ON_ERROR(esp_event_handler_register(
      IOT_ETH_EVENT, ESP_EVENT_ANY_ID, EthernetEventHandler, nullptr), kTag,
      "Register Ethernet event handler failed");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(
      IP_EVENT, IP_EVENT_ETH_GOT_IP, EthernetEventHandler, nullptr), kTag,
      "Register IP event handler failed");

  const usbh_cdc_driver_config_t cdc_config = {
      .task_stack_size = 4096,
      .task_priority = configMAX_PRIORITIES - 1,
      .task_coreid = 0,
      .skip_init_usb_host_driver = true,
  };
  ESP_RETURN_ON_ERROR(internal::InstallCdc(&cdc_config, config.adapter), kTag,
      "Install Ethernet CDC driver failed");
  ESP_RETURN_ON_ERROR(
      InstallEcm(), kTag, "Install ECM network interface failed");
  internal::EnableDeviceEvents();
  ESP_LOGI(kTag, "Waiting for %s RTL8152B; MAC source=%s",
      config.adapter == Adapter::kExternal ? "Type-A" : "onboard",
      use_software_mac ? "ESP32-P4 eFuse" : "ECM");
  return ESP_OK;
}

void WaitForIp() {
  if (event_group != nullptr) {
    xEventGroupWaitBits(
        event_group, kGotIpBit, pdFALSE, pdFALSE, portMAX_DELAY);
  }
}

}  // namespace common::usb_ethernet
