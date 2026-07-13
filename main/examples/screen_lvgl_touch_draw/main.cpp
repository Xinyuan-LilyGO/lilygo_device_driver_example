/*
 * @Description: screen_lvgl_touch_draw
 * @Author: LILYGO_L
 * @Date: 2026-07-10 10:38:17
 * @LastEditTime: 2026-07-13 00:00:00
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

void TouchInput(lv_indev_t*, lv_indev_data_t* data) {
  int x = 0;
  int y = 0;
  if (common::ReadSingleTouch(x, y)) {
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
  common::InitDriver();
  if (!common::TouchReady()) {
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
      common::PrintMultipleTouch();
      next_log_time = esp_log_timestamp() + 1000;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
