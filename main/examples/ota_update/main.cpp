/*
 * @Description: BOOT 按键触发 ESP32-C5/C6 与 ESP32-P4 HTTPS OTA 更新示例
 * @Author: LILYGO_L
 * @Date: 2026-07-19 00:00:00
 * @LastEditTime: 2026-07-20 10:21:05
 * @License: GPL 3.0
 */
#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "common.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_ota.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_littlefs.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

namespace {

constexpr char kWifiSsid[] = "LilyGo-AABB";
constexpr char kWifiPassword[] = "xinyuandianzi";
// 编译示例前需要填写无线固件和主固件的两个 HTTPS OTA 地址。
constexpr char kWirelessFirmwareUrl[] = "";
constexpr char kMainFirmwareUrl[] = "";
constexpr char kStoragePartitionLabel[] = "storage";
constexpr char kStorageBasePath[] = "/storage";
constexpr char kWirelessFirmwarePath[] =
    "/storage/network_adapter_firmware.bin";
constexpr char kWirelessFirmwareTempPath[] =
    "/storage/network_adapter_firmware.tmp";
constexpr char kPendingMainFirmwareUpdatePath[] =
    "/storage/pending_main_firmware_ota";
constexpr char kWirelessFirmwareProjectName[] = "network_adapter";
#if CONFIG_SLAVE_IDF_TARGET_ESP32C5
constexpr esp_chip_id_t kExpectedWirelessChipId = ESP_CHIP_ID_ESP32C5;
constexpr char kWirelessChipName[] = "ESP32-C5";
#elif CONFIG_SLAVE_IDF_TARGET_ESP32C6
constexpr esp_chip_id_t kExpectedWirelessChipId = ESP_CHIP_ID_ESP32C6;
constexpr char kWirelessChipName[] = "ESP32-C6";
#else
#error "Unsupported Wi-Fi Remote slave target"
#endif
constexpr int kHttpTimeoutMs = 15000;
// 预签名下载地址可能远长于 ESP-IDF 默认的 512 字节 HTTP 发送缓冲区，
// 因此需要为请求地址和请求头预留足够空间。
constexpr int kHttpTxBufferSize = 4096;
constexpr size_t kHttpDownloadBufferSize = 4096;
constexpr size_t kImageSearchAlignment = 0x1000;
constexpr uint8_t kMaxImageSegmentCount = 16;
constexpr size_t kWirelessFirmwareOtaChunkSize = 1500;
constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr uint32_t kButtonPollIntervalMs = 20;
constexpr uint32_t kButtonDebounceMs = 40;
constexpr uint32_t kRestartDelayMs = 1000;
constexpr uint32_t kOtaTaskStackSize = 8192;
constexpr UBaseType_t kOtaTaskPriority = 5;
constexpr int kProgressStepPercent = 10;

EventGroupHandle_t g_wifi_events = nullptr;

struct FirmwareImageInfo {
  size_t offset = 0;
  size_t size = 0;
  esp_app_desc_t app_desc = {};
};

struct FirmwareDownloadContext {
  FILE* file = nullptr;
  size_t downloaded_size = 0;
  size_t next_progress_size = 64 * 1024;
  bool write_failed = false;
};

enum class WirelessFirmwareUpdateResult {
  kNotRequired,
  kRestarting,
  kFailed,
};

enum class MainFirmwareUpdateResult {
  kNotRequired,
  kRestarting,
  kFailed,
};

/**
 * @brief 挂载用于保存无线固件和更新状态的 LittleFS 分区
 * @return 挂载成功返回 true，失败返回 false
 */
bool MountStorage() {
  esp_vfs_littlefs_conf_t config = {};
  config.base_path = kStorageBasePath;
  config.partition_label = kStoragePartitionLabel;
  config.format_if_mount_failed = true;
  config.read_only = false;
  config.dont_mount = false;
  config.grow_on_mount = false;

  const esp_err_t result = esp_vfs_littlefs_register(&config);
  if (result != ESP_OK) {
    printf("Mount LittleFS partition '%s' failed: %s\n", kStoragePartitionLabel,
        esp_err_to_name(result));
    return false;
  }

  size_t total_bytes = 0;
  size_t used_bytes = 0;
  if (esp_littlefs_info(kStoragePartitionLabel, &total_bytes, &used_bytes) ==
      ESP_OK) {
    printf("LittleFS storage: total=%u used=%u bytes\n",
        static_cast<unsigned int>(total_bytes),
        static_cast<unsigned int>(used_bytes));
  }
  return true;
}

/**
 * @brief 在开始组合 OTA 前清空用于临时保存固件的 LittleFS 分区
 * @return 格式化并重新挂载成功返回 true，失败返回 false
 */
bool ClearStorageForUpdate() {
  printf("Clearing LittleFS storage before OTA\n");
  const esp_err_t result = esp_littlefs_format(kStoragePartitionLabel);
  if (result != ESP_OK) {
    printf("Clear LittleFS storage failed: %s\n", esp_err_to_name(result));
    return false;
  }

  size_t total_bytes = 0;
  size_t used_bytes = 0;
  if (esp_littlefs_info(kStoragePartitionLabel, &total_bytes, &used_bytes) ==
      ESP_OK) {
    printf("LittleFS storage cleared: total=%u used=%u bytes\n",
        static_cast<unsigned int>(total_bytes),
        static_cast<unsigned int>(used_bytes));
  }
  return true;
}

/**
 * @brief 从固件文件的指定偏移读取数据
 * @param file 已打开的固件文件
 * @param file_size 固件文件总长度
 * @param offset 文件内的读取偏移
 * @param destination 读取数据的目标缓冲区
 * @param size 需要读取的字节数
 * @return 读取成功返回 true，失败返回 false
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
 * @brief 获取固件文件长度并将文件位置恢复到开头
 * @param file 已打开的固件文件
 * @param file_size 用于返回固件文件总长度
 * @return 获取成功且文件非空返回 true，失败返回 false
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
 * @brief 根据 ESP-IDF 镜像头和各段信息计算应用镜像完整长度
 * @param file 已打开的固件文件
 * @param file_size 固件文件总长度
 * @param image_offset 应用镜像在文件内的起始偏移
 * @param image_header 已解析的 ESP-IDF 镜像头
 * @param image_size 用于返回应用镜像完整长度
 * @return 镜像结构和范围有效返回 true，失败返回 false
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
    if (!ReadFirmwareFile(
            file, file_size, cursor, &segment_header, sizeof(segment_header))) {
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

  // 校验和位于下一个 16 字节对齐块的最后一个字节。
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
 * @brief 在独立应用镜像或合并固件中查找目标无线固件应用镜像
 * @param file 已打开的固件文件
 * @param file_size 固件文件总长度
 * @param image_info 用于返回镜像偏移、长度和应用描述信息
 * @return 找到芯片型号和项目名称匹配的有效镜像返回 true，否则返回 false
 */
bool FindWirelessFirmwareImage(
    FILE* file, size_t file_size, FirmwareImageInfo* image_info) {
  if (file == nullptr || image_info == nullptr) {
    return false;
  }

  for (size_t offset = 0;
      offset + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
          sizeof(esp_app_desc_t) <=
      file_size;
      offset += kImageSearchAlignment) {
    esp_image_header_t image_header = {};
    if (!ReadFirmwareFile(
            file, file_size, offset, &image_header, sizeof(image_header)) ||
        image_header.magic != ESP_IMAGE_HEADER_MAGIC ||
        image_header.chip_id != kExpectedWirelessChipId ||
        image_header.segment_count == 0 ||
        image_header.segment_count > kMaxImageSegmentCount) {
      continue;
    }

    esp_app_desc_t app_desc = {};
    const size_t app_desc_offset = offset + sizeof(esp_image_header_t) +
                                   sizeof(esp_image_segment_header_t);
    if (!ReadFirmwareFile(
            file, file_size, app_desc_offset, &app_desc, sizeof(app_desc)) ||
        app_desc.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        std::strncmp(app_desc.project_name, kWirelessFirmwareProjectName,
            sizeof(kWirelessFirmwareProjectName)) != 0) {
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
 * @brief 打开并检查无线固件文件
 * @param path 无线固件文件路径
 * @param image_info 用于返回有效应用镜像信息
 * @return 固件文件有效返回 true，失败返回 false
 */
bool InspectWirelessFirmware(
    const char* path, FirmwareImageInfo* image_info) {
  std::unique_ptr<FILE, decltype(&std::fclose)> firmware_file(
      std::fopen(path, "rb"), &std::fclose);
  if (firmware_file == nullptr) {
    printf("Open Wireless firmware (%s) file '%s' failed\n", kWirelessChipName,
        path);
    return false;
  }

  size_t file_size = 0;
  if (!GetFirmwareFileSize(firmware_file.get(), &file_size) ||
      !FindWirelessFirmwareImage(firmware_file.get(), file_size, image_info)) {
    printf("No valid Wireless firmware (%s) image found in '%s'\n",
        kWirelessChipName, path);
    return false;
  }
  return true;
}

/**
 * @brief 将 HTTP 响应数据写入 LittleFS 中的无线固件临时文件
 * @param event HTTP 客户端事件及其附带的数据
 * @return 事件处理成功返回 ESP_OK，写文件失败返回 ESP_FAIL
 */
esp_err_t WirelessFirmwareDownloadEventHandler(
    esp_http_client_event_t* event) {
  if (event == nullptr || event->user_data == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }
  if (esp_http_client_get_status_code(event->client) != 200) {
    return ESP_OK;
  }

  auto* context = static_cast<FirmwareDownloadContext*>(event->user_data);
  if (context->file == nullptr ||
      std::fwrite(event->data, 1, event->data_len, context->file) !=
          static_cast<size_t>(event->data_len)) {
    context->write_failed = true;
    return ESP_FAIL;
  }

  context->downloaded_size += static_cast<size_t>(event->data_len);
  if (context->downloaded_size >= context->next_progress_size) {
    printf("Wireless firmware (%s) downloaded: %u bytes\n", kWirelessChipName,
        static_cast<unsigned int>(context->downloaded_size));
    context->next_progress_size += 64 * 1024;
  }
  return ESP_OK;
}

/**
 * @brief 从配置的 HTTPS 地址下载无线固件到 LittleFS
 * @return 下载、校验并安装固件文件成功返回 true，失败返回 false
 */
bool DownloadWirelessFirmware() {
  if (kWirelessFirmwareUrl[0] == '\0') {
    printf("Wireless firmware URL is not configured\n");
    return false;
  }

  std::unique_ptr<FILE, decltype(&std::fclose)> output_file(
      std::fopen(kWirelessFirmwareTempPath, "wb"), &std::fclose);
  if (output_file == nullptr) {
    printf("Create temporary Wireless firmware (%s) file failed\n",
        kWirelessChipName);
    std::remove(kWirelessFirmwareTempPath);
    return false;
  }

  FirmwareDownloadContext download_context;
  download_context.file = output_file.get();
  esp_http_client_config_t http_config = {};
  http_config.url = kWirelessFirmwareUrl;
  http_config.crt_bundle_attach = esp_crt_bundle_attach;
  http_config.timeout_ms = kHttpTimeoutMs;
  http_config.buffer_size = kHttpDownloadBufferSize;
  http_config.buffer_size_tx = kHttpTxBufferSize;
  http_config.event_handler = WirelessFirmwareDownloadEventHandler;
  http_config.user_data = &download_context;
  http_config.keep_alive_enable = true;
  http_config.max_redirection_count = 5;

  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == nullptr) {
    printf("Create Wireless firmware (%s) HTTP client failed\n",
        kWirelessChipName);
    output_file.reset();
    std::remove(kWirelessFirmwareTempPath);
    return false;
  }

  const esp_err_t result = esp_http_client_perform(client);
  const int status_code = esp_http_client_get_status_code(client);
  const int64_t content_length = esp_http_client_get_content_length(client);

  if (std::fflush(output_file.get()) != 0) {
    download_context.write_failed = true;
  }
  output_file.reset();
  esp_http_client_cleanup(client);

  bool download_ok = result == ESP_OK && status_code == 200 &&
                     !download_context.write_failed &&
                     download_context.downloaded_size > 0;
  if (!download_ok) {
    printf("Download Wireless firmware (%s) failed: result=%s HTTP=%d\n",
        kWirelessChipName, esp_err_to_name(result), status_code);
  }
  if (content_length > 0 &&
      download_context.downloaded_size != static_cast<size_t>(content_length)) {
    printf("Wireless firmware (%s) download is incomplete: %u/%lld bytes\n",
        kWirelessChipName,
        static_cast<unsigned int>(download_context.downloaded_size),
        static_cast<long long>(content_length));
    download_ok = false;
  }

  FirmwareImageInfo image_info;
  if (!download_ok ||
      !InspectWirelessFirmware(kWirelessFirmwareTempPath, &image_info)) {
    std::remove(kWirelessFirmwareTempPath);
    return false;
  }

  std::remove(kWirelessFirmwarePath);
  if (std::rename(kWirelessFirmwareTempPath, kWirelessFirmwarePath) !=
      0) {
    printf("Install downloaded Wireless firmware (%s) file failed\n",
        kWirelessChipName);
    std::remove(kWirelessFirmwareTempPath);
    return false;
  }

  printf("Downloaded Wireless firmware (%s) to '%s' (%u bytes)\n",
      kWirelessChipName, kWirelessFirmwarePath,
      static_cast<unsigned int>(download_context.downloaded_size));
  return true;
}

/**
 * @brief 检查是否存在重启后继续执行主固件 OTA 的状态标记
 * @return 状态标记存在返回 true，否则返回 false
 */
bool HasPendingMainFirmwareUpdate() {
  FILE* marker = std::fopen(kPendingMainFirmwareUpdatePath, "rb");
  if (marker == nullptr) {
    return false;
  }
  std::fclose(marker);
  return true;
}

/**
 * @brief 写入重启后继续执行主固件 OTA 的状态标记
 * @return 状态标记写入成功返回 true，失败返回 false
 */
bool SetPendingMainFirmwareUpdate() {
  FILE* marker = std::fopen(kPendingMainFirmwareUpdatePath, "wb");
  if (marker == nullptr) {
    printf("Create pending Main firmware OTA marker failed\n");
    return false;
  }
  const bool result = std::fwrite("1", 1, 1, marker) == 1;
  std::fclose(marker);
  if (!result) {
    std::remove(kPendingMainFirmwareUpdatePath);
  }
  return result;
}

/**
 * @brief 清除重启后继续执行主固件 OTA 的状态标记
 * @return 状态标记已清除或原本不存在返回 true，失败返回 false
 */
bool ClearPendingMainFirmwareUpdate() {
  if (std::remove(kPendingMainFirmwareUpdatePath) == 0 || errno == ENOENT) {
    return true;
  }
  printf("Remove pending Main firmware OTA marker failed\n");
  return false;
}

/**
 * @brief 判断当前无线固件是否支持显式激活 OTA 镜像
 * @param version 当前无线芯片的 ESP-Hosted 固件版本
 * @return 固件版本不低于 2.6.0 返回 true，否则返回 false
 */
bool SupportsWirelessFirmwareOtaActivate(
    const esp_hosted_coprocessor_fwver_t& version) {
  return version.major1 > 2 || (version.major1 == 2 && version.minor1 >= 6);
}

/**
 * @brief 比较并更新 LittleFS 中保存的无线固件
 * @return 无需更新返回 kNotRequired，准备重启返回 kRestarting，
 * 更新失败返回 kFailed
 */
WirelessFirmwareUpdateResult CheckAndUpdateWirelessFirmware() {
  FirmwareImageInfo image_info;
  if (!InspectWirelessFirmware(kWirelessFirmwarePath, &image_info)) {
    return WirelessFirmwareUpdateResult::kFailed;
  }

  char target_version[sizeof(image_info.app_desc.version) + 1] = {};
  std::memcpy(target_version, image_info.app_desc.version,
      sizeof(image_info.app_desc.version));
  target_version[sizeof(image_info.app_desc.version)] = '\0';

  esp_hosted_coprocessor_fwver_t current_version = {};
  const esp_err_t version_result =
      esp_hosted_get_coprocessor_fwversion(&current_version);
  if (version_result != ESP_OK) {
    printf("Read Wireless firmware (%s) version failed: %s\n",
        kWirelessChipName, esp_err_to_name(version_result));
    return WirelessFirmwareUpdateResult::kFailed;
  }

  char current_version_text[32] = {};
  std::snprintf(current_version_text, sizeof(current_version_text),
      "%" PRIu32 ".%" PRIu32 ".%" PRIu32, current_version.major1,
      current_version.minor1, current_version.patch1);
  printf("Stored Wireless firmware (%s): version=%s offset=0x%X size=%u bytes\n",
      kWirelessChipName, target_version,
      static_cast<unsigned int>(image_info.offset),
      static_cast<unsigned int>(image_info.size));
  printf("Running Wireless firmware (%s): version=%s\n", kWirelessChipName,
      current_version_text);

  if (std::strcmp(current_version_text, target_version) == 0) {
    printf("Wireless firmware (%s) is already up to date\n",
        kWirelessChipName);
    return WirelessFirmwareUpdateResult::kNotRequired;
  }

  std::unique_ptr<FILE, decltype(&std::fclose)> firmware_file(
      std::fopen(kWirelessFirmwarePath, "rb"), &std::fclose);
  size_t firmware_file_size = 0;
  if (firmware_file == nullptr ||
      !GetFirmwareFileSize(firmware_file.get(), &firmware_file_size)) {
    printf("Open stored Wireless firmware (%s) failed\n", kWirelessChipName);
    return WirelessFirmwareUpdateResult::kFailed;
  }

  auto chunk = std::make_unique<uint8_t[]>(kWirelessFirmwareOtaChunkSize);
  if (chunk == nullptr) {
    printf("Allocate Wireless firmware (%s) OTA buffer failed\n",
        kWirelessChipName);
    return WirelessFirmwareUpdateResult::kFailed;
  }

  // 无线固件激活后 ESP32-P4 必须重启重新同步；先写入标记，重启后自动继续主固件 OTA。
  if (!SetPendingMainFirmwareUpdate()) {
    return WirelessFirmwareUpdateResult::kFailed;
  }

  printf("Updating Wireless firmware (%s) from %s to %s\n", kWirelessChipName,
      current_version_text, target_version);
  esp_err_t result = esp_hosted_slave_ota_begin();
  if (result != ESP_OK) {
    printf("Wireless firmware (%s) OTA begin failed: %s\n", kWirelessChipName,
        esp_err_to_name(result));
    ClearPendingMainFirmwareUpdate();
    return WirelessFirmwareUpdateResult::kFailed;
  }

  size_t sent_size = 0;
  uint32_t last_progress = 0;
  printf("Wireless firmware (%s) update progress: 0%%\n", kWirelessChipName);
  while (sent_size < image_info.size) {
    const size_t chunk_size =
        std::min(kWirelessFirmwareOtaChunkSize, image_info.size - sent_size);
    if (!ReadFirmwareFile(firmware_file.get(), firmware_file_size,
            image_info.offset + sent_size, chunk.get(), chunk_size)) {
      printf("Read Wireless firmware (%s) failed at offset 0x%X\n",
          kWirelessChipName,
          static_cast<unsigned int>(image_info.offset + sent_size));
      esp_hosted_slave_ota_end();
      ClearPendingMainFirmwareUpdate();
      return WirelessFirmwareUpdateResult::kFailed;
    }

    result = esp_hosted_slave_ota_write(
        chunk.get(), static_cast<uint32_t>(chunk_size));
    if (result != ESP_OK) {
      printf("Wireless firmware (%s) OTA write failed at %u bytes: %s\n",
          kWirelessChipName, static_cast<unsigned int>(sent_size),
          esp_err_to_name(result));
      esp_hosted_slave_ota_end();
      ClearPendingMainFirmwareUpdate();
      return WirelessFirmwareUpdateResult::kFailed;
    }

    sent_size += chunk_size;
    const uint32_t progress =
        static_cast<uint32_t>(sent_size * 100 / image_info.size);
    if (progress == 100 || progress >= last_progress + 5) {
      printf("Wireless firmware (%s) update progress: %" PRIu32
             "%% (%u/%u bytes)\n",
          kWirelessChipName, progress, static_cast<unsigned int>(sent_size),
          static_cast<unsigned int>(image_info.size));
      last_progress = progress;
    }
  }

  result = esp_hosted_slave_ota_end();
  if (result != ESP_OK) {
    printf("Wireless firmware (%s) OTA verification failed: %s\n",
        kWirelessChipName, esp_err_to_name(result));
    ClearPendingMainFirmwareUpdate();
    return WirelessFirmwareUpdateResult::kFailed;
  }

  if (SupportsWirelessFirmwareOtaActivate(current_version)) {
    result = esp_hosted_slave_ota_activate();
    if (result != ESP_OK) {
      printf("Wireless firmware (%s) OTA activate failed: %s\n",
          kWirelessChipName, esp_err_to_name(result));
      ClearPendingMainFirmwareUpdate();
      return WirelessFirmwareUpdateResult::kFailed;
    }
    printf("Wireless firmware (%s) activated\n", kWirelessChipName);
  } else {
    printf("Current Wireless firmware (%s) activates the update during OTA end\n",
        kWirelessChipName);
  }

  firmware_file.reset();
  printf(
      "Restarting ESP32-P4 to resynchronize with Wireless firmware (%s); "
      "Main firmware OTA will resume "
      "automatically\n",
      kWirelessChipName);
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
  return WirelessFirmwareUpdateResult::kRestarting;
}

/**
 * @brief 处理 Wi-Fi 和 IP 状态事件
 * @param handler_arg 事件处理器上下文，本示例未使用
 * @param event_base 事件所属的事件基
 * @param event_id 具体事件编号
 * @param event_data 事件附带的数据
 */
void WifiEventHandler(void* handler_arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data) {
  static_cast<void>(handler_arg);
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    const esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) {
      printf("Initial Wi-Fi connection failed: %s\n", esp_err_to_name(result));
    }
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    printf("Connected to %s, IP: " IPSTR "\n", kWifiSsid,
        IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(g_wifi_events, kWifiConnectedBit);
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(g_wifi_events, kWifiConnectedBit);
    printf("Wi-Fi disconnected, reconnecting to %s\n", kWifiSsid);
    const esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) {
      printf("Wi-Fi reconnect failed: %s\n", esp_err_to_name(result));
    }
  }
}

/**
 * @brief 初始化通过 ESP-Hosted 无线芯片提供的 Wi-Fi Station
 * @return 初始化并启动成功返回 true，失败返回 false
 */
bool InitWifiStation() {
  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    printf("esp_netif_init failed: %s\n", esp_err_to_name(result));
    return false;
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    printf("Create default event loop failed: %s\n", esp_err_to_name(result));
    return false;
  }

  if (esp_netif_create_default_wifi_sta() == nullptr) {
    printf("Create default Wi-Fi station interface failed\n");
    return false;
  }

  g_wifi_events = xEventGroupCreate();
  if (g_wifi_events == nullptr) {
    printf("Create Wi-Fi event group failed\n");
    return false;
  }

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  result = esp_wifi_init(&init_config);
  if (result != ESP_OK) {
    printf("esp_wifi_init failed: %s\n", esp_err_to_name(result));
    return false;
  }

  result = esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler, nullptr);
  if (result != ESP_OK) {
    printf(
        "Register Wi-Fi event handler failed: %s\n", esp_err_to_name(result));
    return false;
  }

  result = esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, WifiEventHandler, nullptr);
  if (result != ESP_OK) {
    printf("Register IP event handler failed: %s\n", esp_err_to_name(result));
    return false;
  }

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK) {
    printf("Set Wi-Fi station mode failed: %s\n", esp_err_to_name(result));
    return false;
  }

  wifi_config_t wifi_config = {};
  const size_t ssid_length = std::strlen(kWifiSsid);
  const size_t password_length = std::strlen(kWifiPassword);
  if (ssid_length == 0 || ssid_length > sizeof(wifi_config.sta.ssid) ||
      password_length > sizeof(wifi_config.sta.password)) {
    printf("Wi-Fi SSID or password length is invalid\n");
    return false;
  }
  std::memcpy(wifi_config.sta.ssid, kWifiSsid, ssid_length);
  std::memcpy(wifi_config.sta.password, kWifiPassword, password_length);
  wifi_config.sta.threshold.authmode =
      password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

  result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (result != ESP_OK) {
    printf("Set Wi-Fi configuration failed: %s\n", esp_err_to_name(result));
    return false;
  }

  result = esp_wifi_start();
  if (result != ESP_OK) {
    printf("esp_wifi_start failed: %s\n", esp_err_to_name(result));
    return false;
  }

  result = esp_wifi_set_ps(WIFI_PS_NONE);
  if (result != ESP_OK) {
    printf("Disable Wi-Fi power save failed; continuing: %s\n",
        esp_err_to_name(result));
  }

  return true;
}

/**
 * @brief 检查服务器主固件镜像的项目名称是否与当前程序一致
 * @param new_app 服务器主固件镜像中的应用描述信息
 * @return 项目名称一致返回 true，否则返回 false
 */
bool ValidateMainFirmwareImage(const esp_app_desc_t& new_app) {
  const esp_app_desc_t* running_app = esp_app_get_description();
  if (running_app == nullptr) {
    printf("Read running Main firmware description failed\n");
    return false;
  }

  printf("Running Main firmware: project=%s version=%s\n",
      running_app->project_name, running_app->version);
  printf("Server Main firmware:  project=%s version=%s\n", new_app.project_name,
      new_app.version);

  if (std::strncmp(new_app.project_name, running_app->project_name,
          sizeof(new_app.project_name)) != 0) {
    printf("Main firmware project name does not match the running firmware\n");
    return false;
  }

  return true;
}

/**
 * @brief 比较服务器主固件镜像与当前运行程序的版本号
 * @param new_app 服务器主固件镜像中的应用描述信息
 * @return 版本号相同返回 true，否则返回 false
 */
bool IsSameMainFirmwareVersion(const esp_app_desc_t& new_app) {
  const esp_app_desc_t* running_app = esp_app_get_description();
  if (running_app == nullptr) {
    return false;
  }
  return std::strncmp(new_app.version, running_app->version,
             sizeof(new_app.version)) == 0;
}

/**
 * @brief 按固定百分比间隔输出主固件 OTA 下载进度
 * @param ota_handle 当前主固件 HTTPS OTA 会话句柄
 * @param last_progress_percent 上一次已经输出的进度百分比
 */
void PrintMainFirmwareProgress(
    esp_https_ota_handle_t ota_handle, int* last_progress_percent) {
  const int image_size = esp_https_ota_get_image_size(ota_handle);
  const int image_read = esp_https_ota_get_image_len_read(ota_handle);
  if (image_size <= 0 || image_read < 0 || last_progress_percent == nullptr) {
    return;
  }

  const int progress_percent = image_read * 100 / image_size;
  if (progress_percent < *last_progress_percent + kProgressStepPercent &&
      progress_percent != 100) {
    return;
  }

  *last_progress_percent = progress_percent;
  printf("Main firmware download progress: %d%% (%d/%d bytes)\n",
      progress_percent, image_read, image_size);
}

/**
 * @brief 检查并安装服务器上的 ESP32-P4 主固件
 * @return 无需更新返回 kNotRequired，准备重启返回 kRestarting，
 * 更新失败返回 kFailed
 */
MainFirmwareUpdateResult CheckAndUpdateMainFirmware() {
  if (kMainFirmwareUrl[0] == '\0') {
    printf("Main firmware (ESP32-P4) URL is not configured\n");
    return MainFirmwareUpdateResult::kFailed;
  }

  esp_http_client_config_t http_config = {};
  http_config.url = kMainFirmwareUrl;
  http_config.crt_bundle_attach = esp_crt_bundle_attach;
  http_config.timeout_ms = kHttpTimeoutMs;
  http_config.buffer_size_tx = kHttpTxBufferSize;
  http_config.keep_alive_enable = true;

  esp_https_ota_config_t ota_config = {};
  ota_config.http_config = &http_config;

  // 预签名下载地址可能包含临时凭据，因此日志中不输出完整 URL。
  printf("Checking Main firmware from the configured HTTPS URL\n");
  esp_https_ota_handle_t ota_handle = nullptr;
  esp_err_t result = esp_https_ota_begin(&ota_config, &ota_handle);
  if (result != ESP_OK) {
    printf("Main firmware HTTPS OTA begin failed: %s\n",
        esp_err_to_name(result));
    return MainFirmwareUpdateResult::kFailed;
  }

  esp_app_desc_t new_app = {};
  result = esp_https_ota_get_img_desc(ota_handle, &new_app);
  if (result != ESP_OK) {
    printf("Read OTA image description failed: %s\n", esp_err_to_name(result));
    esp_https_ota_abort(ota_handle);
    return MainFirmwareUpdateResult::kFailed;
  }

  if (!ValidateMainFirmwareImage(new_app)) {
    esp_https_ota_abort(ota_handle);
    return MainFirmwareUpdateResult::kFailed;
  }

  if (IsSameMainFirmwareVersion(new_app)) {
    printf("Main firmware is already up to date\n");
    esp_https_ota_abort(ota_handle);
    return MainFirmwareUpdateResult::kNotRequired;
  }

  printf("New Main firmware found; starting automatic update\n");
  int last_progress_percent = -kProgressStepPercent;
  do {
    result = esp_https_ota_perform(ota_handle);
    PrintMainFirmwareProgress(ota_handle, &last_progress_percent);
  } while (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  if (result != ESP_OK) {
    printf("Main firmware OTA download failed: %s\n",
        esp_err_to_name(result));
    esp_https_ota_abort(ota_handle);
    return MainFirmwareUpdateResult::kFailed;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    printf("Main firmware OTA ended before the complete image was received\n");
    esp_https_ota_abort(ota_handle);
    return MainFirmwareUpdateResult::kFailed;
  }

  result = esp_https_ota_finish(ota_handle);
  if (result != ESP_OK) {
    printf("Main firmware OTA image verification failed: %s\n",
        esp_err_to_name(result));
    return MainFirmwareUpdateResult::kFailed;
  }

  ClearPendingMainFirmwareUpdate();
  printf("Main firmware OTA succeeded; restarting into version %s\n",
      new_app.version);
  vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
  esp_restart();
  return MainFirmwareUpdateResult::kRestarting;
}

/**
 * @brief 执行主固件 OTA 阶段并在无需更新时清除续跑标记
 */
void RunMainFirmwareUpdateStage() {
  const MainFirmwareUpdateResult result = CheckAndUpdateMainFirmware();
  if (result == MainFirmwareUpdateResult::kNotRequired) {
    ClearPendingMainFirmwareUpdate();
  }
}

/**
 * @brief 按照无线固件优先、主固件随后的顺序执行组合更新
 */
void RunCombinedUpdate() {
  if (kWirelessFirmwareUrl[0] == '\0' || kMainFirmwareUrl[0] == '\0') {
    printf("Configure both Wireless firmware and Main firmware URLs first\n");
    return;
  }

  if (!ClearStorageForUpdate()) {
    return;
  }

  printf("Step 1/2: downloading and checking Wireless firmware (%s)\n",
      kWirelessChipName);
  if (!DownloadWirelessFirmware()) {
    return;
  }

  const WirelessFirmwareUpdateResult wireless_firmware_result =
      CheckAndUpdateWirelessFirmware();
  if (wireless_firmware_result == WirelessFirmwareUpdateResult::kFailed) {
    return;
  }
  if (wireless_firmware_result == WirelessFirmwareUpdateResult::kRestarting) {
    return;
  }

  printf("Step 2/2: checking Main firmware (ESP32-P4)\n");
  RunMainFirmwareUpdateStage();
}

/**
 * @brief 确认当前运行的主固件 OTA 镜像并取消回滚
 */
void ConfirmRunningMainFirmware() {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
  const esp_partition_t* running_partition = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_result =
      esp_ota_get_state_partition(running_partition, &ota_state);
  if (state_result != ESP_OK || ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }

  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result == ESP_OK) {
    printf("Running Main firmware marked as valid\n");
  } else {
    printf("Mark running Main firmware valid failed: %s\n",
        esp_err_to_name(result));
  }
#endif
}

/**
 * @brief 等待网络连接、恢复未完成流程并轮询 BOOT 按键
 * @param task_parameter FreeRTOS 任务参数，本示例未使用
 */
void OtaTask(void* task_parameter) {
  static_cast<void>(task_parameter);
  auto tool = std::make_unique<cpp_bus_driver::Tool>();
  if (!tool->SetGpioMode(common::BootButtonGpio(),
          cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    vTaskDelete(nullptr);
    return;
  }

  xEventGroupWaitBits(
      g_wifi_events, kWifiConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
  ConfirmRunningMainFirmware();
  if (HasPendingMainFirmwareUpdate()) {
    printf("Verifying Wireless firmware (%s) before resuming Main firmware "
           "OTA\n",
        kWirelessChipName);
    const WirelessFirmwareUpdateResult wireless_firmware_result =
        CheckAndUpdateWirelessFirmware();
    if (wireless_firmware_result ==
        WirelessFirmwareUpdateResult::kNotRequired) {
      RunMainFirmwareUpdateStage();
    }
  }
  printf("Press and release BOOT once to update Wireless firmware (%s) first, "
         "then Main firmware (ESP32-P4)\n",
      kWirelessChipName);

  bool was_pressed = false;
  TickType_t press_start_tick = 0;
  while (true) {
    const bool is_pressed = tool->GpioRead(common::BootButtonGpio()) == 0;
    if (is_pressed && !was_pressed) {
      press_start_tick = xTaskGetTickCount();
    } else if (!is_pressed && was_pressed) {
      const uint32_t pressed_time_ms = static_cast<uint32_t>(
          (xTaskGetTickCount() - press_start_tick) * portTICK_PERIOD_MS);
      if (pressed_time_ms >= kButtonDebounceMs) {
        if ((xEventGroupGetBits(g_wifi_events) & kWifiConnectedBit) != 0) {
          RunCombinedUpdate();
        } else {
          printf("Wi-Fi is not connected; OTA check skipped\n");
        }
        printf("Press BOOT again to update Wireless firmware (%s) first, then "
               "Main firmware (ESP32-P4)\n",
            kWirelessChipName);
      }
    }
    was_pressed = is_pressed;
    vTaskDelay(pdMS_TO_TICKS(kButtonPollIntervalMs));
  }
}

}  // namespace

extern "C" void app_main(void) {
  printf(
      "ESP32-P4 BOOT-triggered HTTPS OTA example on %s\n", common::kBoardName);
  if (!common::InitDriver()) {
    printf("Board driver initialization failed\n");
    return;
  }

  if (!MountStorage()) {
    return;
  }

  const esp_err_t hosted_result = static_cast<esp_err_t>(esp_hosted_init());
  if (hosted_result != ESP_OK) {
    printf("esp_hosted_init failed: %s\n", esp_err_to_name(hosted_result));
    return;
  }

  if (!InitWifiStation()) {
    return;
  }

  const BaseType_t task_result = xTaskCreate(OtaTask, "ota_update",
      kOtaTaskStackSize, nullptr, kOtaTaskPriority, nullptr);
  if (task_result != pdPASS) {
    printf("Create OTA task failed\n");
  }
}
