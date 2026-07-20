/*
 * @Description: 网络适配器版本检查与更新
 * @Author: LILYGO_L
 * @Date: 2026-07-17 00:00:00
 * @LastEditTime: 2026-07-20 00:00:00
 * @License: GPL 3.0
 */
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_event.h"
#include "esp_hosted_ota.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lilygo_device_driver_library.h"

namespace {

// 用于存放网络适配器待更新固件文件的 LittleFS 分区和挂载路径
constexpr char kFirmwarePartitionLabel[] = "storage";
constexpr char kFirmwareStorageBasePath[] = "/storage";
constexpr char kFirmwareFilePath[] =
    "/storage/network_adapter_firmware.bin";
// 网络适配器固件中预期的 ESP-IDF 应用程序名称
constexpr char kFirmwareProjectName[] = "network_adapter";
#if CONFIG_SLAVE_IDF_TARGET_ESP32C5
constexpr esp_chip_id_t kExpectedCoprocessorChipId = ESP_CHIP_ID_ESP32C5;
constexpr char kCoprocessorName[] = "ESP32-C5";
#elif CONFIG_SLAVE_IDF_TARGET_ESP32C6
constexpr esp_chip_id_t kExpectedCoprocessorChipId = ESP_CHIP_ID_ESP32C6;
constexpr char kCoprocessorName[] = "ESP32-C6";
#else
#error "Unsupported Wi-Fi Remote slave target"
#endif
// ESP-IDF 应用镜像和合并固件中的镜像入口均按 4 KiB 对齐
constexpr size_t kImageSearchAlignment = 0x1000;
// 解析未验证镜像时允许的最大段数量
constexpr uint8_t kMaxImageSegmentCount = 16;
// ESP-Hosted 组件示例推荐的 OTA 单次传输大小
constexpr size_t kOtaChunkSize = 1500;
// BOOT 按键轮询和消抖时间
constexpr uint32_t kButtonPollIntervalMs = 20;
constexpr uint32_t kButtonDebounceMs = 40;
// 板级驱动开启网络适配器电源后的启动等待时间
constexpr uint32_t kCoprocessorBootDelayMs = 500;
// 等待 ESP-Hosted SDIO 传输链路建立的最长时间
constexpr uint32_t kHostedTransportTimeoutMs = 10000;
// 网络适配器固件更新后重启 P4 前的等待时间
constexpr uint32_t kHostRestartDelayMs = 2000;
constexpr EventBits_t kHostedTransportUpBit = BIT0;

EventGroupHandle_t hosted_event_group = nullptr;

struct FirmwareImageInfo {
  size_t offset = 0;
  size_t size = 0;
  esp_app_desc_t app_desc = {};
};

/**
 * @brief 以只读方式挂载包含网络适配器固件的 LittleFS 分区
 * @return 挂载成功时返回 true
 */
bool MountFirmwareStorage() {
  esp_vfs_littlefs_conf_t config = {};
  config.base_path = kFirmwareStorageBasePath;
  config.partition_label = kFirmwarePartitionLabel;
  config.format_if_mount_failed = false;
  config.read_only = true;
  config.dont_mount = false;
  config.grow_on_mount = false;

  const esp_err_t result = esp_vfs_littlefs_register(&config);
  if (result != ESP_OK) {
    printf("Mount LittleFS partition '%s' at '%s' failed: %s\n",
        kFirmwarePartitionLabel, kFirmwareStorageBasePath,
        esp_err_to_name(result));
    return false;
  }

  size_t total_bytes = 0;
  size_t used_bytes = 0;
  if (esp_littlefs_info(
          kFirmwarePartitionLabel, &total_bytes, &used_bytes) == ESP_OK) {
    printf("LittleFS storage: total=%u used=%u bytes\n",
        static_cast<unsigned int>(total_bytes),
        static_cast<unsigned int>(used_bytes));
  }
  return true;
}

/**
 * @brief 处理 ESP-Hosted 传输状态事件
 */
void HostedEventHandler(void*, esp_event_base_t event_base,
    int32_t event_id, void*) {
  if (event_base != ESP_HOSTED_EVENT || hosted_event_group == nullptr) {
    return;
  }

  if (event_id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
    xEventGroupSetBits(hosted_event_group, kHostedTransportUpBit);
    printf("ESP-Hosted transport is ready\n");
  } else if (event_id == ESP_HOSTED_EVENT_TRANSPORT_DOWN) {
    xEventGroupClearBits(hosted_event_group, kHostedTransportUpBit);
    printf("ESP-Hosted transport is down\n");
  } else if (event_id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
    xEventGroupClearBits(hosted_event_group, kHostedTransportUpBit);
    printf("ESP-Hosted transport failure\n");
  }
}

/**
 * @brief 初始化 ESP-Hosted 并等待协处理器传输链路建立
 * @return 传输链路在超时前建立时返回 true
 */
bool InitEspHostedTransport() {
  esp_err_t result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    printf("Create default event loop failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  hosted_event_group = xEventGroupCreate();
  if (hosted_event_group == nullptr) {
    printf("Create ESP-Hosted event group failed\n");
    return false;
  }

  result = esp_event_handler_register(ESP_HOSTED_EVENT,
      ESP_EVENT_ANY_ID, HostedEventHandler, nullptr);
  if (result != ESP_OK) {
    printf("Register ESP-Hosted event handler failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  result = static_cast<esp_err_t>(esp_hosted_init());
  if (result != ESP_OK) {
    printf("esp_hosted_init failed: %s\n", esp_err_to_name(result));
    return false;
  }

  result = static_cast<esp_err_t>(esp_hosted_connect_to_slave());
  if (result != ESP_OK) {
    printf("esp_hosted_connect_to_slave failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  printf("Waiting for ESP-Hosted transport...\n");
  const EventBits_t bits = xEventGroupWaitBits(hosted_event_group,
      kHostedTransportUpBit, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(kHostedTransportTimeoutMs));
  if ((bits & kHostedTransportUpBit) == 0) {
    printf("ESP-Hosted transport timeout after %" PRIu32 " ms\n",
        kHostedTransportTimeoutMs);
    return false;
  }
  return true;
}

/**
 * @brief 检查读取范围后从固件文件读取数据
 * @param file 已打开的固件文件
 * @param file_size 固件文件总长度
 * @param offset 文件内的字节偏移
 * @param destination 目标缓冲区
 * @param size 读取的字节数
 * @return 读取成功时返回 true
 */
bool ReadFirmwareFile(FILE* file, size_t file_size, size_t offset,
    void* destination, size_t size) {
  if (file == nullptr || destination == nullptr || offset > file_size ||
      size > file_size - offset) {
    return false;
  }
  if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
    return false;
  }
  return std::fread(destination, 1, size, file) == size;
}

/**
 * @brief 获取固件文件长度并将读写位置恢复到文件开头
 * @param file 已打开的固件文件
 * @param file_size 用于返回固件文件长度
 * @return 获取成功且文件非空时返回 true
 */
bool GetFirmwareFileSize(FILE* file, size_t* file_size) {
  if (file == nullptr || file_size == nullptr ||
      std::fseek(file, 0, SEEK_END) != 0) {
    return false;
  }

  const long end_offset = std::ftell(file);
  if (end_offset <= 0) {
    return false;
  }
  std::rewind(file);
  *file_size = static_cast<size_t>(end_offset);
  return true;
}

/**
 * @brief 计算 ESP-IDF 应用镜像的完整长度
 * @param file 存放镜像的固件文件
 * @param file_size 固件文件总长度
 * @param image_offset 应用镜像的起始偏移
 * @param image_header 已解析的应用镜像头
 * @param image_size 用于返回完整镜像长度
 * @return 所有镜像段及尾部数据均位于文件范围内时返回 true
 */
bool CalculateImageSize(FILE* file, size_t file_size, size_t image_offset,
    const esp_image_header_t& image_header, size_t* image_size) {
  if (image_size == nullptr || image_header.segment_count == 0 ||
      image_header.segment_count > kMaxImageSegmentCount) {
    return false;
  }

  size_t cursor = image_offset + sizeof(esp_image_header_t);
  size_t total_size = sizeof(esp_image_header_t);
  for (uint8_t index = 0; index < image_header.segment_count; ++index) {
    esp_image_segment_header_t segment_header = {};
    if (!ReadFirmwareFile(file, file_size, cursor, &segment_header,
            sizeof(segment_header))) {
      return false;
    }

    cursor += sizeof(segment_header);
    total_size += sizeof(segment_header);
    if (cursor > file_size || segment_header.data_len > file_size - cursor) {
      return false;
    }
    cursor += segment_header.data_len;
    total_size += segment_header.data_len;
  }

  // 校验和位于下一个 16 字节对齐块的最后一个字节
  total_size += 16 - total_size % 16;
  if (image_header.hash_appended == 1) {
    total_size += 32;
  }

  if (image_offset > file_size || total_size > file_size - image_offset) {
    return false;
  }
  *image_size = total_size;
  return true;
}

/**
 * @brief 在独立应用镜像或合并固件中查找 network_adapter 应用镜像
 * @param file 存放待更新固件的文件
 * @param file_size 固件文件总长度
 * @param image_info 用于返回镜像位置、长度和应用信息
 * @return 找到目标芯片有效的 network_adapter 镜像时返回 true
 */
bool FindNetworkAdapterImage(
    FILE* file, size_t file_size, FirmwareImageInfo* image_info) {
  if (file == nullptr || image_info == nullptr) {
    return false;
  }

  for (size_t offset = 0;
       offset + sizeof(esp_image_header_t) +
               sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t) <=
           file_size;
       offset += kImageSearchAlignment) {
    esp_image_header_t image_header = {};
    if (!ReadFirmwareFile(
            file, file_size, offset, &image_header, sizeof(image_header))) {
      continue;
    }

    if (image_header.magic != ESP_IMAGE_HEADER_MAGIC) {
      continue;
    }
    if (image_header.chip_id != kExpectedCoprocessorChipId) {
      continue;
    }
    if (image_header.segment_count == 0 ||
        image_header.segment_count > kMaxImageSegmentCount) {
      continue;
    }

    esp_app_desc_t app_desc = {};
    const size_t app_desc_offset = offset + sizeof(esp_image_header_t) +
                                   sizeof(esp_image_segment_header_t);
    if (!ReadFirmwareFile(
            file, file_size, app_desc_offset, &app_desc, sizeof(app_desc))) {
      continue;
    }

    if (app_desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
      continue;
    }
    if (std::strncmp(app_desc.project_name, kFirmwareProjectName,
            sizeof(kFirmwareProjectName)) != 0) {
      continue;
    }

    size_t image_size = 0;
    if (!CalculateImageSize(
            file, file_size, offset, image_header, &image_size)) {
      continue;
    }

    image_info->offset = offset;
    image_info->size = image_size;
    image_info->app_desc = app_desc;
    return true;
  }
  return false;
}

/**
 * @brief 将定长 ESP-IDF 文本字段复制到以空字符结尾的缓冲区
 * @param destination 目标文本缓冲区
 * @param destination_size 目标缓冲区大小
 * @param source 定长 ESP-IDF 文本字段
 * @param source_size 源字段大小
 */
void CopyImageText(char* destination, size_t destination_size,
    const char* source, size_t source_size) {
  if (destination == nullptr || destination_size == 0 || source == nullptr) {
    return;
  }
  const size_t copy_size =
      std::min(destination_size - 1, source_size);
  std::memcpy(destination, source, copy_size);
  destination[copy_size] = '\0';
}

/**
 * @brief 检查当前网络适配器是否支持显式激活 RPC
 * @param version 当前 ESP-Hosted 协处理器版本
 * @return ESP-Hosted 版本不低于 2.6.0 时返回 true
 */
bool SupportsOtaActivate(const esp_hosted_coprocessor_fwver_t& version) {
  return version.major1 > 2 ||
         (version.major1 == 2 && version.minor1 >= 6);
}

/**
 * @brief 比较分区固件和网络适配器当前固件，并在版本不同时执行更新
 * @return 无需更新或更新成功时返回 true
 */
bool CheckAndUpdateNetworkAdapter() {
  printf("Checking %s network adapter firmware...\n", kCoprocessorName);

  std::unique_ptr<FILE, decltype(&std::fclose)> firmware_file(
      std::fopen(kFirmwareFilePath, "rb"), &std::fclose);
  if (firmware_file == nullptr) {
    printf("Open firmware file '%s' failed\n", kFirmwareFilePath);
    return false;
  }

  size_t firmware_file_size = 0;
  if (!GetFirmwareFileSize(firmware_file.get(), &firmware_file_size)) {
    printf("Read firmware file size failed: '%s'\n", kFirmwareFilePath);
    return false;
  }

  FirmwareImageInfo image_info;
  if (!FindNetworkAdapterImage(
          firmware_file.get(), firmware_file_size, &image_info)) {
    printf("No valid %s network_adapter image found in '%s'\n",
        kCoprocessorName, kFirmwareFilePath);
    return false;
  }

  char target_version[sizeof(image_info.app_desc.version) + 1] = {};
  CopyImageText(target_version, sizeof(target_version),
      image_info.app_desc.version, sizeof(image_info.app_desc.version));
  printf("Stored firmware: version=%s offset=0x%X size=%u bytes\n",
      target_version, static_cast<unsigned int>(image_info.offset),
      static_cast<unsigned int>(image_info.size));

  esp_hosted_coprocessor_fwver_t current_version = {};
  const esp_err_t version_result =
      esp_hosted_get_coprocessor_fwversion(&current_version);
  if (version_result != ESP_OK) {
    printf("Read %s firmware version failed: %s\n", kCoprocessorName,
        esp_err_to_name(version_result));
    return false;
  }

  char current_version_text[32] = {};
  std::snprintf(current_version_text, sizeof(current_version_text),
      "%" PRIu32 ".%" PRIu32 ".%" PRIu32, current_version.major1,
      current_version.minor1, current_version.patch1);
  printf("Running firmware: version=%s\n", current_version_text);

  if (std::strcmp(current_version_text, target_version) == 0) {
    printf("Versions match; %s update is not required\n", kCoprocessorName);
    return true;
  }

  printf("Version differs; updating %s from %s to %s\n", kCoprocessorName,
      current_version_text, target_version);
  esp_err_t result = esp_hosted_slave_ota_begin();
  if (result != ESP_OK) {
    printf("%s OTA begin failed: %s\n", kCoprocessorName,
        esp_err_to_name(result));
    return false;
  }

  uint8_t chunk[kOtaChunkSize] = {};
  size_t sent_size = 0;
  uint32_t last_progress = 0;
  printf("%s update progress: 0%%\n", kCoprocessorName);
  while (sent_size < image_info.size) {
    const size_t chunk_size =
        std::min(kOtaChunkSize, image_info.size - sent_size);
    if (!ReadFirmwareFile(firmware_file.get(), firmware_file_size,
            image_info.offset + sent_size, chunk, chunk_size)) {
      printf("Read %s firmware failed at offset 0x%X\n", kCoprocessorName,
          static_cast<unsigned int>(image_info.offset + sent_size));
      esp_hosted_slave_ota_end();
      return false;
    }

    result = esp_hosted_slave_ota_write(
        chunk, static_cast<uint32_t>(chunk_size));
    if (result != ESP_OK) {
      printf("%s OTA write failed at %u bytes: %s\n", kCoprocessorName,
          static_cast<unsigned int>(sent_size), esp_err_to_name(result));
      esp_hosted_slave_ota_end();
      return false;
    }

    sent_size += chunk_size;
    const uint32_t progress = static_cast<uint32_t>(
        sent_size * 100 / image_info.size);
    if (progress == 100 || progress >= last_progress + 5) {
      printf("%s update progress: %" PRIu32 "%% (%u/%u bytes)\n",
          kCoprocessorName, progress, static_cast<unsigned int>(sent_size),
          static_cast<unsigned int>(image_info.size));
      last_progress = progress;
    }
  }

  result = esp_hosted_slave_ota_end();
  if (result != ESP_OK) {
    printf("%s OTA verification failed: %s\n", kCoprocessorName,
        esp_err_to_name(result));
    return false;
  }
  printf("%s firmware transfer and verification completed\n",
      kCoprocessorName);

  if (SupportsOtaActivate(current_version)) {
    result = esp_hosted_slave_ota_activate();
    if (result != ESP_OK) {
      printf("%s OTA activate failed: %s\n", kCoprocessorName,
          esp_err_to_name(result));
      return false;
    }
    printf("%s new firmware activated\n", kCoprocessorName);
  } else {
    printf("Current %s firmware activates the update during OTA end\n",
        kCoprocessorName);
  }

  firmware_file.reset();
  printf("Restarting ESP32-P4 to resynchronize with %s...\n",
      kCoprocessorName);
  vTaskDelay(pdMS_TO_TICKS(kHostRestartDelayMs));
  esp_restart();
  return true;
}

}  // 匿名命名空间

extern "C" void app_main(void) {
  namespace board = lilygo_device_driver::t_display_p4;

  printf("%s network adapter update example\n", kCoprocessorName);
  auto& driver = lilygo_device_driver::TDisplayP4Driver::GetInstance();
  driver.CreateDrivers();
  if (!driver.InitXl9535() || !driver.InitPower() ||
      !driver.ConfigXl9535()) {
    printf("Board and %s power initialization failed\n", kCoprocessorName);
    return;
  }

  if (!MountFirmwareStorage()) {
    return;
  }

  auto tool = std::make_unique<cpp_bus_driver::Tool>();
  if (!tool->SetGpioMode(board::gpio::button::kEsp32p4Boot,
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(kCoprocessorBootDelayMs));
  if (!InitEspHostedTransport()) {
    return;
  }

  printf("Press and release BOOT once to check the %s firmware\n",
      kCoprocessorName);
  bool was_pressed = false;
  TickType_t press_start_tick = 0;
  while (true) {
    const bool is_pressed =
        tool->GpioRead(board::gpio::button::kEsp32p4Boot) == 0;
    if (is_pressed && !was_pressed) {
      press_start_tick = xTaskGetTickCount();
    } else if (!is_pressed && was_pressed) {
      const uint32_t pressed_time_ms = static_cast<uint32_t>(
          (xTaskGetTickCount() - press_start_tick) * portTICK_PERIOD_MS);
      if (pressed_time_ms >= kButtonDebounceMs) {
        if ((xEventGroupGetBits(hosted_event_group) &
                kHostedTransportUpBit) != 0) {
          CheckAndUpdateNetworkAdapter();
        } else {
          printf("ESP-Hosted transport is not ready\n");
        }
        printf("Press BOOT again to check the %s firmware\n",
            kCoprocessorName);
      }
    }
    was_pressed = is_pressed;
    vTaskDelay(pdMS_TO_TICKS(kButtonPollIntervalMs));
  }
}
