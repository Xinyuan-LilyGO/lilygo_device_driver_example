/*
 * @Description: 使用 LVGL 读取触摸输入并在屏幕上绘图的示例
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-07-28 14:05:30
 * @License: GPL 3.0
 */
#include <ctime>
#include <vector>

#include "common_lvgl.h"
#include "lvgl.h"

namespace {

std::vector<lv_point_t> g_points;
lv_obj_t* g_canvas = nullptr;
lv_layer_t g_layer;
time_t g_last_touch_time = 0;
bool g_needs_clear = false;

bool TouchReady() {
  auto& driver = common::GetDriver();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return driver.IsScreenReady() && driver.IsTouchReady();
#else
  return driver.IsScreenReady() && driver.IsHi8561TouchReady();
#endif
}

template <typename Touch>
bool ReadSingleTouchFrom(Touch* touch, int& x, int& y) {
  cpp_bus_driver::TouchFrame frame;
  if (touch == nullptr ||
      touch->ReadPrimaryTouch(&frame) !=
          cpp_bus_driver::TouchReadStatus::kSuccess ||
      frame.contact_count == 0) {
    return false;
  }
  x = frame.contacts[0].x;
  y = frame.contacts[0].y;
  return true;
}

bool ReadSingleTouch(int& x, int& y) {
  auto& driver = common::GetDriver();
  if (common::IsHi8561Screen()) {
    return ReadSingleTouchFrom(driver.chip().hi8561_touch.get(), x, y);
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  return ReadSingleTouchFrom(driver.chip().gt9895.get(), x, y);
#else
  return false;
#endif
}

template <typename Touch>
void PrintMultipleTouchFrom(Touch* touch) {
  cpp_bus_driver::TouchFrame frame;
  if (touch == nullptr ||
      touch->ReadTouchFrame(&frame) !=
          cpp_bus_driver::TouchReadStatus::kSuccess) {
    return;
  }
  printf("Touch finger: %u edge touch flag: %u\n",
      static_cast<unsigned int>(frame.contact_count),
      static_cast<unsigned int>(frame.edge_touch));
  for (size_t i = 0; i < frame.contact_count; ++i) {
    printf("Touch num:[%u] x: %u y: %u p: %u\n",
        static_cast<unsigned int>(i + 1),
        static_cast<unsigned int>(frame.contacts[i].x),
        static_cast<unsigned int>(frame.contacts[i].y),
        static_cast<unsigned int>(frame.contacts[i].pressure));
  }
}

void PrintMultipleTouch() {
  auto& driver = common::GetDriver();
  if (common::IsHi8561Screen()) {
    PrintMultipleTouchFrom(driver.chip().hi8561_touch.get());
    return;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  PrintMultipleTouchFrom(driver.chip().gt9895.get());
#endif
}

void TouchInput(lv_indev_t*, lv_indev_data_t* data) {
  int x = 0;
  int y = 0;
  if (ReadSingleTouch(x, y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void DrawPoint(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSING) {
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);
    g_points.push_back(point);
    if (g_points.size() >= 2) {
      lv_draw_line_dsc_t line;
      lv_draw_line_dsc_init(&line);
      line.color = lv_palette_main(LV_PALETTE_RED);
      line.width = 4;
      line.round_start = 1;
      line.round_end = 1;
      line.p1 = lv_point_to_precise(&g_points[0]);
      line.p2 = lv_point_to_precise(&g_points[1]);
      lv_draw_line(&g_layer, &line);
      lv_canvas_finish_layer(g_canvas, &g_layer);
      g_points.erase(g_points.begin());
    }
    g_last_touch_time = time(nullptr);
    g_needs_clear = true;
  } else if (code == LV_EVENT_RELEASED) {
    g_points.clear();
  }
}

void CreateCanvas(const common::LvglPort& lvgl_port) {
  const auto& info = common::GetDriver().screen_info();
  const size_t size =
      static_cast<size_t>(info.width) * info.height * info.bits_per_pixel / 8;
  void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  assert(buffer != nullptr);
  g_canvas = lv_canvas_create(lv_screen_active());
  lv_canvas_set_buffer(
      g_canvas, buffer, info.width, info.height, lvgl_port.color_format());
  lv_canvas_fill_bg(g_canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
  lv_obj_center(g_canvas);
  lv_canvas_init_layer(g_canvas, &g_layer);
  lv_obj_add_event_cb(lv_screen_active(), DrawPoint, LV_EVENT_ALL, nullptr);
}

void ClearCanvasTimer(lv_timer_t*) {
  if (g_needs_clear && time(nullptr) - g_last_touch_time > 5) {
    lv_canvas_fill_bg(g_canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
    g_points.clear();
    g_needs_clear = false;
  }
}

}  // namespace

extern "C" void app_main(void) {
  printf("LVGL touch drawing example on %s\n", common::kBoardName);
  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }
  if (!TouchReady()) {
    printf("Screen or touch init failed\n");
    return;
  }

  common::LvglPort lvgl_port;
  if (!lvgl_port.Init(TouchInput)) {
    printf("LVGL init failed\n");
    return;
  }
  CreateCanvas(lvgl_port);
  lv_timer_create(ClearCanvasTimer, 1000, nullptr);
  common::StartBacklight();
  if (!lvgl_port.Start()) {
    printf("LVGL task start failed\n");
    return;
  }

  uint32_t next_log_time = 0;
  while (true) {
    if (esp_log_timestamp() >= next_log_time) {
      PrintMultipleTouch();
      next_log_time = esp_log_timestamp() + 1000;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
