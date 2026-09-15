/*
 * @Description: 在支持的设备上测试 Wi-Fi 发射功率
 * @Author: LILYGO_L
 * @Date: 2026-09-15
 * @LastEditTime: 2026-09-15
 * @License: GPL 3.0
 */
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "common.h"

namespace {

bool CheckResult(esp_err_t result, const char* operation) {
  if (result == ESP_OK) {
    return true;
  }
  printf("Wi-Fi %s failed: %s\n", operation, esp_err_to_name(result));
  return false;
}

bool InitWifiAp() {
  if (!CheckResult(esp_netif_init(), "network interface initialization")) {
    return false;
  }
  if (!CheckResult(esp_event_loop_create_default(), "event loop creation")) {
    return false;
  }
  if (esp_netif_create_default_wifi_ap() == nullptr) {
    printf("Wi-Fi AP interface creation failed\n");
    return false;
  }

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  init_config.nvs_enable = false;
  if (!CheckResult(esp_wifi_init(&init_config), "initialization") ||
      !CheckResult(esp_wifi_set_storage(WIFI_STORAGE_RAM), "storage setup") ||
      !CheckResult(esp_wifi_set_mode(WIFI_MODE_AP), "mode setup")) {
    return false;
  }

  wifi_config_t ap_config = {};
  constexpr char kSsid[] = "WiFi-Tx-Test";
  ap_config.ap.ssid_len = sizeof(kSsid) - 1;
  std::memcpy(ap_config.ap.ssid, kSsid, ap_config.ap.ssid_len);
  ap_config.ap.channel = 6;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;
  ap_config.ap.max_connection = 1;
  ap_config.ap.beacon_interval = 100;
  if (!CheckResult(esp_wifi_set_config(WIFI_IF_AP, &ap_config),
          "AP configuration")) {
    return false;
  }

  wifi_country_t country = {};
  std::memcpy(country.cc, "CN", sizeof(country.cc));
  country.schan = 1;
  country.nchan = 13;
  country.max_tx_power = 80;
  country.policy = WIFI_COUNTRY_POLICY_AUTO;
  return CheckResult(esp_wifi_set_country(&country), "country setup") &&
      CheckResult(esp_wifi_start(), "start");
}

bool ConfigureBand(uint8_t cycle) {
  if (cycle == 0) {
    printf("Wi-Fi transmit test: 2.4 GHz, channel 6, 802.11b\n");
    return CheckResult(esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY),
               "2.4 GHz band") &&
        CheckResult(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B),
            "802.11b protocol") &&
        CheckResult(esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE),
            "channel 6");
  }

#if CONFIG_SLAVE_IDF_TARGET_ESP32C5
  printf("Wi-Fi transmit test: 5 GHz, channel 36, 802.11a\n");
  return CheckResult(esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY),
             "5 GHz band") &&
      CheckResult(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11A),
          "802.11a protocol") &&
      CheckResult(esp_wifi_set_channel(36, WIFI_SECOND_CHAN_NONE),
          "channel 36");
#else
  return false;
#endif
}

}  // namespace

extern "C" void app_main(void) {
  printf("Wi-Fi transmit power test on %s\n", common::kBoardName);
  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors; continuing example\n");
  }
  if (!common::SetWifiCoprocessorPowerEnabled(true) ||
      !common::RegisterWifiCoprocessorResetCallback()) {
    printf("Wi-Fi coprocessor initialization failed\n");
    return;
  }
  if (static_cast<esp_err_t>(esp_hosted_init()) != ESP_OK) {
    printf("esp_hosted_init failed\n");
    return;
  }
  if (!InitWifiAp()) {
    return;
  }

  uint8_t cycle = 0;
  while (true) {
    if (!CheckResult(esp_wifi_stop(), "stop") ||
        !CheckResult(esp_wifi_start(), "restart") ||
        !ConfigureBand(cycle) ||
        !CheckResult(esp_wifi_set_max_tx_power(80), "maximum transmit power")) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    int8_t applied_tx_power = 0;
    if (CheckResult(esp_wifi_get_max_tx_power(&applied_tx_power),
            "read maximum transmit power")) {
      printf("Wi-Fi driver maximum transmit power: %.2f dBm\n",
          applied_tx_power / 4.0f);
    }
    printf("Wi-Fi beacon transmitting at maximum configured power for 5 s\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
#if CONFIG_SLAVE_IDF_TARGET_ESP32C5
    cycle ^= 1;
#endif
  }
}
