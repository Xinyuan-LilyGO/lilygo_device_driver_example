/*
 * @Description: 通过无线网络获取、解码并播放 MP3 音频流
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include "common.h"
#include "wifi_mp3.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>

#include "esp_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

namespace {

constexpr char kWifiSsid[] = "LilyGo-AABB-5G";
constexpr char kWifiPassword[] = "xinyuandianzi";
constexpr char kMp3Url[] =
    "https://s9.imslp.org/files/imglnks/usimg/b/ba/"
    "IMSLP479619-PMLP02312-Nocturne,_Op.9_No.2_in_E-flat_major_Aya_"
    "Higuchi.mp3";
constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr size_t kReadBufferSize = 4 * 1024;
constexpr size_t kPcmBufferSize = 8 * 1024;

EventGroupHandle_t g_wifi_events = nullptr;

uint32_t GetSystemTimeMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t SynchsafeToUint32(const uint8_t* buffer) {
  return ((buffer[0] & 0x7F) << 21) | ((buffer[1] & 0x7F) << 14) |
         ((buffer[2] & 0x7F) << 7) | (buffer[3] & 0x7F);
}

bool SkipHttpBytes(esp_http_client_handle_t client, uint32_t byte_count) {
  auto buffer = std::make_unique<uint8_t[]>(kReadBufferSize);
  uint32_t remaining = byte_count;
  while (remaining > 0) {
    const size_t chunk = std::min<size_t>(kReadBufferSize, remaining);
    const int read_length = esp_http_client_read(
        client, reinterpret_cast<char*>(buffer.get()), chunk);
    if (read_length <= 0) {
      printf("ID3 skip failed, remaining: %lu bytes\n",
          static_cast<unsigned long>(remaining));
      return false;
    }
    remaining -= static_cast<uint32_t>(read_length);
  }
  return true;
}

size_t ReadPrefixAndSkipId3v2(
    esp_http_client_handle_t client, uint8_t* prefix_buffer) {
  uint8_t header[10] = {};
  const int read_length = esp_http_client_read(
      client, reinterpret_cast<char*>(header), sizeof(header));
  if (read_length <= 0) {
    return 0;
  }

  if (read_length != sizeof(header) ||
      std::memcmp(header, "ID3", 3) != 0) {
    std::memcpy(prefix_buffer, header, read_length);
    return static_cast<size_t>(read_length);
  }

  const uint32_t tag_size = SynchsafeToUint32(header + 6);
  printf("ID3v2.%u tag detected, size: %lu bytes\n",
      static_cast<unsigned int>(header[3]),
      static_cast<unsigned long>(tag_size));
  if (!SkipHttpBytes(client, tag_size)) {
    return 0;
  }

  return 0;
}

void CleanupStream(
    esp_http_client_handle_t client, esp_audio_dec_handle_t decoder) {
  if (decoder != nullptr) {
    esp_audio_dec_close(decoder);
  }
  esp_audio_dec_unregister_default();
  if (client != nullptr) {
    esp_http_client_cleanup(client);
  }
}

void HttpStreamPlayTask(void*) {
  printf("Starting MP3 stream playback: %s\n", kMp3Url);

  esp_http_client_config_t config = {
      .url = kMp3Url,
      .timeout_ms = 15000,
      .crt_bundle_attach = esp_crt_bundle_attach,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr || esp_http_client_open(client, 0) != ESP_OK) {
    printf("HTTP connection failed\n");
    if (client != nullptr) {
      esp_http_client_cleanup(client);
    }
    vTaskDelete(nullptr);
    return;
  }

  const int64_t content_length = esp_http_client_fetch_headers(client);
  const int status_code = esp_http_client_get_status_code(client);
  if (status_code < 200 || status_code >= 300) {
    printf("HTTP request failed, status: %d\n", status_code);
    esp_http_client_cleanup(client);
    vTaskDelete(nullptr);
    return;
  }
  printf("MP3 stream size: %lld bytes\n",
      static_cast<long long>(content_length));

  esp_audio_dec_register_default();
  esp_audio_dec_cfg_t decoder_config = {
      .type = ESP_AUDIO_TYPE_MP3,
  };
  esp_audio_dec_handle_t decoder = nullptr;
  if (esp_audio_dec_open(&decoder_config, &decoder) != ESP_AUDIO_ERR_OK) {
    printf("MP3 decoder initialization failed\n");
    CleanupStream(client, decoder);
    vTaskDelete(nullptr);
    return;
  }

  auto read_buffer = std::make_unique<uint8_t[]>(kReadBufferSize);
  auto pcm_buffer = std::make_unique<uint8_t[]>(kPcmBufferSize);
  size_t remaining_bytes =
      ReadPrefixAndSkipId3v2(client, read_buffer.get());
  int64_t total_downloaded = 0;
  const uint32_t start_time = GetSystemTimeMs();
  uint32_t last_print_time = start_time;
  bool playback_ok = true;

  while (playback_ok) {
    const int read_length = esp_http_client_read(client,
        reinterpret_cast<char*>(read_buffer.get() + remaining_bytes),
        kReadBufferSize - remaining_bytes);
    if (read_length < 0) {
      printf("HTTP stream read failed\n");
      break;
    }

    const size_t total_input =
        remaining_bytes + static_cast<size_t>(read_length);
    total_downloaded += read_length;
    if (total_input == 0) {
      break;
    }

    esp_audio_dec_in_raw_t input = {
        .buffer = read_buffer.get(),
        .len = total_input,
    };
    esp_audio_dec_out_frame_t output = {
        .buffer = pcm_buffer.get(),
        .len = kPcmBufferSize,
    };

    while (input.len > 0) {
      output.len = kPcmBufferSize;
      output.decoded_size = 0;
      const esp_audio_err_t result =
          esp_audio_dec_process(decoder, &input, &output);
      if (result != ESP_AUDIO_ERR_OK) {
        if (result == ESP_AUDIO_ERR_FAIL) {
          printf("MP3 decoding failed\n");
        }
        break;
      }

      if (output.decoded_size > 0 &&
          !WriteWifiMp3Pcm(output.buffer, output.decoded_size)) {
        printf("PCM audio output failed\n");
        playback_ok = false;
        break;
      }

      if (input.consumed == 0) {
        break;
      }
      input.buffer += input.consumed;
      input.len -= input.consumed;
    }

    remaining_bytes = input.len;
    if (remaining_bytes > 0) {
      std::memmove(read_buffer.get(), input.buffer, remaining_bytes);
    }

    const uint32_t now = GetSystemTimeMs();
    if (now - last_print_time >= 1000) {
      const float duration_seconds = (now - start_time) / 1000.0f;
      const float speed_kilobytes =
          (total_downloaded / 1024.0f) / duration_seconds;
      const float progress =
          content_length > 0
              ? total_downloaded * 100.0f / content_length
              : 0.0f;
      printf(
          "MP3 progress: %.2f%%, speed: %.2f KB/s, downloaded: %.1f KB\n",
          progress, speed_kilobytes, total_downloaded / 1024.0f);
      last_print_time = now;
    }

    if (read_length == 0 && remaining_bytes == total_input) {
      break;
    }
  }

  printf("MP3 stream playback finished\n");
  CleanupStream(client, decoder);
  vTaskDelete(nullptr);
}

void WifiEventHandler(
    void*, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
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

  wifi_config_t wifi_config = {};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), kWifiSsid,
      sizeof(wifi_config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), kWifiPassword,
      sizeof(wifi_config.sta.password) - 1);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

}  // namespace

extern "C" void app_main(void) {
  printf("Wi-Fi MP3 example on %s\n", common::kBoardName);
  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }

  const esp_err_t hosted_result = esp_hosted_init();
  if (hosted_result != ESP_OK) {
    printf("esp_hosted_init failed: %s\n", esp_err_to_name(hosted_result));
    return;
  }

  InitWifiStation();
  xEventGroupWaitBits(
      g_wifi_events, kWifiConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);

  if (!InitWifiMp3Audio()) {
    printf("Audio output initialization failed\n");
    return;
  }

  if (xTaskCreate(HttpStreamPlayTask, "wifi_mp3_task", 10 * 1024, nullptr, 5,
          nullptr) != pdPASS) {
    printf("MP3 playback task creation failed\n");
    return;
  }

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
