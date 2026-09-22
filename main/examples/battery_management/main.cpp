/*
 * @Description: 根据当前硬件配置运行对应的电池管理示例
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-22 17:03:41
 * @License: GPL 3.0
 */
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "battery_management.h"
#include "display/lvgl.h"

namespace {

// 静态生命周期保证芯片初始化失败、app_main 返回后 LVGL 任务仍可显示错误。
common::LvglPort g_lvgl_port;
lv_obj_t* g_log_label = nullptr;
bool g_snapshot_active = false;
bool g_startup_saved = false;

struct LogBuffers {
  std::string text;
  std::string startup_text;
};

LogBuffers& GetLogBuffers() {
  // 保持进程生命周期，app_main 返回后屏幕仍可显示日志。
  static LogBuffers* const buffers = new LogBuffers;
  return *buffers;
}

template <typename Touch>
bool ReadTouch(Touch* touch, lv_indev_data_t* data) {
  cpp_bus_driver::TouchFrame frame;
  if (touch == nullptr ||
      touch->ReadPrimaryTouch(&frame) !=
          cpp_bus_driver::TouchReadStatus::kSuccess ||
      frame.contact_count == 0) {
    return false;
  }
  data->state = LV_INDEV_STATE_PRESSED;
  data->point.x = frame.contacts[0].x;
  data->point.y = frame.contacts[0].y;
  return true;
}

void TouchInput(lv_indev_t*, lv_indev_data_t* data) {
  auto& driver = common::GetDriver();
  data->state = LV_INDEV_STATE_RELEASED;
  if (!driver.IsTouchReady()) {
    return;
  }
  if (common::IsHi8561Screen()) {
    ReadTouch(driver.chip().hi8561_touch.get(), data);
    return;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  ReadTouch(driver.chip().gt9895.get(), data);
#endif
}

void UpdateLogScreen() {
  if (g_log_label == nullptr) {
    return;
  }
  g_lvgl_port.Lock();
  auto* screen = lv_display_get_screen_active(g_lvgl_port.display());
  const int32_t scroll_y = lv_obj_get_scroll_y(screen);
  lv_label_set_text(g_log_label, GetLogBuffers().text.c_str());
  lv_obj_update_layout(screen);
  // 更新数据时不跳回顶部，便于持续查看屏幕下方的分类。
  lv_obj_scroll_to_y(screen, scroll_y, LV_ANIM_OFF);
  g_lvgl_port.Unlock();
}

void InitLogScreen() {
  auto& driver = common::GetDriver();
  // 完整初始化已经负责初始化屏幕、触摸和背光，这里只检查结果，避免重复复位屏幕。
  if (!common::IsScreenReady()) {
    BatteryLogPrintf("Screen is not ready; continuing serial output\n");
    return;
  }
  if (!driver.IsTouchReady()) {
    BatteryLogPrintf("Touch init failed; touch scrolling unavailable\n");
  }
  if (!driver.IsScreenBacklightReady()) {
    BatteryLogPrintf("Screen backlight init failed\n");
  }
  if (!g_lvgl_port.Init(TouchInput)) {
    BatteryLogPrintf("LVGL init failed; continuing serial output\n");
    return;
  }

  auto* screen = lv_display_get_screen_active(g_lvgl_port.display());
  lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 16, LV_PART_MAIN);
  lv_obj_set_scroll_dir(screen, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(screen, 6, LV_PART_SCROLLBAR);

  g_log_label = lv_label_create(screen);
  lv_obj_set_width(g_log_label, lv_pct(100));
  lv_label_set_long_mode(g_log_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(g_log_label, lv_color_black(), LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_26
  lv_obj_set_style_text_font(g_log_label, &lv_font_montserrat_26, LV_PART_MAIN);
#endif
  lv_obj_set_style_text_line_space(g_log_label, 4, LV_PART_MAIN);
  lv_obj_align(g_log_label, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_label_set_text(g_log_label, GetLogBuffers().text.c_str());
  common::StartBacklight();
  lv_refr_now(g_lvgl_port.display());
  if (!g_lvgl_port.Start()) {
    BatteryLogPrintf("LVGL task start failed; continuing serial output\n");
    lv_refr_now(g_lvgl_port.display());
    g_log_label = nullptr;
  }
}

}  // namespace

void BatteryLogPrintf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list screen_args;
  va_copy(screen_args, args);
  vprintf(format, args);
  va_end(args);

  va_list size_args;
  va_copy(size_args, screen_args);
  const int length = vsnprintf(nullptr, 0, format, size_args);
  va_end(size_args);
  if (length > 0) {
    std::string line(static_cast<size_t>(length) + 1, '\0');
    vsnprintf(&line[0], line.size(), format, screen_args);
    GetLogBuffers().text.append(line.data(), static_cast<size_t>(length));
  }
  va_end(screen_args);
  if (!g_snapshot_active) {
    UpdateLogScreen();
  }
}

void BatteryLogBeginSnapshot() {
  if (!g_startup_saved) {
    GetLogBuffers().startup_text = GetLogBuffers().text;
    g_startup_saved = true;
  }
  GetLogBuffers().text = GetLogBuffers().startup_text;
  g_snapshot_active = true;
}

void BatteryLogEndSnapshot() {
  g_snapshot_active = false;
  UpdateLogScreen();
}

extern "C" void app_main() {
  BatteryLogPrintf("Battery management example on %s %s\n", common::kBoardName,
      common::GetDriver().device_model_info().version);
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  const bool initialized = common::InitDriver(kExternalChargeCurrentMa);
#else
  const bool initialized = common::InitDriver();
#endif
  if (!initialized) {
    BatteryLogPrintf(
        "Device driver initialization completed with errors; continuing "
        "example\n");
  }
  InitLogScreen();

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
  RunBq27220Example();
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR) || \
    (defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) &&      \
        defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2))
  RunAxp517Example();
#endif
}
