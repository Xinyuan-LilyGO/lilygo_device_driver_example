/*
 * @Description: T-Display-P4 V2 外接 AW93307 的独立触摸按键及三电极滑动调试
 * @Author: LILYGO_L
 * @Date: 2026-10-30 00:00:00
 * @LastEditTime: 2026-11-03 11:29:07
 * @License: GPL 3.0
 */
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "bus/i2c/software_i2c.h"
#include "chip/i2c/aw933xx.h"
#include "display/lvgl.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#if !defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4) || \
    !defined(CONFIG_LILYGO_DEVICE_DRIVER_DEVICE_VERSION_V2)
#error "screen_lvgl_aw933xx requires T-Display-P4 V2"
#endif

namespace {

using Sensor = cpp_bus_driver::Aw933xx;
// CN10: 2=NFC_IRQ(GPIO27), 4=NFC_CS(GPIO28), 6=CC_GDO2(GPIO29)。
// SDA/SCL 和 INTN 均通过 1.8V/3.3V 电平转换连接，不连接键盘或 RF 扩展板。
constexpr int kSda = 27;
constexpr int kScl = 28;
constexpr int kInterrupt = 29;
constexpr uint16_t kEnabledChannels = 0x0F;
// 提高硬件及软件触摸阈值以降低灵敏度；软件差值保持原始 Q10 单位。
constexpr uint32_t kSliderTouchThreshold = 2000000;
constexpr uint32_t kSliderPositionThreshold = 50000;
constexpr uint32_t kButtonTouchThreshold = 800000;
constexpr uint32_t kSignalTouchThreshold = 320000000;
constexpr uint32_t kSignalReleaseThreshold = kSignalTouchThreshold / 2;
constexpr uint32_t kSliderSignalTouchThreshold = 800000000;
constexpr uint32_t kSliderSignalReleaseThreshold =
    kSliderSignalTouchThreshold / 2;
constexpr uint8_t kSignalDebounceSamples = 3;
constexpr uint8_t kButtonChannel = 0;
constexpr uint8_t kSliderFirstChannel = 1;
constexpr uint8_t kSliderChannelCount = 3;
// CH0 为 CS1 按键；CH1/2/3 分别为滑条 CS0/3/4。
constexpr std::array<uint8_t, 4> kElectrodes = {1, 0, 3, 4};

constexpr int64_t kGestureHighlightMs = 2500;
constexpr uint32_t kInkColor = 0x202B3C;
constexpr uint32_t kMutedColor = 0x66758A;
constexpr uint32_t kAccentColor = 0x1869C9;
constexpr uint32_t kLineColor = 0xDCE4EE;
constexpr int32_t kNavigationStepPercent = 12;
constexpr std::array<const char*, 5> kApps = {
    "Music", "Volume", "Brightness", "Bluetooth", "Settings"};
constexpr std::array<const char*, 5> kAppSymbols = {LV_SYMBOL_AUDIO,
    LV_SYMBOL_VOLUME_MAX, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_BLUETOOTH,
    LV_SYMBOL_SETTINGS};
constexpr std::array<uint32_t, 5> kAppColors = {
    0x216AC9, 0x168578, 0xBA6B29, 0x6052BA, 0x57677A};
constexpr size_t kAppListItemCount = 5;

struct GestureView {
  lv_obj_t* state = nullptr;
  lv_obj_t* gesture = nullptr;
  lv_obj_t* history = nullptr;
};

struct GestureHistory {
  Sensor::SliderEvent event{};
  int64_t time_ms = -1;
};

struct NavigationState {
  size_t selected = 0;
  size_t list_selected = 0;
  bool in_app = false;
  int32_t anchor = 0;
  int32_t pointer_start_x = 0;
  bool tracking = false;
};

struct Ui {
  lv_obj_t* status = nullptr;
  GestureView button;
  GestureView slider;
  lv_obj_t* position = nullptr;
  lv_obj_t* bar = nullptr;
  lv_obj_t* navigation_title = nullptr;
  lv_obj_t* navigation_hint = nullptr;
  lv_obj_t* app_tileview = nullptr;
  lv_obj_t* app_list = nullptr;
  std::array<lv_obj_t*, kAppListItemCount> app_rows{};
};

// 主循环持有 LvglPort 锁时更新，输入回调由同一 LVGL 任务读取。
lv_indev_data_t slider_pointer{};

/**
 * @brief 将外置电容滑条转换为 LVGL 指针输入，不接入屏幕触摸总线。
 * @param data 输出虚拟指针坐标和按下、释放状态。
 */
void ReadSliderPointer(lv_indev_t*, lv_indev_data_t* data) {
  data->point = slider_pointer.point;
  data->state = slider_pointer.state;
}

/**
 * @brief 创建白底、深色文字的调试标签。
 * @param parent 标签父对象。
 * @param text 初始文本。
 * @return 创建的标签对象。
 */
lv_obj_t* AddLabel(lv_obj_t* parent, const char* text) {
  auto* label = lv_label_create(parent);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_font(label, &lv_font_montserrat_26, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, text);
  return label;
}

/**
 * @brief 创建独立的交互区域，统一使用页面继承的字体和间距。
 * @param parent 页面根对象。
 * @param title 电极区域名称。
 * @param view 输出状态、当前手势与历史手势控件。
 * @return 区域容器，用于追加滑条位置控件。
 */
lv_obj_t* CreateInteractionArea(
    lv_obj_t* parent, const char* title, GestureView* view) {
  auto* area = lv_obj_create(parent);
  lv_obj_set_width(area, LV_PCT(100));
  lv_obj_set_height(area, LV_SIZE_CONTENT);
  lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(area, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(area, lv_color_hex(kLineColor), 0);
  lv_obj_set_style_border_width(area, 1, 0);
  lv_obj_set_style_radius(area, 12, 0);
  lv_obj_set_style_pad_all(area, 18, 0);
  lv_obj_set_style_pad_row(area, 10, 0);
  lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
  auto* heading = AddLabel(area, title);
  lv_obj_set_style_text_color(heading, lv_color_hex(kAccentColor), 0);
  view->state = AddLabel(area, "State: waiting");
  view->gesture = AddLabel(area, "Gesture: waiting");
  view->history = AddLabel(area, "Last gesture: none");
  lv_obj_set_style_text_color(view->history, lv_color_hex(kMutedColor), 0);
  return area;
}

/**
 * @brief 根据图标距视口中心的距离平滑缩放，统一左右预览的大小和透明度。
 * @param tileview 含五个独立应用分页的控件，须持有 LVGL 锁。
 */
void UpdateAppIconFocus(lv_obj_t* tileview) {
  const int32_t step = lv_obj_get_content_width(tileview);
  if (step <= 0) {
    return;
  }
  const int32_t scroll = lv_obj_get_scroll_x(tileview);
  for (size_t app = 0; app < kApps.size(); ++app) {
    auto* tile = lv_obj_get_child(tileview, static_cast<int32_t>(app));
    auto* icon = lv_obj_get_child(tile, 0);
    auto* name = lv_obj_get_child(tile, 1);
    const int32_t offset = static_cast<int32_t>(app) * step - scroll;
    const int32_t distance = std::min(step, offset < 0 ? -offset : offset);
    // 中心图标 104 px，左右预览约 73 px；使用变换避免布局和滚动范围跳动。
    const int32_t scale = 256 - distance * 76 / step;
    lv_obj_set_style_transform_scale_x(icon, scale, 0);
    lv_obj_set_style_transform_scale_y(icon, scale, 0);
    lv_obj_set_style_opa(
        icon, static_cast<lv_opa_t>(LV_OPA_COVER - distance * 120 / step), 0);
    lv_obj_set_style_text_color(
        name, lv_color_hex(distance < step / 2 ? kInkColor : kMutedColor), 0);
  }
}

/**
 * @brief 拖动和吸附动画期间持续更新图标主次效果。
 * @param event Tileview 的滚动事件。
 */
void AppIconsScroll(lv_event_t* event) {
  UpdateAppIconFocus(lv_event_get_current_target_obj(event));
}

/**
 * @brief 创建五个应用的独立图标分页和应用内列表，页面切换由电容按键控制。
 * @param parent 白底导航区域，宽度由父布局确定。
 * @param ui 接收 Tileview、列表及提示控件。
 */
void CreateAppNavigation(lv_obj_t* parent, Ui* ui) {
  ui->navigation_title = AddLabel(parent, "Applications / Music");
  lv_obj_set_style_text_align(ui->navigation_title, LV_TEXT_ALIGN_CENTER, 0);
  auto* content = lv_obj_create(parent);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_height(content, 0);
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_min_height(content, 190, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  ui->app_tileview = lv_tileview_create(content);
  lv_obj_set_size(ui->app_tileview, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(ui->app_tileview, 0, 0);
  lv_obj_set_style_border_width(ui->app_tileview, 0, 0);
  lv_obj_set_style_bg_color(ui->app_tileview, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_scrollbar_mode(ui->app_tileview, LV_SCROLLBAR_MODE_OFF);
  // 两侧留出预览区域，每个真实应用只占一个 tile，避免跨页重复图标。
  lv_obj_update_layout(ui->app_tileview);
  const int32_t side_padding = lv_obj_get_width(ui->app_tileview) / 3;
  lv_obj_set_style_pad_left(ui->app_tileview, side_padding, 0);
  lv_obj_set_style_pad_right(ui->app_tileview, side_padding, 0);
  for (size_t page = 0; page < kApps.size(); ++page) {
    auto* tile = lv_tileview_add_tile(
        ui->app_tileview, static_cast<uint8_t>(page), 0, LV_DIR_HOR);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 14, 0);
    lv_obj_set_flex_align(
        tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto* icon = lv_obj_create(tile);
    lv_obj_set_size(icon, 104, 104);
    lv_obj_set_style_transform_pivot_x(icon, 52, 0);
    lv_obj_set_style_transform_pivot_y(icon, 52, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(icon, 26, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(kAppColors[page]), 0);
    auto* symbol = lv_label_create(icon);
    lv_label_set_text(symbol, kAppSymbols[page]);
    lv_obj_set_style_text_color(symbol, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(symbol);
    auto* name = AddLabel(tile, kApps[page]);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_add_event_cb(
      ui->app_tileview, AppIconsScroll, LV_EVENT_SCROLL, nullptr);
  lv_tileview_set_tile_by_index(ui->app_tileview, 0, 0, LV_ANIM_OFF);
  UpdateAppIconFocus(ui->app_tileview);
  ui->app_list = lv_list_create(content);
  lv_obj_set_size(ui->app_list, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(ui->app_list, 0, 0);
  lv_obj_set_style_pad_row(ui->app_list, 6, 0);
  lv_obj_set_style_border_width(ui->app_list, 0, 0);
  lv_obj_set_style_bg_color(ui->app_list, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_scroll_dir(ui->app_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(ui->app_list, LV_SCROLLBAR_MODE_AUTO);
  for (size_t item = 0; item < kAppListItemCount; ++item) {
    char text[40];
    snprintf(
        text, sizeof(text), "%s %u", kApps[0], static_cast<unsigned>(item + 1));
    auto* row = lv_list_add_button(ui->app_list, nullptr, text);
    lv_obj_set_style_min_height(row, 56, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    ui->app_rows[item] = row;
  }
  lv_obj_add_flag(ui->app_list, LV_OBJ_FLAG_HIDDEN);
  ui->navigation_hint = AddLabel(parent, "Slide to switch / Click to enter");
  lv_obj_set_style_text_align(ui->navigation_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(
      ui->navigation_hint, lv_color_hex(kMutedColor), 0);
}

/**
 * @brief 创建白底交互页面，展示独立手势区域及应用图标和列表，字体统一。
 * @return 页面控件引用。
 */
Ui CreateUi() {
  auto* root = lv_screen_active();
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_color(root, lv_color_hex(kInkColor), 0);
  lv_obj_set_style_text_font(root, &lv_font_montserrat_26, 0);
  lv_obj_set_style_pad_all(root, 24, 0);
  lv_obj_set_style_pad_row(root, 16, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  AddLabel(root, "AW93307 touch test");
  Ui ui;
  ui.status = AddLabel(root, "Loading sensor...");
  lv_obj_set_style_text_color(ui.status, lv_color_hex(kMutedColor), 0);
  CreateInteractionArea(root, "Touch button / CS1", &ui.button);
  auto* slider_area =
      CreateInteractionArea(root, "Slider / CS0 - CS3 - CS4", &ui.slider);
  ui.position = AddLabel(slider_area, "Signal position: --");
  ui.bar = lv_bar_create(slider_area);
  lv_obj_set_size(ui.bar, LV_PCT(100), 20);
  lv_bar_set_range(ui.bar, 0, 100);
  lv_obj_set_style_bg_color(ui.bar, lv_color_hex(0xE8EEF6), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      ui.bar, lv_color_hex(kAccentColor), LV_PART_INDICATOR);
  auto* preview = lv_obj_create(root);
  lv_obj_set_width(preview, LV_PCT(100));
  lv_obj_set_height(preview, 0);
  lv_obj_set_flex_grow(preview, 1);
  lv_obj_set_style_min_height(preview, 330, 0);
  lv_obj_remove_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(preview, 18, 0);
  lv_obj_set_style_pad_row(preview, 10, 0);
  lv_obj_set_style_bg_color(preview, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(preview, lv_color_hex(kLineColor), 0);
  lv_obj_set_style_border_width(preview, 1, 0);
  lv_obj_set_style_radius(preview, 12, 0);
  lv_obj_set_flex_flow(preview, LV_FLEX_FLOW_COLUMN);
  CreateAppNavigation(preview, &ui);
  AddLabel(root, "BOOT: calibrate with fingers away");
  return ui;
}

/**
 * @brief 空载校准滑条及独立按键的四个通道，等待寄生电容校准稳定。
 * @param sensor 已启用四通道扫描的传感器。
 * @return 四通道校准命令发送成功并完成等待返回 true，否则返回 false。
 */
bool CalibrateSensor(Sensor* sensor) {
  if (!sensor->Calibrate(kEnabledChannels)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(500));
  return true;
}

/**
 * @brief 配置三电极滑条与 CS1 独立触摸按键，保持佩戴算法关闭。
 * @param sensor 已加载固件并处于睡眠模式的传感器。
 * @return 所有配置、启动及空载校准成功返回 true。
 */
bool ConfigureSensor(Sensor* sensor) {
  uint32_t id;
  if (!sensor->GetChipId(&id) || id != 0xA9330710 ||
      !sensor->SetInterruptPin(Sensor::InterruptPin::kCs2)) {
    return false;
  }
  for (uint8_t channel = 0; channel < kElectrodes.size(); ++channel) {
    std::array<Sensor::ElectrodeMode, Sensor::kChannelCount> electrodes{};
    for (uint8_t cs = 0; cs < Sensor::kChannelCount; ++cs) {
      if ((sensor->electrode_mask() & (1U << cs)) != 0 && cs != 2 && cs != 5) {
        electrodes[cs] = Sensor::ElectrodeMode::kGround;
      }
    }
    electrodes[kElectrodes[channel]] = Sensor::ElectrodeMode::kMeasure;
    if (!sensor->ConfigureElectrodes(channel, electrodes)) {
      return false;
    }
  }
  Sensor::WidgetConfig slider;
  slider.first_channel = kSliderFirstChannel;
  slider.x_channels = kSliderChannelCount;
  slider.touch_threshold = kSliderTouchThreshold;
  slider.position_threshold = kSliderPositionThreshold;
  slider.scan_period_ms = 20;
  slider.click_interval_ms = 500;
  Sensor::WidgetConfig button;
  button.widget = Sensor::Widget::kButton;
  button.first_channel = kButtonChannel;
  button.x_channels = 1;
  button.touch_threshold = kButtonTouchThreshold;
  button.position_threshold = kButtonTouchThreshold;
  button.scan_period_ms = 20;
  button.click_interval_ms = 500;
  button.long_press_ms = 2000;
  button.longer_press_ms = 3000;
  button.super_long_press_ms = 4000;
  if (!sensor->ConfigureWidget(0, slider) ||
      !sensor->ConfigureWidget(1, button) ||
      !sensor->ConfigureScan(kEnabledChannels, 20) ||
      !sensor->SetInterruptMask(0x8007F) ||
      !sensor->SetOperatingMode(Sensor::OperatingMode::kActive)) {
    return false;
  }
  if (!CalibrateSensor(sensor)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  Sensor::Events discarded;
  return sensor->ReadEvents(&discarded);
}

/**
 * @brief 获取滑动电极正差值加权位置，仅用于观察电极响应，不代替芯片事件。
 * @param channels 四通道采样数据。
 * @param threshold 本次触摸判定阈值，使用芯片原始单位。
 * @param position 输出 0~100 的调试位置，未达到阈值时保持不变。
 * @return 滑动电极存在超过指定阈值的信号返回 true。
 */
bool GetSignalPosition(const std::array<Sensor::ChannelData, 4>& channels,
    uint32_t threshold, int32_t* position) {
  int64_t total = 0;
  int64_t weighted = 0;
  bool touched = false;
  for (size_t i = 0; i < kSliderChannelCount; ++i) {
    const int64_t signal =
        std::max<int32_t>(0, channels[kSliderFirstChannel + i].difference);
    touched |= signal > threshold;
    total += signal;
    weighted += signal * i * 50;
  }
  if (!touched || total == 0) {
    return false;
  }
  *position = static_cast<int32_t>(weighted / total);
  return true;
}

/**
 * @brief 将芯片连击次数转换为调试页中的手势名称。
 * @param clicks 芯片报告的连击次数，0 表示本次没有点击事件。
 * @return 对应的英文手势名称。
 */
const char* ClickGestureName(uint8_t clicks) {
  switch (clicks) {
    case 0:
      return "None";
    case 1:
      return "Single click";
    case 2:
      return "Double click";
    case 3:
      return "Triple click";
    default:
      return "Multi click";
  }
}

/**
 * @brief 将芯片按住事件编码转换为名称，不将软件触摸状态当作手势。
 * @param press 芯片报告的按住事件编码。
 * @return 对应的英文名称，未知编码可通过串口原始值排查。
 */
const char* HoldGestureName(uint8_t press) {
  switch (press) {
    case 0:
      return "None";
    case 1:
      return "Hold";
    case 3:
      return "Long hold";
    case 7:
      return "Super-long hold";
    default:
      return "Unknown hold";
  }
}

/**
 * @brief 将同一次硬件报告中的点击、按住和方向组合成可读手势。
 * @param event 当前算法组的芯片报告，不根据主控触摸计时推导手势。
 * @param text 输出缓冲区。
 * @param size 输出缓冲区容量。
 */
void FormatGesture(const Sensor::SliderEvent& event, char* text, size_t size) {
  const bool has_click = event.clicks != 0;
  const bool has_hold = event.press != 0;
  snprintf(text, size, "%s%s%s%s%s%s%s%s",
      has_click ? ClickGestureName(event.clicks) : "",
      has_click && has_hold ? " / " : "",
      has_hold ? HoldGestureName(event.press) : "",
      (has_click || has_hold) && event.directions != 0 ? " / " : "",
      (event.directions & 1) != 0 ? "Up " : "",
      (event.directions & 2) != 0 ? "Down " : "",
      (event.directions & 4) != 0 ? "Left " : "",
      (event.directions & 8) != 0 ? "Right" : "");
}

/**
 * @brief 保存当前组的新硬件事件，空报告不覆盖历史，并同步输出串口日志。
 * @param history 对应区域的独立事件记录。
 * @param event 本轮读取的硬件事件。
 * @param now_ms 当前毫秒时刻。
 * @param name 串口中的区域名称。
 */
void CaptureGesture(GestureHistory* history, const Sensor::SliderEvent& event,
    int64_t now_ms, const char* name) {
  if (event.clicks == 0 && event.press == 0 && event.directions == 0) {
    return;
  }
  history->event = event;
  history->time_ms = now_ms;
  char text[112];
  FormatGesture(event, text, sizeof(text));
  printf("AW933xx %s gesture: %s (clicks=%u press=%u directions=0x%02X)\n",
      name, text, event.clicks, event.press, event.directions);
}

/**
 * @brief 刷新一个交互区域，及时展示新事件，过期事件只保留在历史行。
 * @param view 该区域的控件。
 * @param history 该区域独立的最近硬件事件。
 * @param touched 当前有效触摸状态。
 * @param valid 本轮数据是否完整有效。
 * @param now_ms 当前毫秒时刻。
 */
void UpdateGestureView(const GestureView& view, const GestureHistory& history,
    bool touched, bool valid, int64_t now_ms) {
  if (!valid) {
    lv_label_set_text(view.state, "State: unavailable");
    lv_label_set_text(view.gesture, "Gesture: unavailable");
    lv_label_set_text(view.history, "Last gesture: unavailable");
    lv_obj_set_style_text_color(view.gesture, lv_color_hex(kMutedColor), 0);
    return;
  }
  lv_label_set_text(view.state, touched ? "State: touched" : "State: released");
  const bool has_event = history.time_ms >= 0;
  const int64_t age_ms = has_event ? now_ms - history.time_ms : 0;
  char text[112] = {};
  if (has_event) {
    FormatGesture(history.event, text, sizeof(text));
  }
  if (has_event && age_ms < kGestureHighlightMs) {
    lv_label_set_text_fmt(view.gesture, "Gesture: %s", text);
    lv_obj_set_style_text_color(view.gesture, lv_color_hex(kAccentColor), 0);
  } else {
    lv_label_set_text(view.gesture, "Gesture: waiting for next event");
    lv_obj_set_style_text_color(view.gesture, lv_color_hex(kMutedColor), 0);
  }
  if (has_event) {
    lv_label_set_text_fmt(view.history, "Last: %s (%ld s ago)", text,
        static_cast<long>(age_ms / 1000));
  } else {
    lv_label_set_text(view.history, "Last gesture: none");
  }
}

/**
 * @brief 刷新应用主页或当前应用列表，选项高亮后滚动到可见位置。
 * @param ui 导航控件，调用时须持有 LVGL 锁。
 * @param navigation 当前应用、列表选项及页面状态。
 */
void RefreshAppNavigation(const Ui& ui, const NavigationState& navigation) {
  if (navigation.in_app) {
    lv_obj_add_flag(ui.app_tileview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.app_list, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui.navigation_title, kApps[navigation.selected]);
    lv_label_set_text(
        ui.navigation_hint, "Slide to select / Double click to exit");
    for (size_t item = 0; item < kAppListItemCount; ++item) {
      char text[40];
      snprintf(text, sizeof(text), "%s %u", kApps[navigation.selected],
          static_cast<unsigned>(item + 1));
      lv_list_set_button_text(ui.app_list, ui.app_rows[item], text);
      const bool selected = item == navigation.list_selected;
      lv_obj_set_style_bg_color(ui.app_rows[item],
          lv_color_hex(selected ? kAccentColor : 0xF0F4F9), 0);
      lv_obj_set_style_text_color(
          ui.app_rows[item], lv_color_hex(selected ? 0xFFFFFF : kInkColor), 0);
    }
    lv_obj_scroll_to_view(ui.app_rows[navigation.list_selected], LV_ANIM_OFF);
  } else {
    lv_obj_add_flag(ui.app_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.app_tileview, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(
        ui.navigation_title, "Applications / %s", kApps[navigation.selected]);
    lv_label_set_text(ui.navigation_hint, "Slide to switch / Click to enter");
    lv_tileview_set_tile_by_index(ui.app_tileview,
        static_cast<uint32_t>(navigation.selected), 0, LV_ANIM_ON);
  }
}

/**
 * @brief 主页使用虚拟指针驱动原生拖动与吸附，列表按位移切换选项。
 * @param ui 导航控件，调用时须持有 LVGL 锁。
 * @param navigation 当前页面状态和本次滑动锚点。
 * @param touched 本轮是否存在有效滑条信号。
 * @param position 软件信号位置，范围 0~100。
 * @param button_event 本轮 CS1 硬件事件；不处理滑条自身的点击事件。
 */
void UpdateNavigation(const Ui& ui, NavigationState* navigation, bool touched,
    int32_t position, const Sensor::SliderEvent& button_event) {
  if (!navigation->in_app) {
    const int32_t step = lv_obj_get_content_width(ui.app_tileview);
    if (step > 0) {
      const int32_t selected =
          (lv_obj_get_scroll_x(ui.app_tileview) + step / 2) / step;
      navigation->selected = static_cast<size_t>(std::max<int32_t>(
          0, std::min(static_cast<int32_t>(kApps.size()) - 1, selected)));
      lv_label_set_text_fmt(ui.navigation_title, "Applications / %s",
          kApps[navigation->selected]);
    }
  }
  if (button_event.clicks == 2 && navigation->in_app) {
    slider_pointer.state = LV_INDEV_STATE_RELEASED;
    navigation->in_app = false;
    navigation->tracking = false;
    RefreshAppNavigation(ui, *navigation);
    return;
  }
  if (button_event.clicks == 1 && !navigation->in_app) {
    slider_pointer.state = LV_INDEV_STATE_RELEASED;
    navigation->in_app = true;
    navigation->list_selected = 0;
    navigation->tracking = false;
    RefreshAppNavigation(ui, *navigation);
    return;
  }
  if (!navigation->in_app) {
    if (!touched) {
      slider_pointer.state = LV_INDEV_STATE_RELEASED;
      navigation->tracking = false;
      return;
    }
    lv_area_t area;
    lv_obj_get_coords(ui.app_tileview, &area);
    const int32_t width = lv_obj_get_width(ui.app_tileview);
    if (width <= 0) {
      slider_pointer.state = LV_INDEV_STATE_RELEASED;
      navigation->tracking = false;
      return;
    }
    if (!navigation->tracking) {
      navigation->anchor = position;
      navigation->pointer_start_x = (area.x1 + area.x2) / 2;
      navigation->tracking = true;
    }
    // 位置增加映射为向左拖动，保持应用切换方向；由 LVGL 处理滚动和吸附。
    const int32_t x = navigation->pointer_start_x -
                      (position - navigation->anchor) * width / 100;
    slider_pointer.point.x = std::max<int32_t>(area.x1, std::min(area.x2, x));
    slider_pointer.point.y = (area.y1 + area.y2) / 2;
    slider_pointer.state = LV_INDEV_STATE_PRESSED;
    return;
  }
  slider_pointer.state = LV_INDEV_STATE_RELEASED;
  if (!touched) {
    navigation->tracking = false;
  } else if (!navigation->tracking) {
    navigation->anchor = position;
    navigation->tracking = true;
  } else {
    const int32_t steps =
        (position - navigation->anchor) / kNavigationStepPercent;
    if (steps != 0) {
      size_t& current = navigation->list_selected;
      const size_t selected = static_cast<size_t>(std::max<int32_t>(
          0, std::min(static_cast<int32_t>(kAppListItemCount) - 1,
                 static_cast<int32_t>(current) + steps)));
      navigation->anchor = std::max<int32_t>(
          0, std::min<int32_t>(
                 100, navigation->anchor + steps * kNavigationStepPercent));
      if (selected != current) {
        current = selected;
        RefreshAppNavigation(ui, *navigation);
      }
    }
  }
}

}  // namespace

extern "C" void app_main() {
  if (!common::Init()) {
    printf(
        "Device driver initialization completed with errors; continuing "
        "example\n");
  }
  static common::LvglPort port;
  if (!port.Init(ReadSliderPointer)) {
    return;
  }
  const Ui ui = CreateUi();
  common::StartBacklight();
  if (!port.Start()) {
    return;
  }
  gpio_config_t inputs = {};
  inputs.pin_bit_mask =
      (1ULL << kInterrupt) | (1ULL << common::BootButtonGpio());
  inputs.mode = GPIO_MODE_INPUT;
  // INTN 高侧由电平转换模块上拉；不向 1.8V 小板额外注入 3.3V。
  inputs.pull_up_en = GPIO_PULLUP_DISABLE;
  inputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
  inputs.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&inputs) != ESP_OK) {
    port.Lock();
    lv_label_set_text(ui.status, "GPIO setup failed");
    port.Unlock();
    return;
  }
  auto bus = std::make_shared<cpp_bus_driver::SoftwareI2c>(kSda, kScl);
  Sensor sensor(bus);
  // 软件 I2C 避免占用板载显示使用的两个硬件控制器。
  const bool initialized = sensor.Init() && ConfigureSensor(&sensor);
  port.Lock();
  lv_label_set_text(ui.status,
      initialized
          ? "Sensor ready / I2C 0x12"
          : "Sensor init failed\nCheck wiring, 1.8V level shift and ID.");
  port.Unlock();
  if (!initialized) {
    printf("AW933xx initialization failed; no valid sensor data\n");
    sensor.Deinit();
    return;
  }
  uint32_t chip_id = 0, version = 0;
  sensor.GetChipId(&chip_id);
  sensor.GetFirmwareVersion(&version);
  printf("AW933xx ID=0x%08lX FW=0x%08lX SDA=%d SCL=%d INT=%d\n",
      static_cast<unsigned long>(chip_id), static_cast<unsigned long>(version),
      kSda, kScl, kInterrupt);
  bool previous_boot = false;
  int64_t last_action = 0, last_log = 0;
  const char* calibration_status = nullptr;
  GestureHistory button_history, slider_history;
  NavigationState navigation;
  bool button_touched = false;
  uint8_t button_transition_samples = 0;
  bool signal_touched = false;
  uint8_t signal_transition_samples = 0;
  while (true) {
    const int64_t now = esp_timer_get_time() / 1000;
    const bool boot =
        gpio_get_level(static_cast<gpio_num_t>(common::BootButtonGpio())) == 0;
    const char* action = nullptr;
    if (now - last_action > 300 && boot && !previous_boot) {
      signal_touched = false;
      signal_transition_samples = 0;
      button_touched = false;
      button_transition_samples = 0;
      button_history = {};
      slider_history = {};
      navigation.tracking = false;
      port.Lock();
      slider_pointer.state = LV_INDEV_STATE_RELEASED;
      port.Unlock();
      action = CalibrateSensor(&sensor) ? "Air calibration complete"
                                        : "Air calibration failed";
      calibration_status = action;
      last_action = now;
    }
    previous_boot = boot;
    std::array<Sensor::ChannelData, 4> channels;
    Sensor::Events events;
    Sensor::SliderState slider, button;
    // 固定周期读清事件，同时可观察 INTN 电平；即使未接 INTN 也能轮询调试。
    bool success = sensor.ReadEvents(&events) &&
                   sensor.ReadSliderState(0, &slider) &&
                   sensor.ReadSliderState(1, &button);
    for (size_t i = 0; success && i < channels.size(); ++i) {
      success = sensor.ReadChannel(i, &channels[i]);
    }
    const int64_t sample_time_ms = esp_timer_get_time() / 1000;
    bool hardware_touched = false;
    bool position_valid = false;
    int32_t position = 0;
    if (!success) {
      signal_touched = false;
      signal_transition_samples = 0;
      button_touched = false;
      button_transition_samples = 0;
      button_history = {};
      slider_history = {};
    } else {
      const bool button_candidate =
          channels[kButtonChannel].difference >
          static_cast<int32_t>(
              button_touched ? kSignalReleaseThreshold : kSignalTouchThreshold);
      if (button_candidate == button_touched) {
        button_transition_samples = 0;
      } else if (++button_transition_samples >= kSignalDebounceSamples) {
        button_touched = button_candidate;
        button_transition_samples = 0;
      }
      hardware_touched = (events.proximity0 & (1U << kButtonChannel)) != 0;
      const bool candidate = GetSignalPosition(channels,
          signal_touched ? kSliderSignalReleaseThreshold
                         : kSliderSignalTouchThreshold,
          &position);
      if (candidate == signal_touched) {
        signal_transition_samples = 0;
      } else if (++signal_transition_samples >= kSignalDebounceSamples) {
        signal_touched = candidate;
        signal_transition_samples = 0;
      }
      position_valid = signal_touched && candidate;
      if (!position_valid) {
        position = 0;
      }
      // 日志和页面使用同一份分组事件记录；组 0 不覆盖组 1 的手势。
      CaptureGesture(
          &slider_history, events.sliders[0], sample_time_ms, "slider");
      CaptureGesture(&button_history, events.sliders[1], sample_time_ms, "CS1");
      if (sample_time_ms - last_log >= 1000) {
        printf(
            "AW933xx button=%d button_state=0x%lX hw_x=%u centroid=%ld "
            "touch=%d diff(CH0-3)=%ld,%ld,%ld,%ld prox0=0x%X prox1=0x%X "
            "irq=0x%lX\n",
            button_touched, static_cast<unsigned long>(button.button_state),
            slider.position_x, static_cast<long>(position), signal_touched,
            static_cast<long>(channels[0].difference / 1024),
            static_cast<long>(channels[1].difference / 1024),
            static_cast<long>(channels[2].difference / 1024),
            static_cast<long>(channels[3].difference / 1024),
            static_cast<unsigned>(events.proximity0),
            static_cast<unsigned>(events.proximity1),
            static_cast<unsigned long>(events.source));
        last_log = sample_time_ms;
      }
    }
    port.Lock();
    if (!success) {
      lv_label_set_text(ui.status, "Read failed - check sensor connection");
      lv_label_set_text(ui.position, "Signal position: --");
    } else {
      lv_label_set_text(ui.status,
          calibration_status != nullptr && sample_time_ms - last_action < 3000
              ? calibration_status
              : "Ready - touch button or slide");
      if (position_valid) {
        lv_label_set_text_fmt(
            ui.position, "Signal position: %ld%%", static_cast<long>(position));
      } else {
        lv_label_set_text(ui.position, "Signal position: --");
      }
    }
    UpdateGestureView(
        ui.button, button_history, hardware_touched, success, sample_time_ms);
    UpdateGestureView(
        ui.slider, slider_history, signal_touched, success, sample_time_ms);
    lv_bar_set_value(ui.bar, position, LV_ANIM_OFF);
    if (success && action == nullptr) {
      UpdateNavigation(
          ui, &navigation, position_valid, position, events.sliders[1]);
    } else {
      slider_pointer.state = LV_INDEV_STATE_RELEASED;
      navigation.tracking = false;
    }
    port.Unlock();
    if (action != nullptr) {
      printf("AW933xx: %s\n", action);
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}
