/*
 * @Description: screen_lvgl
 * @Author: LILYGO_L
 * @Date: 2026-07-10 10:38:17
 * @LastEditTime: 2026-07-13 14:56:42
 * @License: GPL 3.0
 */
#include <algorithm>
#include <array>

#include "common_lvgl.h"
#include "lvgl.h"

namespace {

extern "C" void example_lvgl_demo_ui(lv_display_t* display);

void ReleasedInput(lv_indev_t*, lv_indev_data_t* data) {
  data->state = LV_INDEV_STATE_RELEASED;
}

uint16_t Rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
}

void FillColorBars(void* buffer, int width, int height, int bits_per_pixel) {
  constexpr std::array<std::array<uint8_t, 3>, 8> kColors = {{
      {0xff, 0x00, 0x00},
      {0xff, 0x7f, 0x00},
      {0xff, 0xff, 0x00},
      {0x00, 0xff, 0x00},
      {0x00, 0xff, 0xff},
      {0x00, 0x00, 0xff},
      {0x80, 0x00, 0xff},
      {0xff, 0xff, 0xff},
  }};
  const int block_height =
      std::max(1, height / static_cast<int>(kColors.size()));

  for (int y = 0; y < height; ++y) {
    const auto& color = kColors[std::min(
        static_cast<int>(kColors.size()) - 1, y / block_height)];
    for (int x = 0; x < width; ++x) {
      const size_t pixel = static_cast<size_t>(y) * width + x;
      if (bits_per_pixel == 16) {
        static_cast<uint16_t*>(buffer)[pixel] =
            Rgb565(color[0], color[1], color[2]);
      } else {
        auto* bytes = static_cast<uint8_t*>(buffer) + pixel * 3;
        bytes[0] = color[0];
        bytes[1] = color[1];
        bytes[2] = color[2];
      }
    }
  }
}

}  // namespace

extern "C" void app_main(void) {
  printf("LVGL screen example on %s\n", common::kBoardName);
  common::InitDriver();
  if (!common::ScreenReady()) {
    printf("Screen init failed\n");
    return;
  }

  const auto& info = common::GetDriver().screen_info();
  const size_t buffer_size =
      static_cast<size_t>(info.width) * info.height * info.bits_per_pixel / 8;
  void* color_buffer =
      heap_caps_aligned_calloc(16, 1, buffer_size, MALLOC_CAP_SPIRAM);
  if (color_buffer != nullptr) {
    FillColorBars(color_buffer, info.width, info.height, info.bits_per_pixel);
    common::SendScreen(0, 0, info.width, info.height, color_buffer);
    heap_caps_free(color_buffer);
  }
  common::StartBacklight();

  vTaskDelay(pdMS_TO_TICKS(5000));

  common::LvglPort lvgl_port;
  if (!lvgl_port.Init(ReleasedInput)) {
    printf("LVGL init failed\n");
    return;
  }
  example_lvgl_demo_ui(lvgl_port.display());
  if (!lvgl_port.Start()) {
    printf("LVGL task start failed\n");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(1000));
  common::RunScreenEffects();
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
