#include "ethernet_transport.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

extern "C" esp_err_t __real_usb_host_client_register(
    const usb_host_client_config_t* config,
    usb_host_client_handle_t* client_ret);
extern "C" esp_err_t __real_usb_host_transfer_submit(usb_transfer_t* transfer);

namespace {

constexpr uint16_t kBoardHubVid = 0x05E3;
constexpr uint16_t kBoardHubPid = 0x0610;
// T-Display-P4 V2.0 原理图中，GL852G 端口 2 接板载网卡，端口 4 接 Type-A。
constexpr uint8_t kTypeAPort = 4;
constexpr uint8_t kOnboardPort = 2;
std::atomic<uint8_t> g_selected_port{kOnboardPort};

std::atomic<TaskHandle_t> g_installing_task{nullptr};
std::atomic<usb_device_handle_t> g_selected_device{nullptr};
std::atomic<bool> g_device_events_enabled{false};
usb_host_client_handle_t g_cdc_client = nullptr;
usb_host_client_event_cb_t g_cdc_callback = nullptr;
void* g_cdc_callback_arg = nullptr;

/**
 * @brief 沿父设备查找板载 Hub 分支，支持 Type-A 外接多级 Hub
 * @param device 待检查的设备句柄
 * @param port 成功时写入板载 Hub 的下游端口号
 * @return 找到板载 Hub 返回 ESP_OK，否则返回错误码
 */
esp_err_t GetBoardHubPort(usb_device_handle_t device, uint8_t* port) {
  for (int depth = 0; depth < 7; ++depth) {
    usb_device_info_t info = {};
    {
      const esp_err_t error = usb_host_device_info(device, &info);
      if (error != ESP_OK) {
        printf("Read USB device topology failed: %s\n", esp_err_to_name(error));
        return error;
      }
    }
    if (info.parent.dev_hdl == nullptr) {
      return ESP_ERR_NOT_FOUND;
    }

    usb_device_info_t parent_info = {};
    {
      const esp_err_t error =
          usb_host_device_info(info.parent.dev_hdl, &parent_info);
      if (error != ESP_OK) {
        printf("Read parent USB device topology failed: %s\n",
            esp_err_to_name(error));
        return error;
      }
    }
    if (parent_info.parent.dev_hdl == nullptr) {
      const usb_device_desc_t* descriptor = nullptr;
      {
        const esp_err_t error =
            usb_host_get_device_descriptor(info.parent.dev_hdl, &descriptor);
        if (error != ESP_OK) {
          printf(
              "Read root hub descriptor failed: %s\n", esp_err_to_name(error));
          return error;
        }
      }
      if (descriptor->bDeviceClass != USB_CLASS_HUB ||
          descriptor->idVendor != kBoardHubVid ||
          descriptor->idProduct != kBoardHubPid) {
        return ESP_ERR_NOT_SUPPORTED;
      }
      *port = info.parent.port_num;
      return ESP_OK;
    }
    device = info.parent.dev_hdl;
  }
  return ESP_ERR_NOT_SUPPORTED;
}

/**
 * @brief 筛选指定分支的 RTL8152B 后转发 CDC 设备通知
 * @param event USB 主机设备事件
 * @param arg 未使用
 */
void SelectedDeviceEvent(const usb_host_client_event_msg_t* event, void* arg) {
  (void)arg;
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    // CDC 安装后即开始收取事件，先等待 ECM 注册监听并完成 netif 绑定。
    while (!g_device_events_enabled.load()) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    usb_device_handle_t device = nullptr;
    if (usb_host_device_open(g_cdc_client, event->new_dev.address, &device) !=
        ESP_OK) {
      printf(
          "Open USB device %u for filtering failed\n", event->new_dev.address);
      return;
    }

    const usb_device_desc_t* descriptor = nullptr;
    esp_err_t result = usb_host_get_device_descriptor(device, &descriptor);
    uint8_t port = 0;
    bool selected = false;
    bool is_rtl8152 = false;
    if (result == ESP_OK && descriptor->bDeviceClass != USB_CLASS_HUB) {
      is_rtl8152 =
          descriptor->idVendor == 0x0BDA && descriptor->idProduct == 0x8152;
      result = GetBoardHubPort(device, &port);
      selected =
          is_rtl8152 && result == ESP_OK && port == g_selected_port.load();
      printf(
          "%s USB device: address=%u, VID:%04X PID:%04X, "
          "board hub port=%u, topology=%s\n",
          selected ? "Select Ethernet" : "Skip device", event->new_dev.address,
          descriptor->idVendor, descriptor->idProduct, port,
          esp_err_to_name(result));
    } else if (result != ESP_OK) {
      printf(
          "Read USB device descriptor failed: %s\n", esp_err_to_name(result));
    }

    if (usb_host_device_close(g_cdc_client, device) != ESP_OK) {
      printf("Close USB device after filtering failed\n");
      return;
    }
    if (!selected) {
      return;
    }
    if (is_rtl8152) {
      g_selected_device.store(device);
    }
  } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    usb_device_handle_t device = event->dev_gone.dev_hdl;
    g_selected_device.compare_exchange_strong(device, nullptr);
  }
  g_cdc_callback(event, g_cdc_callback_arg);
}

}  // namespace

// ECM 没有端口筛选接口；链接包装仅作用于本模块安装的 CDC 客户端。
extern "C" esp_err_t __wrap_usb_host_client_register(
    const usb_host_client_config_t* config,
    usb_host_client_handle_t* client_ret) {
  if (g_installing_task.load() != xTaskGetCurrentTaskHandle()) {
    return __real_usb_host_client_register(config, client_ret);
  }
  if (config == nullptr || client_ret == nullptr || config->is_synchronous ||
      config->async.client_event_callback == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  usb_host_client_config_t filtered_config = *config;
  g_cdc_callback = config->async.client_event_callback;
  g_cdc_callback_arg = config->async.callback_arg;
  filtered_config.async.client_event_callback = SelectedDeviceEvent;
  filtered_config.async.callback_arg = nullptr;
  const esp_err_t result =
      __real_usb_host_client_register(&filtered_config, client_ret);
  if (result == ESP_OK) {
    g_cdc_client = *client_ret;
  }
  return result;
}

// RTL8152 ECM 的非零 OUT 端点为 bulk 数据端点；短包用于标记以太网帧结束。
extern "C" esp_err_t __wrap_usb_host_transfer_submit(usb_transfer_t* transfer) {
  const usb_device_handle_t device = g_selected_device.load();
  const bool is_ethernet = transfer != nullptr && device != nullptr &&
                           transfer->device_handle == device;
  if (is_ethernet && transfer->bEndpointAddress != 0 &&
      (transfer->bEndpointAddress & 0x80) == 0) {
    transfer->flags |= USB_TRANSFER_FLAG_ZERO_PACK;
  }
  const esp_err_t result = __real_usb_host_transfer_submit(transfer);
  if (is_ethernet && result != ESP_OK) {
    printf("Ethernet USB submit failed: endpoint=0x%02X, bytes=%d, %s\n",
        transfer->bEndpointAddress, transfer->num_bytes,
        esp_err_to_name(result));
  }
  return result;
}

namespace common::usb_ethernet::internal {

esp_err_t InstallCdc(const usbh_cdc_driver_config_t* config, Adapter adapter) {
  g_selected_port.store(
      adapter == Adapter::kExternal ? kTypeAPort : kOnboardPort);
  g_installing_task.store(xTaskGetCurrentTaskHandle());
  const esp_err_t result = usbh_cdc_driver_install(config);
  g_installing_task.store(nullptr);
  return result;
}

void EnableDeviceEvents() { g_device_events_enabled.store(true); }

}  // namespace common::usb_ethernet::internal

namespace {

constexpr uint8_t kClassInterfaceOut = 0x21;
constexpr uint8_t kSetEthernetPacketFilter = 0x43;
constexpr uint16_t kDefaultPacketFilter = 0x000E;
constexpr uint16_t kPromiscuousPacketFilter = 0x0001;
std::atomic<bool> g_promiscuous_mode_enabled{false};

}  // namespace

namespace common::usb_ethernet::internal {

void EnableEcmPromiscuousMode(bool enabled) {
  g_promiscuous_mode_enabled.store(enabled);
}

}  // namespace common::usb_ethernet::internal

extern "C" esp_err_t __real_usbh_cdc_send_custom_request(
    usbh_cdc_port_handle_t cdc_port_handle, uint8_t bm_request_type,
    uint8_t request, uint16_t value, uint16_t index, uint16_t length,
    uint8_t* data);

// 在官方组件原有的请求时序中调整过滤值，避免链路回调与初始化相互覆盖。
// 只改变接收过滤，不写入 RTL8152B 的 MAC、EEPROM 或 OTP。
extern "C" esp_err_t __wrap_usbh_cdc_send_custom_request(
    usbh_cdc_port_handle_t cdc_port_handle, uint8_t bm_request_type,
    uint8_t request, uint16_t value, uint16_t index, uint16_t length,
    uint8_t* data) {
  const bool override_filter =
      g_promiscuous_mode_enabled.load() && cdc_port_handle != nullptr &&
      bm_request_type == kClassInterfaceOut &&
      request == kSetEthernetPacketFilter && value == kDefaultPacketFilter &&
      length == 0 && data == nullptr;
  if (override_filter) {
    value |= kPromiscuousPacketFilter;
  }
  const esp_err_t result = __real_usbh_cdc_send_custom_request(
      cdc_port_handle, bm_request_type, request, value, index, length, data);
  if (override_filter) {
    if (result == ESP_OK) {
      printf("ECM packet filter=0x%04X, interface=%u (software MAC)\n",
          static_cast<unsigned>(value), static_cast<unsigned>(index));
    } else {
      printf("Set ECM packet filter=0x%04X, interface=%u failed: %s\n",
          static_cast<unsigned>(value), static_cast<unsigned>(index),
          esp_err_to_name(result));
    }
  }
  return result;
}
