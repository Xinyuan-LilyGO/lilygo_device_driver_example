/*
 * @Description: 声明 LoRa 接收灵敏度与丢包率测试的公共接口
 * @Author: LILYGO_L
 * @Date: 2026-07-29 15:09:12
 * @LastEditTime: 2026-07-29 16:55:27
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lora_rx_sensitivity {

// Semtech数据手册灵敏度测试使用的Payload长度
inline constexpr size_t kPayloadLength = 64;
// 每个输入功率点要求信号发生器发送的总包数
inline constexpr uint32_t kExpectedPacketCount = 1000;
// 收到过数据后用于判定信号流结束的空闲时间
inline constexpr uint32_t kIdleFinishTimeMs = 10000;
// 等待首包和测试进行期间周期输出状态日志的时间间隔
inline constexpr uint32_t kProgressPrintIntervalMs = 5000;
// 可修改的LoRa扩频因子，LR2021、SX1262和LR1121统一使用该值
inline constexpr uint8_t kSpreadingFactor = 12;
static_assert(kSpreadingFactor >= 5 && kSpreadingFactor <= 12,
    "LoRa spreading factor must be between SF5 and SF12");
// LoRa公共网络同步字
inline constexpr uint8_t kPublicSyncWord = 0x34;

// 信号发生器选择All 1时生成的固定64字节测试Payload
inline constexpr std::array<uint8_t, kPayloadLength> kExpectedPayload = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// 最近一个正确数据包的信号质量数据
struct PacketMetrics {
  float packet_rssi_dbm = 0.0f;  // 数据包RSSI，单位为dBm
  float signal_rssi_dbm = 0.0f;  // LoRa信号RSSI，单位为dBm
  float snr_db = 0.0f;           // 数据包SNR，单位为dB
  bool has_signal_rssi = false;  // 当前芯片是否提供独立信号RSSI
};

// 接收端能够识别的数据包错误类型
enum class PacketError {
  kHeader,  // LoRa Header错误
  kCrc,     // Payload CRC错误
  kLength,  // 数据包长度错误
  kRead,    // 数据包内容读取失败
};

// 管理单个输入功率点的接收统计、PER计算和测试状态
class TestSession {
 public:
  /**
   * @brief 创建一个LoRa接收灵敏度测试会话
   * @param radio_name 当前无线芯片名称
   * @param frequency_hz 当前测试频率，单位为Hz
   */
  TestSession(const char* radio_name, uint32_t frequency_hz);

  /**
   * @brief 输出当前接收配置和信号发生器Payload设置
   */
  void PrintSetup() const;

  /**
   * @brief 周期输出状态日志并在数据流空闲时结束测试
   * @param current_time_ms 当前系统时间，单位为ms
   */
  void Poll(uint32_t current_time_ms);

  /**
   * @brief 清空当前统计并重新等待一轮射频数据包
   * @param current_time_ms 当前系统时间，单位为ms
   */
  void Restart(uint32_t current_time_ms);

  /**
   * @brief 校验并统计一个硬件CRC正确的接收数据包
   * @param payload 接收Payload地址
   * @param payload_length 接收Payload长度
   * @param metrics 当前数据包的信号质量
   * @param current_time_ms 当前系统时间，单位为ms
   */
  void RecordPacket(const uint8_t* payload, size_t payload_length,
      const PacketMetrics& metrics, uint32_t current_time_ms);

  /**
   * @brief 统计一个由无线芯片报告的数据包错误
   * @param error 数据包错误类型
   * @param current_time_ms 当前系统时间，单位为ms
   */
  void RecordPacketError(
      PacketError error, uint32_t current_time_ms);

  /**
   * @brief 统计一次不直接代表数据包的驱动或传输错误
   */
  void RecordDriverError();

 private:
  // 保存一项信号质量数据的样本数量、总和与范围
  struct MetricStatistics {
    uint32_t count = 0;     // 已累计的有效样本数
    float sum = 0.0f;       // 全部有效样本之和
    float minimum = 0.0f;   // 最小样本值
    float maximum = 0.0f;   // 最大样本值

    /**
     * @brief 添加一个信号质量样本
     * @param value 待添加的样本值
     */
    void Add(float value);

    /**
     * @brief 计算全部有效样本的平均值
     * @return 没有样本返回0，否则返回样本平均值
     */
    float Average() const;
  };

  /**
   * @brief 清空历史数据并开始一个新的输入功率点
   * @param current_time_ms 当前系统时间，单位为ms
   */
  void Start(uint32_t current_time_ms);

  /**
   * @brief 结束当前输入功率点并输出最终PER结果
   * @param current_time_ms 当前系统时间，单位为ms
   * @param reason 测试结束原因
   */
  void Finish(uint32_t current_time_ms, const char* reason);

  /**
   * @brief 输出当前接收统计、错误和信号质量的阶段结果
   * @param current_time_ms 当前系统时间，单位为ms
   */
  void PrintProgress(uint32_t current_time_ms) const;

  /**
   * @brief 检查已观察包数是否达到配置的期望包数
   * @param current_time_ms 当前系统时间，单位为ms
   */
  void CheckForCompletion(uint32_t current_time_ms);

  /**
   * @brief 计算已经观察到的错误包总数
   * @return Header、CRC、长度、Payload和读取错误包数之和
   */
  uint32_t ObservedBadPackets() const;

  /**
   * @brief 计算已经观察到的数据包事件总数
   * @return 正确包数与已观察错误包数之和
   */
  uint32_t ObservedPackets() const;

  const char* radio_name_;  // 当前无线芯片名称
  uint32_t frequency_hz_;   // 当前测试频率，单位为Hz
  bool running_ = false;    // 当前功率点是否正在统计
  uint32_t start_time_ms_ = 0;  // 当前测试开始时间，单位为ms
  uint32_t last_activity_time_ms_ = 0;  // 最近包事件时间，单位为ms
  uint32_t last_progress_time_ms_ = 0;  // 最近状态日志输出时间，单位为ms
  bool progress_timer_started_ = false;  // 状态日志计时器是否已经启动
  bool activity_seen_ = false;  // 当前测试是否观察到过包事件
  uint32_t good_packets_ = 0;   // Payload完全正确的数据包数
  uint32_t header_errors_ = 0;  // LoRa Header错误包数
  uint32_t crc_errors_ = 0;     // Payload CRC错误包数
  uint32_t length_errors_ = 0;  // 无线芯片报告的长度错误包数
  uint32_t payload_errors_ = 0;  // Payload长度或内容不匹配包数
  uint32_t read_errors_ = 0;     // 接收FIFO或缓冲区读取失败包数
  uint32_t driver_errors_ = 0;  // 不直接代表数据包的驱动错误数
  MetricStatistics packet_rssi_;  // 正确包的Packet RSSI统计
  MetricStatistics signal_rssi_;  // 正确包的Signal RSSI统计
  MetricStatistics snr_;          // 正确包的SNR统计
};

/**
 * @brief 运行LR1121接收灵敏度测试
 */
void RunLr1121();

/**
 * @brief 运行LR2021接收灵敏度测试
 */
void RunLr2021();

/**
 * @brief 运行SX1262接收灵敏度测试
 */
void RunSx1262();

}  // namespace lora_rx_sensitivity
