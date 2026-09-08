/*
 * @Description: 在屏幕上运行 LVGL 仪表盘界面的示例
 * @Author: LILYGO_L
 * @Date: 2026-07-28 13:59:02
 * @LastEditTime: 2026-09-07 15:58:59
 * @License: GPL 3.0
 */
#include "common_lvgl.h"
#include "esp_lcd_mipi_dsi.h"
#include "lvgl.h"

namespace {

extern "C" void example_lvgl_demo_ui(lv_display_t* display);

void ReleasedInput(lv_indev_t*, lv_indev_data_t* data) {
  data->state = LV_INDEV_STATE_RELEASED;
}

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
/**
 * @brief 启动显示时序后依次显示屏幕内部测试图案
 * @return 图案设置及测试后的清理均成功返回 true，否则返回 false
 * @note 清理后需调用 InitScreen，重新初始化时通过硬件复位恢复视频模式。
 */
bool RunScreenInternalTests() {
  using Screen = cpp_bus_driver::S023msafjf10111e1;
  auto& driver = common::GetDriver();
  if (!driver.InitScreen()) {
    return false;
  }
  auto* screen = driver.chip().s023msafjf10111e1.get();
  struct TestPattern {
    const char* name;
    Screen::BistPattern pattern;
  };
  constexpr TestPattern kPatterns[] = {
      {"red", Screen::BistPattern::kRed},
      {"green", Screen::BistPattern::kGreen},
      {"blue", Screen::BistPattern::kBlue},
      {"white", Screen::BistPattern::kWhite},
      {"blank", Screen::BistPattern::kBlank},
      {"color bars", Screen::BistPattern::kColorBar},
  };
  // BIST 从硬件复位后的默认状态进入，避免沿用视频初始化的亮度路径配置。
  // 使用复位默认增益；测试期间不要调用
  // SetBrightnessGain，以免改写公共控制寄存器。
  bool result = screen->Reset();
  if (result) {
    for (const auto& test : kPatterns) {
      result = screen->SetBistPattern(test.pattern);
      if (result) {
        Screen::BistPattern actual;
        result = screen->GetBistPattern(&actual);
        if (result) {
          result = actual == test.pattern;
        }
      }
      printf("[S023MSAFJF10111E1] Internal test: %s: %s\n", test.name,
          result ? "success" : "failed");
      if (!result) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  } else {
    printf("Screen internal test reset failed\n");
  }
  // 手册未提供完整的 BIST 退出序列，解除初始化后由 InitScreen
  // 复位并恢复视频配置。
  const bool cleaned_up = driver.DeinitScreen();
  if (!cleaned_up) {
    printf("Screen internal test cleanup failed\n");
  }
  return result && cleaned_up;
}

/**
 * @brief 通过 LVGL 绘制软件彩条并发送到屏幕
 * @param display 当前 LVGL 显示对象
 * @return 显示对象有效且亮度配置成功返回 true，否则返回 false
 * @note 在 LVGL 任务启动前调用，结束后清理测试对象。
 */
bool RunScreenSoftwareColorBars(lv_display_t* display) {
  if (display == nullptr) {
    return false;
  }
  auto* root = lv_display_get_screen_active(display);
  const int32_t width = lv_display_get_horizontal_resolution(display);
  const int32_t height = lv_display_get_vertical_resolution(display);
  constexpr uint32_t kColors[] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00,
      0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};
  constexpr int32_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);
  for (int32_t i = 0; i < kColorCount; ++i) {
    auto* bar = lv_obj_create(root);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    const int32_t top = height * i / kColorCount;
    const int32_t bottom = height * (i + 1) / kColorCount;
    lv_obj_set_pos(bar, 0, top);
    lv_obj_set_size(bar, width, bottom - top);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kColors[i]), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  }
  lv_refr_now(display);
  auto* screen = common::GetDriver().chip().s023msafjf10111e1.get();
  const bool result =
      screen != nullptr &&
      screen->SetBrightnessGain(
          cpp_bus_driver::S023msafjf10111e1::kDefaultBrightnessGain);
  printf("[S023MSAFJF10111E1] Software color bars: %s\n",
      result ? "displaying" : "failed");
  if (result) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  lv_obj_clean(root);
  return result;
}
#endif

// 每个按键步骤只应用一次配置，不在步骤内部循环演示。
struct ScreenTestStep {
  const char* name;
  bool (*apply)();
};

struct ScreenTestSequence {
  const char* screen_name;
  const ScreenTestStep* steps;
  size_t count;
};

/**
 * @brief 获取当前屏幕的按键测试序列
 * @return 当前屏幕的测试项；设备未就绪或状态读取失败时返回空序列
 */
ScreenTestSequence GetScreenTestSequence() {
  auto& driver = common::GetDriver();
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
  using Screen = cpp_bus_driver::S023msafjf10111e1;
  if (!driver.IsS023msafjf10111e1Ready()) {
    return {};
  }
  static auto* screen = driver.chip().s023msafjf10111e1.get();
  static Screen::MirrorMode initial_mirror;
  static int8_t initial_x;
  static int8_t initial_y;
  static uint16_t initial_gain;
  if (!screen->GetMirror(&initial_mirror) ||
      !screen->GetPixelShift(&initial_x, &initial_y) ||
      !screen->GetBrightnessGain(&initial_gain)) {
    printf("Screen initial test state read failed\n");
    return {};
  }
  printf("S023 initial mirror: %d, pixel shift: (%d, %d), gain: %u\n",
      static_cast<int>(initial_mirror), initial_x, initial_y,
      static_cast<unsigned int>(initial_gain));

  // 镜像测试以设备默认显示方向为基准。
  static const ScreenTestStep kSteps[] = {
      {"left-right mirror",
          [] { return common::GetDriver().SetScreenMirror(true, false); }},
      {"up-down mirror",
          [] { return common::GetDriver().SetScreenMirror(false, true); }},
      {"left-right + up-down mirror",
          [] { return common::GetDriver().SetScreenMirror(true, true); }},
      {"restore default direction",
          [] { return common::GetDriver().SetScreenMirror(false, false); }},
      {"pixel shift=(-4, 0)", [] { return screen->SetPixelShift(-4, 0); }},
      {"pixel shift=(4, 0)", [] { return screen->SetPixelShift(4, 0); }},
      {"pixel shift=(0, -10)", [] { return screen->SetPixelShift(0, -10); }},
      {"pixel shift=(0, 10)", [] { return screen->SetPixelShift(0, 10); }},
      {"restore initial pixel shift",
          [] { return screen->SetPixelShift(initial_x, initial_y); }},
      {"brightness gain=0", [] { return screen->SetBrightnessGain(0); }},
      {"brightness gain=64", [] { return screen->SetBrightnessGain(64); }},
      {"brightness gain=128", [] { return screen->SetBrightnessGain(128); }},
      {"brightness gain=192", [] { return screen->SetBrightnessGain(192); }},
      {"brightness gain=256", [] { return screen->SetBrightnessGain(256); }},
      {"restore initial screen state",
          [] {
            bool result = screen->SetMirror(initial_mirror);
            result &= screen->SetPixelShift(initial_x, initial_y);
            result &= screen->SetBrightnessGain(initial_gain);
            return result;
          }},
  };
  return {"S023MSAFJF10111E1", kSteps, sizeof(kSteps) / sizeof(kSteps[0])};
#else
  if (common::IsHi8561Screen() && driver.IsHi8561Ready()) {
    using Screen = cpp_bus_driver::Hi8561;
    static auto* screen = driver.chip().hi8561.get();
    static const ScreenTestStep kSteps[] = {
        {"mirror=kHorizontal",
            [] { return screen->SetMirror(Screen::MirrorMode::kHorizontal); }},
        {"mirror=kVertical",
            [] { return screen->SetMirror(Screen::MirrorMode::kVertical); }},
        {"mirror=kHorizontalVertical",
            [] {
              return screen->SetMirror(Screen::MirrorMode::kHorizontalVertical);
            }},
        {"mirror=kOff",
            [] { return screen->SetMirror(Screen::MirrorMode::kOff); }},
        {"color order=BGR",
            [] { return screen->SetColorOrder(Screen::ColorOrder::kBgr); }},
        {"color order=RGB",
            [] { return screen->SetColorOrder(Screen::ColorOrder::kRgb); }},
        {"inversion=on", [] { return screen->SetInversion(true); }},
        {"inversion=off", [] { return screen->SetInversion(false); }},
        {"display=off (black screen)",
            [] { return screen->SetScreenOff(true); }},
        {"display=on", [] { return screen->SetScreenOff(false); }},
        {"sleep=on (black screen)",
            [] {
              return screen->SetScreenOff(true) && screen->SetSleep(true);
            }},
        {"sleep=off, display=on",
            [] {
              return screen->SetSleep(false) && screen->SetScreenOff(false);
            }},
    };
    return {"HI8561", kSteps, sizeof(kSteps) / sizeof(kSteps[0])};
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  if (common::IsRm69a10Screen() && driver.IsRm69a10Ready()) {
    static auto* screen = driver.chip().rm69a10.get();
    static const ScreenTestStep kSteps[] = {
        {"inversion=on", [] { return screen->SetInversion(true); }},
        {"inversion=off", [] { return screen->SetInversion(false); }},
        {"display=off (black screen)",
            [] { return screen->SetScreenOff(true); }},
        {"display=on", [] { return screen->SetScreenOff(false); }},
        {"sleep=on (black screen)",
            [] {
              return screen->SetScreenOff(true) && screen->SetSleep(true);
            }},
        {"sleep=off, display=on",
            [] {
              return screen->SetSleep(false) && screen->SetScreenOff(false);
            }},
    };
    return {"RM69A10", kSteps, sizeof(kSteps) / sizeof(kSteps[0])};
  }
#endif
  return {};
#endif
}

/**
 * @brief 消抖并检测 BOOT 按键的单次按下事件
 * @param pressed 当前采样是否为按下状态
 * @param now_ms 当前单调时钟，单位 ms
 * @param sampled 上次采样状态
 * @param stable 已确认的稳定状态
 * @param changed_ms 最近一次采样变化的时间
 * @return 检测到稳定的按下沿返回 true，其余情况返回 false
 */
bool UpdateBootButton(bool pressed, int64_t now_ms, bool& sampled, bool& stable,
    int64_t& changed_ms) {
  constexpr int64_t kDebounceMs = 30;
  if (pressed != sampled) {
    sampled = pressed;
    changed_ms = now_ms;
  }
  if (sampled != stable && now_ms - changed_ms >= kDebounceMs) {
    stable = sampled;
    return stable;
  }
  return false;
}

/**
 * @brief 使用 BOOT 按键循环切换屏幕测试项
 * @param lvgl_port 当前 LVGL 运行实例
 * @note 持续运行；短按切换一项，长按不重复，失败时下次按键重试当前项。
 */
void RunBootScreenTests(common::LvglPort& lvgl_port) {
  cpp_bus_driver::PlatformHal platform_hal;
  if (!platform_hal.SetGpioMode(common::BootButtonGpio(),
          cpp_bus_driver::PlatformHal::GpioMode::kInput,
          cpp_bus_driver::PlatformHal::GpioStatus::kPullup)) {
    printf("BOOT button initialization failed\n");
    // LVGL 任务仍使用调用方的实例，保持 app_main 栈和对象存活。
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  const auto sequence = GetScreenTestSequence();
  printf("Screen mode: LVGL; BOOT GPIO: %d; test steps: %u\n",
      common::BootButtonGpio(), static_cast<unsigned int>(sequence.count));
  if (sequence.count == 0) {
    printf("No screen test sequence available\n");
  }

  size_t next_step = 0;
  // 启动时已经按住的按键需先释放，避免进入示例即触发切换。
  bool sampled = !platform_hal.GpioRead(common::BootButtonGpio());
  bool stable = sampled;
  int64_t changed_ms = platform_hal.GetSystemTimeMs();
  while (true) {
    const bool pressed = !platform_hal.GpioRead(common::BootButtonGpio());
    if (UpdateBootButton(pressed, platform_hal.GetSystemTimeMs(), sampled,
            stable, changed_ms) &&
        sequence.count != 0) {
      const auto& step = sequence.steps[next_step];
      printf("[%s] BOOT step %u/%u: %s\n", sequence.screen_name,
          static_cast<unsigned int>(next_step + 1),
          static_cast<unsigned int>(sequence.count), step.name);
      // 与 LVGL 绘制串行执行，避免发送屏幕命令时又提交新的图像区域。
      lvgl_port.Lock();
      const bool result = step.apply();
      lvgl_port.Unlock();
      printf("[%s] %s: %s\n", sequence.screen_name, step.name,
          result ? "success" : "failed; next press retries this step");
      if (result) {
        next_step = (next_step + 1) % sequence.count;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

extern "C" void app_main(void) {
  printf("LVGL screen example on %s\n", common::kBoardName);
  if (!common::InitDriver()) {
    printf("Device driver initialization completed with errors\n");
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
  if (!RunScreenInternalTests()) {
    printf("Screen internal tests failed\n");
    return;
  }
  printf("Screen mode: initializing MIPI/LVGL after panel reset\n");
  if (!common::GetDriver().InitScreen()) {
    printf("Screen device initialization failed\n");
    return;
  }
#endif
  if (!common::GetDriver().IsScreenReady()) {
    printf("Screen init failed\n");
    return;
  }

  common::LvglPort lvgl_port;
  if (!lvgl_port.Init(ReleasedInput)) {
    printf("LVGL init failed\n");
    return;
  }
#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
  if (!RunScreenSoftwareColorBars(lvgl_port.display())) {
    printf("Screen software color bars failed\n");
    return;
  }
  printf("Screen mode: LVGL\n");
#endif
  example_lvgl_demo_ui(lvgl_port.display());
  // 提交界面首帧后启动持续刷新。
  lv_refr_now(lvgl_port.display());
  if (!lvgl_port.Start()) {
    printf("LVGL task start failed\n");
    return;
  }

#if !defined(CONFIG_LILYGO_DEVICE_DRIVER_T_GLASSES_P4)
  common::StartBacklight();
#endif
  RunBootScreenTests(lvgl_port);
}
