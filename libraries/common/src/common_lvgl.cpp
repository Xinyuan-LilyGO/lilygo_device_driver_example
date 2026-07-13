/*
 * @Description: LVGL port shared by the display examples
 * @Author: LILYGO_L
 * @Date: 2026-07-13 00:00:00
 * @LastEditTime: 2026-07-13 00:00:00
 * @License: GPL 3.0
 */
#include "common_lvgl.h"

#include <algorithm>

#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace common {

/**
 * @brief 初始化 LVGL 显示、输入设备和 tick 定时器
 * @param input_callback LVGL 指针输入读取回调
 * @return 初始化成功返回 true，否则返回 false
 */
bool LvglPort::Init(lv_indev_read_cb_t input_callback) {
  if (input_callback == nullptr || !ScreenReady()) {
    return false;
  }

  lv_init();
  const auto& screen = GetDriver().screen_info();
  display_ = lv_display_create(screen.width, screen.height);
  if (display_ == nullptr) {
    return false;
  }

  lv_display_set_user_data(display_, this);
  lv_display_set_color_format(display_, color_format());
  lv_display_set_physical_resolution(display_, screen.width, screen.height);

  draw_buffer_ = heap_caps_malloc(DrawBufferSize(), MALLOC_CAP_SPIRAM);
  if (draw_buffer_ == nullptr) {
    printf("LVGL draw buffer allocation failed\n");
    return false;
  }
  lv_display_set_buffers(display_, draw_buffer_, nullptr, DrawBufferSize(),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display_, FlushCallback);

  input_ = lv_indev_create();
  if (input_ == nullptr) {
    return false;
  }
  lv_indev_set_type(input_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(input_, this);
  lv_indev_set_read_cb(input_, input_callback);
  lv_indev_set_display(input_, display_);

  if (!RegisterFlushReadyCallback()) {
    return false;
  }

  const esp_timer_create_args_t tick_timer_args = {
      .callback = TickCallback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = false,
  };
  if (esp_timer_create(&tick_timer_args, &tick_timer_) != ESP_OK ||
      esp_timer_start_periodic(tick_timer_, kTickPeriodMs * 1000) != ESP_OK) {
    printf("LVGL tick timer init failed\n");
    return false;
  }
  return true;
}

/**
 * @brief 启动 LVGL 处理任务
 * @return 任务启动成功返回 true，否则返回 false
 */
bool LvglPort::Start() {
  return xTaskCreate(TaskEntry, "lvgl", kTaskStackBytes, this, kTaskPriority,
             nullptr) == pdPASS;
}

/**
 * @brief 锁定 LVGL API 访问
 */
void LvglPort::Lock() { _lock_acquire(&lock_); }

/**
 * @brief 解锁 LVGL API 访问
 */
void LvglPort::Unlock() { _lock_release(&lock_); }

/**
 * @brief 获取当前屏幕对应的 LVGL 颜色格式
 * @return LVGL 颜色格式
 */
lv_color_format_t LvglPort::color_format() const {
  switch (GetDriver().screen_info().bits_per_pixel) {
    case 24:
      return LV_COLOR_FORMAT_RGB888;
    case 16:
    default:
      return LV_COLOR_FORMAT_RGB565;
  }
}

/**
 * @brief 处理 LVGL 显示刷新请求
 * @param display LVGL 显示对象
 * @param area 待刷新的屏幕区域
 * @param pixels 像素数据地址
 */
void LvglPort::FlushCallback(
    lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
  auto* port = static_cast<LvglPort*>(lv_display_get_user_data(display));
  if (port == nullptr ||
      !SendScreen(
          area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels)) {
    lv_display_flush_ready(display);
  }
}

/**
 * @brief 增加 LVGL tick
 * @param context 回调上下文，本回调不使用该参数
 */
void LvglPort::TickCallback(void*) { lv_tick_inc(kTickPeriodMs); }

/**
 * @brief 进入 LVGL 任务循环
 * @param context LvglPort 对象指针
 */
void LvglPort::TaskEntry(void* context) {
  static_cast<LvglPort*>(context)->TaskLoop();
}

/**
 * @brief 注册屏幕传输完成回调
 * @return 注册成功返回 true，否则返回 false
 */
bool LvglPort::RegisterFlushReadyCallback() {
  const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
      .on_color_trans_done = [](esp_lcd_panel_handle_t,
                                 esp_lcd_dpi_panel_event_data_t*,
                                 void* context) -> bool {
        auto* port = static_cast<LvglPort*>(context);
        if (port != nullptr && port->display_ != nullptr) {
          lv_display_flush_ready(port->display_);
        }
        return false;
      },
      .on_refresh_done = [](esp_lcd_panel_handle_t,
                             esp_lcd_dpi_panel_event_data_t*,
                             void*) -> bool { return false; },
  };
  return esp_lcd_dpi_panel_register_event_callbacks(
             GetDriver().bus().screen_mipi_bus->device_handle(), &callbacks,
             this) == ESP_OK;
}

/**
 * @brief 获取 LVGL 绘制缓冲区大小
 * @return 绘制缓冲区字节数
 */
size_t LvglPort::DrawBufferSize() const {
  const auto& screen = GetDriver().screen_info();
  return static_cast<size_t>(screen.width) * screen.height *
         screen.bits_per_pixel / 8;
}

/**
 * @brief 运行 LVGL handler 任务循环
 */
void LvglPort::TaskLoop() {
  while (true) {
    Lock();
    uint32_t delay_ms = lv_timer_handler();
    Unlock();
    delay_ms = std::max(delay_ms, kMinimumHandlerDelayMs);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

}  // namespace common
