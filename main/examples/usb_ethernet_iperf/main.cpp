/*
 * @Description: 在 T-Display-P4 V2.0 上使用 USB RTL8152B 网卡进行 iperf 测试
 * @Author: LILYGO_L
 * @Date: 2026-05-05 18:15:09
 * @LastEditTime: 2026-09-11 17:24:33
 * @License: GPL 3.0
 */
#include <stdint.h>
#include <stdio.h>

// 0: 板载 RTL8152B；1: Type-A 接口外接 RTL8152B。
#define USB_ETHERNET_IPERF_USE_EXTERNAL_ADAPTER 0
// 1: 从 ESP32-P4 eFuse MAC 派生地址，并启用混杂接收；0: 使用网卡的 ECM MAC。
#define USB_ETHERNET_IPERF_USE_SOFTWARE_MAC 1

#include "esp_check.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iperf_cmd.h"
#include "usb/ethernet.h"
#include "usb/host.h"
#include "usb/usb_host.h"

static const char* TAG = "rtl8152b_iperf";

static void usb_lib_task(void* arg) {
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .root_port_unpowered = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
      .enum_filter_cb = common::usb_ethernet::SelectUsbConfiguration,
      .fifo_settings_custom = {},
      .peripheral_map = 0,
  };
  ESP_ERROR_CHECK(usb_host_install(&host_config));
  xTaskNotifyGive(static_cast<TaskHandle_t>(arg));

  bool has_clients = true;
  bool has_devices = false;
  while (has_clients) {
    uint32_t event_flags = 0;
    ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));

    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_LOGI(TAG, "USB host has no clients");
      if (usb_host_device_free_all() == ESP_OK) {
        ESP_LOGI(TAG, "All USB devices are free");
        has_clients = false;
      } else {
        has_devices = true;
      }
    }

    if (has_devices && (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) {
      ESP_LOGI(TAG, "All USB devices are free");
      has_clients = false;
    }
  }

  ESP_LOGI(TAG, "Uninstall USB Host library");
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_ERROR_CHECK(usb_host_uninstall());
  common::usb_host::DisablePower();
  vTaskDelete(nullptr);
}

/**
 * @brief 根据主控制台配置，通过 USB Serial/JTAG 或 UART 接收 iperf 命令。
 */
static void start_iperf_console(void) {
  esp_console_repl_t* repl = nullptr;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

  repl_config.prompt = "rtl8152b>";
  repl_config.max_history_len = 8;
  repl_config.task_priority = 24;

  // ESP-IDF 仅在 USB Serial/JTAG 为主控制台时提供 USB REPL。
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  esp_console_dev_usb_serial_jtag_config_t usb_config =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(
      esp_console_new_repl_usb_serial_jtag(&usb_config, &repl_config, &repl));
  ESP_LOGI(TAG, "iperf console: USB Serial/JTAG");
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
  esp_console_dev_uart_config_t uart_config =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
  ESP_LOGI(TAG, "iperf console: UART");
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
  ESP_LOGW(TAG, "USB is log-only; select USB Serial/JTAG as the primary "
               "console in menuconfig to enter commands over USB");
#endif
#else
#error "iperf requires a USB Serial/JTAG or UART primary console"
#endif
  ESP_ERROR_CHECK(app_register_iperf_commands());

  printf("\n ========================================================\n");
  printf(" |              RTL8152B USB Ethernet iperf             |\n");
  printf(" |                                                      |\n");
  printf(" | Type 'help' to display a list of available commands. |\n");
  printf(" | Example: iperf -s -i 1 -t 30                         |\n");
  printf(" | Example: iperf -c <server_ip> -i 1 -t 30             |\n");
  printf(" |                                                      |\n");
  printf(" ========================================================\n\n");

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

extern "C" void app_main(void) {
#if USB_ETHERNET_IPERF_USE_EXTERNAL_ADAPTER
  printf("External Type-A Ethernet iperf test; onboard Ethernet skipped\n");
#else
  printf("Onboard Ethernet iperf test; external Type-A Ethernet skipped\n");
#endif

  if (!common::usb_host::InitPower()) {
    ESP_LOGE(TAG, "USB host power initialization failed");
    return;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_lib",
      4096, xTaskGetCurrentTaskHandle(), 5, nullptr, 0);
  ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_FAIL);

  uint32_t notify_value = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000));
  ESP_ERROR_CHECK(notify_value > 0 ? ESP_OK : ESP_ERR_TIMEOUT);

  const common::usb_ethernet::Config ethernet_config = {
      .adapter = USB_ETHERNET_IPERF_USE_EXTERNAL_ADAPTER
          ? common::usb_ethernet::Adapter::kExternal
          : common::usb_ethernet::Adapter::kOnboard,
      .use_software_mac = USB_ETHERNET_IPERF_USE_SOFTWARE_MAC != 0,
  };
  ESP_ERROR_CHECK(common::usb_ethernet::Init(ethernet_config));

#if USB_ETHERNET_IPERF_USE_EXTERNAL_ADAPTER
  ESP_LOGI(TAG, "Connect the external RTL8152B to Type-A and its Ethernet "
               "cable to a DHCP router; keep connected while waiting for IP");
#else
  ESP_LOGI(TAG, "Connect the onboard RTL8152B Ethernet cable to a DHCP router "
               "and keep it connected while waiting for IP");
#endif
  start_iperf_console();

  common::usb_ethernet::WaitForIp();
  vTaskSuspend(nullptr);
}
