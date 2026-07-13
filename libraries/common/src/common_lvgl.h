/*
 * @Description: LVGL port shared by the display examples
 * @Author: LILYGO_L
 * @Date: 2026-07-11 16:22:23
 * @LastEditTime: 2026-07-13 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "common.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "sys/lock.h"

namespace common {

/**
 * @brief 管理示例共用的 LVGL 显示、输入、tick 定时器和任务
 */
class LvglPort final {
 public:
  /**
   * @brief 初始化 LVGL 显示、输入设备和 tick 定时器
   * @param input_callback LVGL 指针输入读取回调
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Init(lv_indev_read_cb_t input_callback);

  /**
   * @brief 启动 LVGL 处理任务
   * @return 任务启动成功返回 true，否则返回 false
   */
  bool Start();

  /**
   * @brief 锁定 LVGL API 访问
   */
  void Lock();

  /**
   * @brief 解锁 LVGL API 访问
   */
  void Unlock();

  /**
   * @brief 获取 LVGL 显示对象
   * @return LVGL 显示对象指针
   */
  lv_display_t* display() const { return display_; }

  /**
   * @brief 获取当前屏幕对应的 LVGL 颜色格式
   * @return LVGL 颜色格式
   */
  lv_color_format_t color_format() const;

 private:
  // LVGL tick 周期，单位为毫秒
  static constexpr int kTickPeriodMs = 1;
  // LVGL 任务栈大小，单位为字节
  static constexpr int kTaskStackBytes = 16 * 1024;
  // LVGL 任务优先级
  static constexpr UBaseType_t kTaskPriority = 1;
  // LVGL handler 的最小等待时间，单位为毫秒
  static constexpr uint32_t kMinimumHandlerDelayMs = 10;

  /**
   * @brief 处理 LVGL 显示刷新请求
   * @param display LVGL 显示对象
   * @param area 待刷新的屏幕区域
   * @param pixels 像素数据地址
   */
  static void FlushCallback(
      lv_display_t* display, const lv_area_t* area, uint8_t* pixels);

  /**
   * @brief 增加 LVGL tick
   * @param context 回调上下文
   */
  static void TickCallback(void* context);

  /**
   * @brief 进入 LVGL 任务循环
   * @param context LvglPort 对象指针
   */
  static void TaskEntry(void* context);

  /**
   * @brief 注册屏幕传输完成回调
   * @return 注册成功返回 true，否则返回 false
   */
  bool RegisterFlushReadyCallback();

  /**
   * @brief 获取 LVGL 绘制缓冲区大小
   * @return 绘制缓冲区字节数
   */
  size_t DrawBufferSize() const;

  /**
   * @brief 运行 LVGL handler 任务循环
   */
  void TaskLoop();

  // LVGL 显示对象
  lv_display_t* display_ = nullptr;
  // LVGL 指针输入设备
  lv_indev_t* input_ = nullptr;
  // LVGL 绘制缓冲区
  void* draw_buffer_ = nullptr;
  // LVGL tick 定时器句柄
  esp_timer_handle_t tick_timer_ = nullptr;
  // LVGL API 访问锁
  _lock_t lock_ = nullptr;
};

}  // namespace common
