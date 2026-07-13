/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-07-10 11:03:22
 * @LastEditTime: 2026-07-11 16:06:25
 * @License: GPL 3.0
 */
#include <stdlib.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <ctime>
#include <vector>

#include "common.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

namespace {

constexpr char kWifiSsid[] = "LilyGo-AABB";
constexpr char kWifiPassword[] = "xinyuandianzi";
constexpr char kNtpServer[] = "pool.ntp.org";
constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr uint16_t kMaxScanResults = 64;
constexpr uint32_t kTimeFetchIntervalMs = 20000;

EventGroupHandle_t g_wifi_events = nullptr;

const char* AuthModeName(wifi_auth_mode_t auth_mode) {
  switch (auth_mode) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2-PSK";
    case WIFI_AUTH_ENTERPRISE:
      return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3-PSK";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI-PSK";
    case WIFI_AUTH_OWE:
      return "OWE";
    case WIFI_AUTH_WPA3_ENT_192:
      return "WPA3-Enterprise-192";
    case WIFI_AUTH_WPA3_EXT_PSK:
      return "WPA3-EXT-PSK";
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
      return "WPA3-EXT-MIXED";
    case WIFI_AUTH_DPP:
      return "DPP";
    case WIFI_AUTH_WPA3_ENTERPRISE:
      return "WPA3-Enterprise";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
      return "WPA2/WPA3-Enterprise";
    case WIFI_AUTH_WPA_ENTERPRISE:
      return "WPA-Enterprise";
    default:
      return "UNKNOWN";
  }
}

const char* BandwidthName(wifi_bandwidth_t bandwidth) {
  switch (bandwidth) {
    case WIFI_BW20:
      return "20 MHz";
    case WIFI_BW40:
      return "40 MHz";
    case WIFI_BW80:
      return "80 MHz";
    case WIFI_BW160:
      return "160 MHz";
    case WIFI_BW80_BW80:
      return "80+80 MHz";
    default:
      return "Unknown";
  }
}

void WifiEventHandler(
    void*, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    printf("Connected to %s, IP: " IPSTR "\n", kWifiSsid,
        IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(g_wifi_events, kWifiConnectedBit);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(g_wifi_events, kWifiConnectedBit);
    printf("Wi-Fi disconnected, reconnecting to %s\n", kWifiSsid);
    esp_wifi_connect();
  }
}

void InitWifiStation() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  assert(esp_netif_create_default_wifi_sta() != nullptr);

  g_wifi_events = xEventGroupCreate();
  assert(g_wifi_events != nullptr);

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, WifiEventHandler, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

void ScanWifi() {
  printf("Scanning Wi-Fi...\n");
  ESP_ERROR_CHECK(esp_wifi_scan_start(nullptr, true));

  uint16_t access_point_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&access_point_count));
  access_point_count = std::min<uint16_t>(access_point_count, kMaxScanResults);

  std::vector<wifi_ap_record_t> records(access_point_count);
  if (access_point_count > 0) {
    ESP_ERROR_CHECK(
        esp_wifi_scan_get_ap_records(&access_point_count, records.data()));
  }

  std::sort(records.begin(), records.end(),
      [](const wifi_ap_record_t& left, const wifi_ap_record_t& right) {
        return left.rssi > right.rssi;
      });

  printf("Found %u access points:\n",
      static_cast<unsigned int>(access_point_count));
  printf(
      "------------------------------------------------------------------------"
      "--------------------------------------------------\n");
  printf("%-32s | %5s | %-7s | %2s | %-9s | %-9s | %-21s | %-17s\n", "SSID",
      "RSSI", "Band", "CH", "Bandwidth", "Encrypted", "Auth mode", "BSSID");
  printf(
      "------------------------------------------------------------------------"
      "--------------------------------------------------\n");
  for (const auto& record : records) {
    const char* band = record.primary <= 14 ? "2.4 GHz" : "5 GHz";
    const bool encrypted = record.authmode != WIFI_AUTH_OPEN;
    printf(
        "%-32s | %4d  | %-7s | %2u | %-9s | %-9s | %-21s | "
        "%02x:%02x:%02x:%02x:%02x:%02x\n",
        reinterpret_cast<const char*>(record.ssid), record.rssi, band,
        static_cast<unsigned int>(record.primary),
        BandwidthName(record.bandwidth), encrypted ? "Yes" : "No",
        AuthModeName(record.authmode), record.bssid[0], record.bssid[1],
        record.bssid[2], record.bssid[3], record.bssid[4], record.bssid[5]);
  }
  printf(
      "------------------------------------------------------------------------"
      "--------------------------------------------------\n");
  ESP_ERROR_CHECK(esp_wifi_clear_ap_list());
}

void ConnectWifi() {
  wifi_config_t wifi_config = {};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), kWifiSsid,
      sizeof(wifi_config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), kWifiPassword,
      sizeof(wifi_config.sta.password) - 1);

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_connect());
  xEventGroupWaitBits(
      g_wifi_events, kWifiConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
}

bool FetchAndPrintRealTime() {
  const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(kNtpServer);
  esp_err_t result = esp_netif_sntp_init(&config);
  if (result != ESP_OK) {
    printf("SNTP init failed: %s\n", esp_err_to_name(result));
    return false;
  }

  printf("Fetching real time from %s...\n", kNtpServer);
  result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
  if (result != ESP_OK) {
    printf("Network time fetch failed: %s\n", esp_err_to_name(result));
    esp_netif_sntp_deinit();
    return false;
  }

  std::time_t now = 0;
  std::tm local_time = {};
  char time_text[32] = {};

  std::time(&now);
  localtime_r(&now, &local_time);
  std::strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &local_time);
  printf("Network real time: %s UTC+8\n", time_text);
  esp_netif_sntp_deinit();
  return true;
}

}  // namespace

extern "C" void app_main(void) {
  printf("Wi-Fi scan, connect, and time example on %s\n", common::kBoardName);
  common::InitDriver();

  const esp_err_t result = esp_hosted_init();
  if (result != ESP_OK) {
    printf("esp_hosted_init failed: %s\n", esp_err_to_name(result));
    return;
  }

  InitWifiStation();
  ScanWifi();
  ConnectWifi();

  setenv("TZ", "CST-8", 1);
  tzset();
  TickType_t last_fetch_time = xTaskGetTickCount();
  while (true) {
    xEventGroupWaitBits(
        g_wifi_events, kWifiConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
    FetchAndPrintRealTime();
    vTaskDelayUntil(&last_fetch_time, pdMS_TO_TICKS(kTimeFetchIntervalMs));
  }
}
