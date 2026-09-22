#include "ethernet.h"

#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "ethernet_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "iot_eth.h"
#include "iot_usbh_ecm.h"

namespace common::usb_ethernet {
namespace {

bool g_use_software_mac = false;

constexpr uint16_t kRtl8152bVid = 0x0BDA;
constexpr uint16_t kRtl8152bPid = 0x8152;
constexpr EventBits_t kGotIpBit = BIT0;
constexpr EventBits_t kNetifReadyBit = BIT1;

EventGroupHandle_t g_event_group = nullptr;
esp_netif_driver_base_t g_netif_driver = {};
iot_eth_handle_t g_eth_handle = nullptr;
bool g_started = false;
bool g_link_up = false;

/**
 * @brief 将 ECM 接收缓冲区交给网络栈，由网络栈释放
 */
esp_err_t ReceiveEthernetFrame(
    iot_eth_handle_t handle, uint8_t* buffer, size_t length, void* context) {
  (void)handle;
  return esp_netif_receive(
      static_cast<esp_netif_t*>(context), buffer, length, nullptr);
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
  config.handle = g_eth_handle;
  config.transmit = iot_eth_transmit;
  config.driver_free_rx_buffer = FreeEthernetFrame;
  {
    const esp_err_t error = esp_netif_set_driver_config(netif, &config);
    if (error != ESP_OK) {
      printf(
          "Set Ethernet network driver failed: %s\n", esp_err_to_name(error));
      return error;
    }
  }
  return iot_eth_update_input_path(g_eth_handle, ReceiveEthernetFrame, netif);
}

/**
 * @brief 验证网卡 MAC 后启动 DHCP，拒绝全零和组播地址
 */
void ConnectEthernetNetif() {
  if (!g_started || !g_link_up) {
    return;
  }
  uint8_t mac[6] = {};
  const char* mac_source = g_use_software_mac ? "ESP32-P4 eFuse" : "ECM";
  const esp_err_t result = g_use_software_mac
                               ? esp_efuse_mac_get_default(mac)
                               : iot_eth_get_addr(g_eth_handle, mac);
  uint8_t nonzero = 0;
  for (uint8_t byte : mac) {
    nonzero |= byte;
  }
  if (result != ESP_OK || nonzero == 0 || (mac[0] & 1) != 0) {
    printf(
        "Cannot start DHCP: %s MAC=%02X:%02X:%02X:%02X:%02X:%02X, "
        "read=%s\n",
        mac_source, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        esp_err_to_name(result));
    return;
  }
  if (g_use_software_mac) {
    // 只派生网络栈使用的本地管理单播地址，不烧写任何芯片的 eFuse。
    mac[0] = static_cast<uint8_t>((mac[0] | 0x02) & 0xFE);
  }
  const esp_err_t mac_result = esp_netif_set_mac(g_netif_driver.netif, mac);
  if (mac_result != ESP_OK) {
    printf(
        "Cannot start DHCP: set MAC failed: %s\n", esp_err_to_name(mac_result));
    return;
  }
  printf(
      "Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X (source=%s); "
      "starting DHCP\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac_source);
  esp_netif_action_connected(g_netif_driver.netif, IOT_ETH_EVENT,
      IOT_ETH_EVENT_CONNECTED, &g_eth_handle);
#if CONFIG_LWIP_IPV6
  const esp_err_t ipv6_result =
      esp_netif_create_ip6_linklocal(g_netif_driver.netif);
  if (ipv6_result != ESP_OK) {
    printf("Create IPv6 link-local address failed: %s\n",
        esp_err_to_name(ipv6_result));
  }
#endif
}

void EthernetEventHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data) {
  (void)arg;
  if (event_base == IOT_ETH_EVENT) {
    // ECM 可在安装期间上报链路；等待绑定完成，避免访问未初始化的 netif。
    xEventGroupWaitBits(
        g_event_group, kNetifReadyBit, pdFALSE, pdTRUE, portMAX_DELAY);
    if (event_data == nullptr ||
        *static_cast<iot_eth_handle_t*>(event_data) != g_eth_handle) {
      return;
    }
    switch (event_id) {
      case IOT_ETH_EVENT_START:
        printf("IOT_ETH_EVENT_START\n");
        esp_netif_action_start(
            g_netif_driver.netif, event_base, event_id, event_data);
        g_started = true;
        ConnectEthernetNetif();
        break;
      case IOT_ETH_EVENT_STOP:
        printf("IOT_ETH_EVENT_STOP\n");
        g_started = false;
        g_link_up = false;
        xEventGroupClearBits(g_event_group, kGotIpBit);
        esp_netif_action_stop(
            g_netif_driver.netif, event_base, event_id, event_data);
        break;
      case IOT_ETH_EVENT_CONNECTED:
        printf("RTL8152B link up\n");
        g_link_up = true;
        ConnectEthernetNetif();
        break;
      case IOT_ETH_EVENT_DISCONNECTED:
        printf("RTL8152B link down\n");
        g_link_up = false;
        xEventGroupClearBits(g_event_group, kGotIpBit);
        if (g_started) {
          esp_netif_action_disconnected(
              g_netif_driver.netif, event_base, event_id, event_data);
        }
        break;
      default:
        printf("IOT_ETH_EVENT id=%" PRId32 "\n", event_id);
        break;
    }
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP &&
      event_data != nullptr) {
    const ip_event_got_ip_t* event =
        static_cast<const ip_event_got_ip_t*>(event_data);
    if (event->esp_netif != g_netif_driver.netif) {
      return;
    }
    printf("RTL8152B got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    printf("Gateway: " IPSTR ", Netmask: " IPSTR "\n",
        IP2STR(&event->ip_info.gw), IP2STR(&event->ip_info.netmask));
    xEventGroupSetBits(g_event_group, kGotIpBit);
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
  {
    const esp_err_t error = iot_eth_install(&eth_cfg, &g_eth_handle);
    if (error != ESP_OK) {
      printf("install iot_eth failed: %s\n", esp_err_to_name(error));
      return error;
    }
  }

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
  if (!(ecm_netif != nullptr)) {
    printf("create USB ECM netif failed\n");
    return ESP_FAIL;
  }
  {
    const esp_err_t error = esp_netif_set_default_netif(ecm_netif);
    if (error != ESP_OK) {
      printf("Set default netif failed: %s\n", esp_err_to_name(error));
      return error;
    }
  }

  g_netif_driver.post_attach = AttachEthernetNetif;
  {
    const esp_err_t error = esp_netif_attach(ecm_netif, &g_netif_driver);
    if (error != ESP_OK) {
      printf("attach netif failed: %s\n", esp_err_to_name(error));
      return error;
    }
  }
  xEventGroupSetBits(g_event_group, kNetifReadyBit);

  return iot_eth_start(g_eth_handle);
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
      printf("RTL8152B does not advertise ECM configuration 2\n");
      return false;
    }
    *configuration_value = 2;
    printf("USB device VID:%04X PID:%04X use config %u\n", dev_desc->idVendor,
        dev_desc->idProduct, *configuration_value);
    return true;
  }

  *configuration_value = 1;
  printf("USB device VID:%04X PID:%04X use config %u\n", dev_desc->idVendor,
      dev_desc->idProduct, *configuration_value);
  return true;
}
esp_err_t Init(const Config& config) {
  if (config.adapter != Adapter::kOnboard &&
      config.adapter != Adapter::kExternal) {
    return ESP_ERR_INVALID_ARG;
  }
  if (g_event_group != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  g_event_group = xEventGroupCreate();
  if (g_event_group == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  g_use_software_mac = config.use_software_mac;
  internal::EnableEcmPromiscuousMode(g_use_software_mac);

  {
    const esp_err_t error = esp_event_handler_register(
        IOT_ETH_EVENT, ESP_EVENT_ANY_ID, EthernetEventHandler, nullptr);
    if (error != ESP_OK) {
      printf("Register Ethernet event handler failed: %s\n",
          esp_err_to_name(error));
      return error;
    }
  }
  {
    const esp_err_t error = esp_event_handler_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP, EthernetEventHandler, nullptr);
    if (error != ESP_OK) {
      printf("Register IP event handler failed: %s\n", esp_err_to_name(error));
      return error;
    }
  }

  const usbh_cdc_driver_config_t cdc_config = {
      .task_stack_size = 4096,
      .task_priority = configMAX_PRIORITIES - 1,
      .task_coreid = 0,
      .skip_init_usb_host_driver = true,
  };
  {
    const esp_err_t error = internal::InstallCdc(&cdc_config, config.adapter);
    if (error != ESP_OK) {
      printf(
          "Install Ethernet CDC driver failed: %s\n", esp_err_to_name(error));
      return error;
    }
  }
  {
    const esp_err_t error = InstallEcm();
    if (error != ESP_OK) {
      printf(
          "Install ECM network interface failed: %s\n", esp_err_to_name(error));
      return error;
    }
  }
  internal::EnableDeviceEvents();
  printf("Waiting for %s RTL8152B; MAC source=%s\n",
      config.adapter == Adapter::kExternal ? "Type-A" : "onboard",
      g_use_software_mac ? "ESP32-P4 eFuse" : "ECM");
  return ESP_OK;
}

void WaitForIp() {
  if (g_event_group != nullptr) {
    xEventGroupWaitBits(
        g_event_group, kGotIpBit, pdFALSE, pdFALSE, portMAX_DELAY);
  }
}

}  // namespace common::usb_ethernet
