/*
 * @Description: 根据当前硬件配置运行对应的电池管理示例
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
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) && defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
constexpr size_t kPageCount = 2;
#else
constexpr size_t kPageCount = 1;
#endif
size_t g_page = 0;
lv_obj_t* g_labels[kPageCount] = {};
bool g_snapshot_active = false;
bool g_startup_saved = false;
bool g_scroll_reset_pending = false;

struct LogBuffers {
  std::string text;
  std::string startup_text;
};

LogBuffers& GetLogBuffers() {
  // 保持进程生命周期，app_main 返回后屏幕仍可显示日志。
  static LogBuffers buffers[kPageCount];
  return buffers[g_page];
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
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) || \
    defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
  ReadTouch(driver.chip().gt9895.get(), data);
#endif
}

void UpdateLogScreen() {
  auto* g_log_label = g_labels[g_page];
  if (g_log_label == nullptr) {
    return;
  }
  g_lvgl_port.Lock();
  auto* screen = lv_obj_get_parent(g_log_label);
  const int32_t scroll_y = lv_obj_get_scroll_y(screen);
  const bool was_at_bottom = lv_obj_get_scroll_bottom(screen) <= 2;
  lv_label_set_text(g_log_label, GetLogBuffers().text.c_str());
  lv_obj_update_layout(screen);
  const int32_t max_scroll_y = LV_MAX(
      0, scroll_y + lv_obj_get_scroll_bottom(screen));
  const bool reset_scroll = g_scroll_reset_pending;
  g_scroll_reset_pending = false;
  const int32_t target_scroll_y = reset_scroll ? 0 : was_at_bottom
      ? max_scroll_y
      : LV_MAX(0, LV_MIN(max_scroll_y, scroll_y));
  lv_obj_scroll_to_y(screen, target_scroll_y, LV_ANIM_OFF);
  g_lvgl_port.Unlock();
}

lv_obj_t* CreateBatteryTile(lv_obj_t* tiles, size_t index) {
  auto* tile = lv_tileview_add_tile(tiles, index, 0,
      index ? LV_DIR_LEFT : LV_DIR_RIGHT);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  auto* title = lv_label_create(tile);
  lv_obj_set_width(title, lv_pct(100));
  lv_obj_set_style_bg_color(title, lv_color_hex(index ? 0x006354 : 0x163e70), 0);
  lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_pad_all(title, 12, 0);
#if LV_FONT_MONTSERRAT_26
  lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
#endif
  lv_label_set_text(title, index ? "2/2   KEYBOARD BATTERY  |  AXP517"
                                 : "1/2   MAINBOARD BATTERY  |  AXP517");
  auto* body = lv_obj_create(tile);
  lv_obj_set_width(body, lv_pct(100));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  return body;
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

  lv_obj_t* tiles = nullptr;
  if constexpr (kPageCount == 2) {
    lv_obj_set_style_pad_all(screen, 0, 0);
    tiles = lv_tileview_create(screen);
    lv_obj_set_size(tiles, lv_pct(100), lv_pct(100));
  }
  for (size_t index = 0; index < kPageCount; ++index) {
    auto* parent = tiles ? CreateBatteryTile(tiles, index) : screen;
    auto* label = g_labels[index] = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_26
    lv_obj_set_style_text_font(label, &lv_font_montserrat_26, LV_PART_MAIN);
#endif
    lv_obj_set_style_text_line_space(label, 4, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(label, index ? "Checking keyboard battery..."
                                   : GetLogBuffers().text.c_str());
  }
  common::StartBacklight();
  if (!g_lvgl_port.Start()) {
    for (auto& label : g_labels) label = nullptr;
    BatteryLogPrintf("LVGL task start failed; continuing serial output\n");
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

void BatteryLogBeginSnapshot(bool keyboard) {
  g_page = keyboard && kPageCount == 2 ? 1 : 0;
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

void BatteryLogResetScroll(bool keyboard) {
  if (!keyboard || kPageCount != 2 || g_labels[1] == nullptr) return;
  g_scroll_reset_pending = true;
  g_lvgl_port.Lock();
  lv_obj_scroll_to_y(lv_obj_get_parent(g_labels[1]), 0, LV_ANIM_OFF);
  g_lvgl_port.Unlock();
}

namespace {

void BatteryManagementTask(void*) {
  BatteryLogPrintf("Battery management example on %s %s\n", common::kBoardName,
      common::GetDriver().device_model_info().version);
  const bool initialized = common::Init();
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
  vTaskDelete(nullptr);
}

}  // namespace

extern "C" void app_main() {
  if (xTaskCreate(BatteryManagementTask, "battery_management", 16 * 1024,
          nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    std::printf("Battery management task creation failed\n");
  }
}
