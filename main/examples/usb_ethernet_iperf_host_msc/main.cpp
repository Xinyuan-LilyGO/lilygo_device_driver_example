/*
 * @Description: 在 T-Display-P4 V2.0 上通过 USB Hub 测试网卡和 U 盘，按 BOOT 安全卸载 U 盘
 * @Author: LILYGO_L
 * @Date: 2026-05-20
 * @LastEditTime: 2026-09-11 17:24:33
 * @License: GPL 3.0
 */
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iperf_cmd.h"
#include "sdkconfig.h"
#include "usb/ethernet.h"
#include "usb/host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "usb/usb_host.h"

// 0: 板载 RTL8152B；1: Type-A 接口外接 RTL8152B。
#define USB_ETHERNET_IPERF_HOST_MSC_USE_EXTERNAL_ADAPTER 0
// 1: 使用 P4 派生 MAC 和混杂接收；0: 使用网卡的 ECM MAC。
#define USB_ETHERNET_IPERF_HOST_MSC_USE_SOFTWARE_MAC 1

#ifndef USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST
#define USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST 1
#endif

namespace {

static const char* TAG = "usb_hub_eth_msc";

static constexpr char MSC_MOUNT_ROOT[] = "/usb";
static constexpr int MAX_MSC_DEVICES = CONFIG_FATFS_VOLUME_COUNT;
#if USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST
static constexpr size_t MSC_RW_TEST_FILE_SIZE = 4096;
static constexpr char MSC_RW_TEST_FILE_NAME[] = "usb_hub_rw_test.txt";
#endif

struct MscDeviceEntry {
  uint8_t usb_addr;
  msc_host_device_handle_t msc_device;
  msc_host_vfs_handle_t vfs_handle;
#if USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST
  uint32_t rw_test_counter;
#endif
};

struct AppMessage {
  enum {
    DEVICE_CONNECTED,
    DEVICE_DISCONNECTED,
  } id;

  union {
    uint8_t new_dev_address;
    msc_host_device_handle_t device_handle;
  } data;
};

static QueueHandle_t s_app_queue = nullptr;
static MscDeviceEntry* s_msc_devices[MAX_MSC_DEVICES] = {};

static int find_free_msc_slot(void) {
  for (int i = 0; i < MAX_MSC_DEVICES; ++i) {
    if (s_msc_devices[i] == nullptr) {
      return i;
    }
  }
  return -1;
}

static int find_msc_slot_by_handle(msc_host_device_handle_t handle) {
  for (int i = 0; i < MAX_MSC_DEVICES; ++i) {
    if (s_msc_devices[i] != nullptr && s_msc_devices[i]->msc_device == handle) {
      return i;
    }
  }
  return -1;
}

static void scan_msc_files(int slot) {
  char mount_path[16] = {};
  snprintf(mount_path, sizeof(mount_path), "%s%d", MSC_MOUNT_ROOT, slot);

  DIR* dir = opendir(mount_path);
  if (dir == nullptr) {
    ESP_LOGW(TAG, "Open %s failed: errno=%d", mount_path, errno);
    return;
  }

  ESP_LOGI(TAG, "Listing %s", mount_path);
  int entry_count = 0;
  struct dirent* entry = nullptr;

  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char path[256] = {};
    if (snprintf(path, sizeof(path), "%s/%s", mount_path, entry->d_name) >=
        static_cast<int>(sizeof(path))) {
      ESP_LOGW(TAG, "Skip long path: %s/%s", mount_path, entry->d_name);
      continue;
    }

    struct stat st = {};
    if (stat(path, &st) != 0) {
      ESP_LOGW(TAG, "stat %s failed: errno=%d", path, errno);
      continue;
    }

    ++entry_count;
    if (S_ISDIR(st.st_mode)) {
      ESP_LOGI(TAG, "[DIR ] %s", path);
    } else {
      ESP_LOGI(TAG, "[FILE] %s (%lld bytes)", path,
          static_cast<long long>(st.st_size));
    }
  }

  closedir(dir);

  if (entry_count == 0) {
    ESP_LOGI(TAG, "%s is empty", mount_path);
  }
}

#if USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST
static void fill_text_test_buffer(
    uint8_t* buffer, size_t size, int slot, uint32_t counter) {
  if (size == 0) {
    return;
  }

  const int header_len = snprintf(reinterpret_cast<char*>(buffer), size,
      "usb_hub_rw_test slot=%d counter=%" PRIu32 "\r\n", slot, counter);
  size_t start = header_len > 0 ? static_cast<size_t>(header_len) : 0;
  if (start > size) {
    start = size;
  }

  for (size_t i = start; i < size; ++i) {
    buffer[i] = static_cast<uint8_t>('A' + ((counter + slot + i) % 26));
  }

  if (size >= 2) {
    buffer[size - 2] = '\r';
    buffer[size - 1] = '\n';
  }
}

static FILE* open_rw_test_file_for_write(const char* path) {
  FILE* file = fopen(path, "r+b");
  if (file == nullptr && errno == ENOENT) {
    file = fopen(path, "w+b");
  }
  return file;
}

static esp_err_t close_file_checked(
    FILE** file, const char* path, const char* operation) {
  if (*file == nullptr) {
    return ESP_OK;
  }

  if (fclose(*file) != 0) {
    ESP_LOGW(TAG, "Close %s after %s failed: errno=%d", path, operation, errno);
    *file = nullptr;
    return ESP_FAIL;
  }

  *file = nullptr;
  return ESP_OK;
}

static esp_err_t run_msc_rw_test_once(int slot) {
  char mount_path[16] = {};
  snprintf(mount_path, sizeof(mount_path), "%s%d", MSC_MOUNT_ROOT, slot);

  char file_path[64] = {};
  snprintf(file_path, sizeof(file_path), "%s/%s", mount_path,
      MSC_RW_TEST_FILE_NAME);

  uint8_t* write_buffer =
      static_cast<uint8_t*>(malloc(MSC_RW_TEST_FILE_SIZE));
  uint8_t* read_buffer = static_cast<uint8_t*>(malloc(MSC_RW_TEST_FILE_SIZE));
  if (write_buffer == nullptr || read_buffer == nullptr) {
    free(write_buffer);
    free(read_buffer);
    return ESP_ERR_NO_MEM;
  }

  MscDeviceEntry* entry = s_msc_devices[slot];
  if (entry == nullptr) {
    free(write_buffer);
    free(read_buffer);
    return ESP_ERR_INVALID_STATE;
  }

  const uint32_t counter = ++entry->rw_test_counter;
  fill_text_test_buffer(write_buffer, MSC_RW_TEST_FILE_SIZE, slot, counter);
  memset(read_buffer, 0, MSC_RW_TEST_FILE_SIZE);

  esp_err_t ret = ESP_OK;
  FILE* file = open_rw_test_file_for_write(file_path);
  if (file == nullptr) {
    ESP_LOGW(TAG, "Open %s for write failed: errno=%d", file_path, errno);
    ret = ESP_FAIL;
    goto cleanup;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    ESP_LOGW(TAG, "Seek %s for write failed: errno=%d", file_path, errno);
    ret = ESP_FAIL;
    close_file_checked(&file, file_path, "seek-write");
    goto cleanup;
  }

  if (fwrite(write_buffer, 1, MSC_RW_TEST_FILE_SIZE, file) !=
      MSC_RW_TEST_FILE_SIZE) {
    ESP_LOGW(TAG, "Write %s failed: errno=%d", file_path, errno);
    ret = ESP_FAIL;
    close_file_checked(&file, file_path, "write");
    goto cleanup;
  }

  if (fflush(file) != 0) {
    ESP_LOGW(TAG, "Flush %s failed: errno=%d", file_path, errno);
    ret = ESP_FAIL;
    close_file_checked(&file, file_path, "flush");
    goto cleanup;
  }

  ret = close_file_checked(&file, file_path, "write");
  if (ret != ESP_OK) {
    goto cleanup;
  }

  file = fopen(file_path, "rb");
  if (file == nullptr) {
    ESP_LOGW(TAG, "Open %s for read failed: errno=%d", file_path, errno);
    ret = ESP_FAIL;
    goto cleanup;
  }

  if (fread(read_buffer, 1, MSC_RW_TEST_FILE_SIZE, file) !=
      MSC_RW_TEST_FILE_SIZE) {
    ESP_LOGW(TAG, "Read %s failed: errno=%d", file_path, errno);
    ret = ESP_FAIL;
    close_file_checked(&file, file_path, "read");
    goto cleanup;
  }

  ret = close_file_checked(&file, file_path, "read");
  if (ret != ESP_OK) {
    goto cleanup;
  }

  if (memcmp(write_buffer, read_buffer, MSC_RW_TEST_FILE_SIZE) != 0) {
    ESP_LOGE(TAG, "MSC RW verify failed: %s", file_path);
    ret = ESP_FAIL;
    goto cleanup;
  }

  ESP_LOGI(TAG, "MSC RW verify OK: %s (%u bytes)", file_path,
      static_cast<unsigned>(MSC_RW_TEST_FILE_SIZE));

cleanup:
  if (file != nullptr) {
    close_file_checked(&file, file_path, "cleanup");
  }
  free(write_buffer);
  free(read_buffer);
  return ret;
}

static void run_msc_rw_tests(void) {
  for (int slot = 0; slot < MAX_MSC_DEVICES; ++slot) {
    if (s_msc_devices[slot] == nullptr || s_msc_devices[slot]->vfs_handle == nullptr) {
      continue;
    }

    const esp_err_t ret = run_msc_rw_test_once(slot);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "MSC RW test slot %d failed: %s", slot, esp_err_to_name(ret));
    }
  }
}
#endif

static esp_err_t allocate_new_msc_device(uint8_t usb_addr) {
  const int slot = find_free_msc_slot();
  if (slot < 0) {
    ESP_LOGW(TAG, "No free MSC slots, max=%d", MAX_MSC_DEVICES);
    return ESP_ERR_NOT_FOUND;
  }

  MscDeviceEntry* entry =
      static_cast<MscDeviceEntry*>(calloc(1, sizeof(MscDeviceEntry)));
  ESP_RETURN_ON_FALSE(entry != nullptr, ESP_ERR_NO_MEM, TAG,
      "allocate MSC device entry failed");

  esp_err_t ret = msc_host_install_device(usb_addr, &entry->msc_device);
  if (ret != ESP_OK) {
    free(entry);
    return ret;
  }

  entry->usb_addr = usb_addr;
  // 安装后即保留槽位，挂载失败且设备释放失败时仍可通过 BOOT 重试卸载。
  s_msc_devices[slot] = entry;

  const esp_vfs_fat_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 8192,
      .disk_status_check_enable = false,
      .use_one_fat = false,
  };

  char mount_path[16] = {};
  snprintf(mount_path, sizeof(mount_path), "%s%d", MSC_MOUNT_ROOT, slot);

  ret = msc_host_vfs_register(
      entry->msc_device, mount_path, &mount_config, &entry->vfs_handle);
  if (ret != ESP_OK) {
    const esp_err_t mount_ret = ret;
    ESP_LOGE(TAG, "msc_host_vfs_register %s failed: %s", mount_path,
        esp_err_to_name(ret));
    const esp_err_t uninstall_ret = msc_host_uninstall_device(entry->msc_device);
    if (uninstall_ret != ESP_OK) {
      ESP_LOGW(TAG, "msc_host_uninstall_device after mount failure failed: %s",
          esp_err_to_name(uninstall_ret));
    } else {
      free(entry);
      s_msc_devices[slot] = nullptr;
    }
    return mount_ret;
  }

  msc_host_device_info_t info = {};
  ret = msc_host_get_device_info(entry->msc_device, &info);
  if (ret == ESP_OK) {
    const uint64_t capacity_mb =
        (static_cast<uint64_t>(info.sector_size) * info.sector_count) /
        (1024 * 1024);
    ESP_LOGI(TAG,
        "MSC mounted at %s: VID=0x%04X PID=0x%04X capacity=%" PRIu64
        "MB sector=%" PRIu32 " count=%" PRIu32,
        mount_path, info.idVendor, info.idProduct, capacity_mb,
        info.sector_size, info.sector_count);
  }

  scan_msc_files(slot);
  return ESP_OK;
}

/**
 * @brief 卸载指定 U 盘，失败时保留尚未释放的句柄以便重试。
 * @param slot U 盘槽位编号，无效或空槽位视为已释放。
 * @return 文件系统和设备均释放成功返回 true，否则返回 false。
 */
static bool free_msc_device(int slot) {
  if (slot < 0 || slot >= MAX_MSC_DEVICES || s_msc_devices[slot] == nullptr) {
    return true;
  }

  MscDeviceEntry* entry = s_msc_devices[slot];

  if (entry->vfs_handle != nullptr) {
    const esp_err_t ret = msc_host_vfs_unregister(entry->vfs_handle);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "msc_host_vfs_unregister slot %d failed: %s", slot,
          esp_err_to_name(ret));
      return false;
    }
    entry->vfs_handle = nullptr;
  }
  if (entry->msc_device != nullptr) {
    const esp_err_t ret = msc_host_uninstall_device(entry->msc_device);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "msc_host_uninstall_device slot %d failed: %s", slot,
          esp_err_to_name(ret));
      return false;
    }
    entry->msc_device = nullptr;
  }

  ESP_LOGI(TAG, "MSC slot %d unmounted", slot);
  free(entry);
  s_msc_devices[slot] = nullptr;
  return true;
}

static void msc_event_cb(const msc_host_event_t* event, void* arg) {
  if (s_app_queue == nullptr) {
    return;
  }

  AppMessage message = {};
  if (event->event == msc_host_event_t::MSC_DEVICE_CONNECTED) {
    ESP_LOGI(TAG, "MSC device connected, usb_addr=%u", event->device.address);
    message.id = AppMessage::DEVICE_CONNECTED;
    message.data.new_dev_address = event->device.address;
    xQueueSend(s_app_queue, &message, portMAX_DELAY);
  } else if (event->event == msc_host_event_t::MSC_DEVICE_DISCONNECTED) {
    ESP_LOGI(TAG, "MSC device disconnected");
    message.id = AppMessage::DEVICE_DISCONNECTED;
    message.data.device_handle = event->device.handle;
    xQueueSend(s_app_queue, &message, portMAX_DELAY);
  } else {
    ESP_LOGW(TAG, "Unsupported MSC event: %d", event->event);
  }
}

static void msc_app_task(void* arg) {
  bool eject_failed = false;
#if USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST
  TickType_t last_test_finished = 0;
#endif
  while (true) {
    AppMessage msg = {};
    const TickType_t wait_ticks = pdMS_TO_TICKS(50);

    if (xQueueReceive(s_app_queue, &msg, wait_ticks) == pdTRUE) {
      if (msg.id == AppMessage::DEVICE_CONNECTED) {
        esp_err_t ret = allocate_new_msc_device(msg.data.new_dev_address);
        if (ret != ESP_OK) {
          ESP_LOGE(TAG, "Install MSC device failed: %s", esp_err_to_name(ret));
        }
      } else if (msg.id == AppMessage::DEVICE_DISCONNECTED) {
        const int slot = find_msc_slot_by_handle(msg.data.device_handle);
        if (!free_msc_device(slot)) {
          eject_failed = true;
        }
      }
    }

    // 卸载和文件读写均由本任务执行，按键任务只提交请求。
    if (common::usb_host::TakeMscEjectRequest()) {
      bool all_released = true;
      for (int slot = 0; slot < MAX_MSC_DEVICES; ++slot) {
        all_released &= free_msc_device(slot);
      }
      eject_failed = !all_released;
      if (all_released) {
        ESP_LOGI(TAG, "MSC unmounted; safe to remove USB drives. "
                      "Unplug and reconnect to test again.");
      } else {
        ESP_LOGE(TAG, "MSC eject failed; RW paused. "
                      "Do not remove USB drives; press BOOT to retry.");
      }
      continue;
    }

    // 卸载失败时暂停读写，等待再次按 BOOT 重试。
    if (eject_failed) {
      continue;
    }
#if USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST
    if (xTaskGetTickCount() - last_test_finished >= pdMS_TO_TICKS(1000)) {
      run_msc_rw_tests();
      last_test_finished = xTaskGetTickCount();
    }
#endif
  }
}

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

  const msc_host_driver_config_t msc_config = {
      .create_backround_task = true,
      .task_priority = 5,
      .stack_size = 4096,
      .core_id = 0,
      .callback = msc_event_cb,
      .callback_arg = nullptr,
  };
  ESP_ERROR_CHECK(msc_host_install(&msc_config));

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
  msc_host_uninstall();
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

  repl_config.prompt = "hub_eth_msc>";
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
  printf(" |          USB Hub RTL8152B Ethernet + MSC Host        |\n");
  printf(" |                                                      |\n");
  printf(" | U disk will mount as /usb0, /usb1 ...                |\n");
  printf(" | Type 'help' to display available iperf commands.     |\n");
  printf(" | Example: iperf -s -i 1 -t 30                         |\n");
  printf(" | Example: iperf -c <server_ip> -i 1 -t 30             |\n");
  printf(" |                                                      |\n");
  printf(" ========================================================\n\n");

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

}  // namespace

extern "C" void app_main(void) {
  printf("USB Ethernet + MSC test on T-Display-P4 V2.0\n");

  if (!common::usb_host::InitPower()) {
    ESP_LOGE(TAG, "USB host power initialization failed");
    return;
  }

  if (!common::usb_host::InitMscTestControl()) {
    ESP_LOGE(TAG, "BOOT control initialization failed");
    common::usb_host::DisablePower();
    return;
  }
#if USB_ETHERNET_IPERF_HOST_MSC_ENABLE_MSC_RW_TEST
  ESP_LOGI(TAG,
      "MSC RW starts on insertion; press BOOT to safely eject "
      "(%u bytes, 1000 ms interval)",
      static_cast<unsigned>(MSC_RW_TEST_FILE_SIZE));
#else
  ESP_LOGI(TAG, "MSC RW test disabled; press BOOT to safely eject");
#endif

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  s_app_queue = xQueueCreate(8, sizeof(AppMessage));
  ESP_ERROR_CHECK(s_app_queue != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

  BaseType_t task_created =
      xTaskCreate(msc_app_task, "msc_app", 4096, nullptr, 4, nullptr);
  ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_FAIL);

  task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096,
      xTaskGetCurrentTaskHandle(), 5, nullptr, 0);
  ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_FAIL);

  uint32_t notify_value = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000));
  ESP_ERROR_CHECK(notify_value > 0 ? ESP_OK : ESP_ERR_TIMEOUT);

  const common::usb_ethernet::Config ethernet_config = {
      .adapter = USB_ETHERNET_IPERF_HOST_MSC_USE_EXTERNAL_ADAPTER
          ? common::usb_ethernet::Adapter::kExternal
          : common::usb_ethernet::Adapter::kOnboard,
      .use_software_mac = USB_ETHERNET_IPERF_HOST_MSC_USE_SOFTWARE_MAC != 0,
  };
  ESP_ERROR_CHECK(common::usb_ethernet::Init(ethernet_config));

  ESP_LOGI(TAG,
      "Connect RTL8152B and USB flash drive through the USB hub, then run iperf");
  start_iperf_console();

  common::usb_ethernet::WaitForIp();
  vTaskSuspend(nullptr);
}
