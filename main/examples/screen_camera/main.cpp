/*
 * @Description: screen_camera
 * @Author: LILYGO_L
 * @Date: 2026-07-10 11:03:22
 * @LastEditTime: 2026-07-13 00:00:00
 * @License: GPL 3.0
 */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "common.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"

namespace {

constexpr uint32_t kCameraBufferCount = 2;
constexpr uint32_t kCameraFrameIntervalMs = 10;
constexpr uint32_t kCameraOutputClearFrameCount = 3;
constexpr const char* kCameraDeviceName = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;

ppa_client_handle_t g_ppa_handle = nullptr;
size_t g_cache_line_size = 0;
int g_video_fd = -1;
bool g_video_initialized = false;
bool g_streaming = false;
std::array<void*, kCameraBufferCount> g_frame_buffers = {};
std::array<size_t, kCameraBufferCount> g_frame_buffer_sizes = {};
uint32_t g_frame_width = 0;
uint32_t g_frame_height = 0;
std::unique_ptr<uint8_t, void (*)(void*)> g_output_buffer(
    nullptr, heap_caps_free);
size_t g_output_buffer_size = 0;
uint32_t g_output_width = 0;
uint32_t g_output_height = 0;
uint32_t g_clear_output_frames_remaining = 0;

size_t AlignUp(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t CameraVideoFormat() {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_CAMERA_TYPE_OV5645)
  return V4L2_PIX_FMT_RGB565;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_SCREEN_PIXEL_FORMAT_RGB888)
  return V4L2_PIX_FMT_RGB24;
#else
  return V4L2_PIX_FMT_RGB565;
#endif
}

ppa_srm_color_mode_t CameraColorMode() {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_CAMERA_TYPE_OV5645)
  return PPA_SRM_COLOR_MODE_RGB565;
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_SCREEN_PIXEL_FORMAT_RGB888)
  return PPA_SRM_COLOR_MODE_RGB888;
#else
  return PPA_SRM_COLOR_MODE_RGB565;
#endif
}

ppa_srm_color_mode_t ScreenColorMode() {
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_SCREEN_PIXEL_FORMAT_RGB888)
  return PPA_SRM_COLOR_MODE_RGB888;
#else
  return PPA_SRM_COLOR_MODE_RGB565;
#endif
}

bool ShowWhiteScreen() {
  const auto& screen = common::GetDriver().screen_info();
  const size_t buffer_size = static_cast<size_t>(screen.width) *
                             screen.height * screen.bits_per_pixel / 8;
  auto buffer = std::unique_ptr<uint8_t, void (*)(void*)>(
      static_cast<uint8_t*>(
          heap_caps_aligned_alloc(16, buffer_size, MALLOC_CAP_SPIRAM)),
      heap_caps_free);
  if (buffer == nullptr) {
    printf("White screen buffer allocation failed\n");
    return false;
  }
  std::memset(buffer.get(), 0xff, buffer_size);
  return common::SendScreen(
      0, 0, screen.width, screen.height, buffer.get());
}

bool RenderCameraFrame(uint8_t* buffer, uint32_t width, uint32_t height) {
  if (buffer == nullptr || g_output_buffer == nullptr || width == 0 ||
      height == 0) {
    return false;
  }

  const float scale =
      std::min(static_cast<float>(g_output_width) / width,
          static_cast<float>(g_output_height) / height);
  const uint32_t scaled_width = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::round(width * scale)));
  const uint32_t scaled_height = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::round(height * scale)));
  const uint32_t output_offset_x = g_output_width > scaled_width
                                       ? (g_output_width - scaled_width) / 2
                                       : 0;
  const uint32_t output_offset_y = g_output_height > scaled_height
                                       ? (g_output_height - scaled_height) / 2
                                       : 0;

  if (g_clear_output_frames_remaining > 0 || output_offset_x > 0 ||
      output_offset_y > 0) {
    std::memset(g_output_buffer.get(), 0xff, g_output_buffer_size);
    if (g_clear_output_frames_remaining > 0) {
      --g_clear_output_frames_remaining;
    }
  }

  ppa_srm_oper_config_t config = {};
  config.in.buffer = buffer;
  config.in.pic_w = width;
  config.in.pic_h = height;
  config.in.block_w = width;
  config.in.block_h = height;
  config.in.block_offset_x = 0;
  config.in.block_offset_y = 0;
  config.in.srm_cm = CameraColorMode();
  config.out.buffer = g_output_buffer.get();
  config.out.buffer_size = g_output_buffer_size;
  config.out.pic_w = g_output_width;
  config.out.pic_h = g_output_height;
  config.out.block_offset_x = output_offset_x;
  config.out.block_offset_y = output_offset_y;
  config.out.srm_cm = ScreenColorMode();
  config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  config.scale_x = scale;
  config.scale_y = scale;
  config.mirror_x = false;
  config.mirror_y = common::IsHi8561Screen();
  config.rgb_swap = false;
  config.byte_swap = false;
  config.mode = PPA_TRANS_MODE_BLOCKING;

  if (ppa_do_scale_rotate_mirror(g_ppa_handle, &config) != ESP_OK) {
    printf("PPA operation failed\n");
    return false;
  }
  if (!common::SendScreen(
          0, 0, g_output_width, g_output_height, g_output_buffer.get())) {
    printf("Screen transfer failed\n");
    return false;
  }
  return true;
}

void DeinitializeCamera() {
  if (g_video_fd >= 0 && g_streaming) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(g_video_fd, VIDIOC_STREAMOFF, &type);
    g_streaming = false;
  }
  for (uint32_t index = 0; index < kCameraBufferCount; ++index) {
    if (g_frame_buffers[index] != nullptr) {
      munmap(g_frame_buffers[index], g_frame_buffer_sizes[index]);
      g_frame_buffers[index] = nullptr;
      g_frame_buffer_sizes[index] = 0;
    }
  }
  if (g_video_fd >= 0) {
    close(g_video_fd);
    g_video_fd = -1;
  }
  g_output_buffer.reset();
  g_output_buffer_size = 0;
  g_output_width = 0;
  g_output_height = 0;
  g_clear_output_frames_remaining = 0;
  if (g_video_initialized) {
    esp_video_deinit();
    g_video_initialized = false;
  }
  if (g_ppa_handle != nullptr) {
    ppa_unregister_client(g_ppa_handle);
    g_ppa_handle = nullptr;
    g_cache_line_size = 0;
  }
}

bool InitializeCamera() {
  const ppa_client_config_t ppa_config = {
      .oper_type = PPA_OPERATION_SRM,
  };
  if (ppa_register_client(&ppa_config, &g_ppa_handle) != ESP_OK ||
      esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &g_cache_line_size) !=
          ESP_OK) {
    printf("PPA SRM init failed\n");
    return false;
  }

  auto& driver = common::GetDriver();
  esp_video_init_csi_config_t csi_config = {};
  csi_config.sccb_config.init_sccb = false;
  csi_config.sccb_config.i2c_handle =
      driver.bus().sgm38121_i2c_bus->bus_handle();
  csi_config.sccb_config.freq = 100000;
  csi_config.reset_pin = GPIO_NUM_NC;
  csi_config.pwdn_pin = GPIO_NUM_NC;
  csi_config.dont_init_ldo = true;

  esp_video_init_config_t camera_config = {};
  camera_config.csi = &csi_config;
  if (esp_video_init(&camera_config) != ESP_OK) {
    printf("esp_video_init failed\n");
    return false;
  }
  g_video_initialized = true;

  g_video_fd = open(kCameraDeviceName, O_RDONLY | O_NONBLOCK);
  if (g_video_fd < 0) {
    printf("Open camera video device failed\n");
    return false;
  }

  v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(g_video_fd, VIDIOC_G_FMT, &format) != 0) {
    printf("VIDIOC_G_FMT failed\n");
    return false;
  }
  format.fmt.pix.pixelformat = CameraVideoFormat();
  if (ioctl(g_video_fd, VIDIOC_S_FMT, &format) != 0) {
    printf("VIDIOC_S_FMT failed\n");
    return false;
  }
  g_frame_width = format.fmt.pix.width;
  g_frame_height = format.fmt.pix.height;

  v4l2_requestbuffers request = {};
  request.count = kCameraBufferCount;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (ioctl(g_video_fd, VIDIOC_REQBUFS, &request) != 0 ||
      request.count < kCameraBufferCount) {
    printf("VIDIOC_REQBUFS failed or returned too few buffers\n");
    return false;
  }

  for (uint32_t index = 0; index < kCameraBufferCount; ++index) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (ioctl(g_video_fd, VIDIOC_QUERYBUF, &buffer) != 0) {
      printf("VIDIOC_QUERYBUF failed\n");
      return false;
    }
    g_frame_buffer_sizes[index] = buffer.length;
    g_frame_buffers[index] = mmap(nullptr, buffer.length,
        PROT_READ | PROT_WRITE, MAP_SHARED, g_video_fd, buffer.m.offset);
    if (g_frame_buffers[index] == MAP_FAILED) {
      g_frame_buffers[index] = nullptr;
      printf("Camera buffer mmap failed\n");
      return false;
    }
    if (ioctl(g_video_fd, VIDIOC_QBUF, &buffer) != 0) {
      printf("VIDIOC_QBUF failed\n");
      return false;
    }
  }

  const auto& screen = driver.screen_info();
  g_output_width = screen.width;
  g_output_height = screen.height;
  const size_t bytes_per_pixel = screen.bits_per_pixel / 8;
  g_output_buffer_size = AlignUp(
      g_output_width * g_output_height * bytes_per_pixel, g_cache_line_size);
  void* output_buffer = heap_caps_aligned_calloc(g_cache_line_size, 1,
      g_output_buffer_size, MALLOC_CAP_SPIRAM);
  if (output_buffer == nullptr) {
    printf("Camera output buffer allocation failed\n");
    return false;
  }
  g_output_buffer.reset(static_cast<uint8_t*>(output_buffer));
  std::memset(g_output_buffer.get(), 0xff, g_output_buffer_size);
  g_clear_output_frames_remaining = kCameraOutputClearFrameCount;

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(g_video_fd, VIDIOC_STREAMON, &type) != 0) {
    printf("VIDIOC_STREAMON failed\n");
    return false;
  }
  g_streaming = true;
  printf("Camera preview started (%lux%lu)\n",
      static_cast<unsigned long>(g_frame_width),
      static_cast<unsigned long>(g_frame_height));
  return true;
}

void RunCameraPreview() {
  while (true) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(g_video_fd, VIDIOC_DQBUF, &buffer) != 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (buffer.index < kCameraBufferCount) {
      RenderCameraFrame(static_cast<uint8_t*>(g_frame_buffers[buffer.index]),
          g_frame_width, g_frame_height);
    }
    if (ioctl(g_video_fd, VIDIOC_QBUF, &buffer) != 0) {
      printf("VIDIOC_QBUF failed while streaming\n");
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kCameraFrameIntervalMs));
  }
}

}  // namespace

extern "C" void app_main(void) {
  printf("Camera screen example on %s\n", common::kBoardName);
  common::InitDriver();
  if (!common::ScreenReady()) {
    printf("Screen init failed\n");
    return;
  }
  if (!ShowWhiteScreen()) {
    printf("Show white screen failed\n");
    return;
  }
  common::StartBacklight();
  if (!InitializeCamera()) {
    printf("Camera init failed\n");
    DeinitializeCamera();
    return;
  }

  RunCameraPreview();
  DeinitializeCamera();
}
