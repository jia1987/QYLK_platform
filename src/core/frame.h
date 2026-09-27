// @srs SRS-010 SRS-019 SRS-021
#pragma once
// 北京枢点 RS485 协议帧定义 —— 常量、枚举与冻结默认值。
// 依据：技术资料/北京枢点RS485通讯协议.pdf + 设计方案文档 §2/§3（决策 D1、D5、T1、T8）。
#include <array>
#include <cstdint>

namespace massage::core {

inline constexpr std::size_t kFrameLen = 8;  // 协议帧固定 8 字节
using Frame = std::array<std::uint8_t, kFrameLen>;

// ---- 主机 -> 从机 指令头码 ----
enum class Cmd : std::uint8_t {
  ConfigAddress = 0xAA,  // 地址+开闭环+堵转时间+极对数（带记忆；严禁上总线广播！仅维护模式单头烧录）
  ConfigParams  = 0x54,  // 限流+缓启动+换向延时（带记忆）
  HallCorrect   = 0x5D,  // 霍尔相序矫正（矫正期间屏蔽一切指令）
  MotorCtrl     = 0x55,  // 核心控制：占空比/转速 + 状态字
  Broadcast     = 0x60,  // 广播（无应答，不可靠）—— 本软件不使用（决策 D5）
  OriginSetup   = 0x59,  // 定零位 —— 不使用（决策 D1：全部速度控制）
  PositionCtrl  = 0x5A,  // 位置控制 —— 不使用
  ReadSpeed     = 0x56,  // 读转速
  ReadPosition  = 0x57,  // 读位置
  ReadInfo      = 0x58,  // 读故障码/温度/运行状态
};

// ---- 从机 -> 主机 应答头码 ----
enum class Reply : std::uint8_t {
  Ack      = 0x11,  // 0x54/0x55/0x5A/0xAA/0x5D 的应答
  Speed    = 0x12,  // 转速应答（24 位 RPM）
  Position = 0x13,  // 位置应答（24 位有符号）
  Info     = 0x14,  // 控制信息应答（故障码/温度/运行状态）
};

// ---- 0x55 状态字（第 6 字节）----
enum class MotorStatus : std::uint8_t {
  DirA      = 0x00,  // A 转向
  DirB      = 0x01,  // B 转向
  ResetA    = 0x02,  // 故障复位 + A 转向
  ResetB    = 0x03,  // 故障复位 + B 转向
  Brake     = 0x04,  // 刹车（快速制动；预留能力，见 D3）
};

// ---- 0x11 应答标志（第 4 字节）----
enum class AckFlag : std::uint8_t {
  Ok       = 0xAA,  // 成功
  Fail     = 0xBB,  // 失败
  AddrBusy = 0xEE,  // 电机运行中拒绝改地址（仅 0xAA 应答出现）
};

// ---- 0x14 应答故障位（第 4 字节，位掩码）----
enum FaultBit : std::uint8_t {
  FaultNone      = 0x00,
  FaultOverVolt  = 0x01,  // 过压保护
  FaultUnderVolt = 0x02,  // 欠压保护
  FaultOverTemp  = 0x04,  // 过温保护
  FaultHall      = 0x10,  // 霍尔异常保护
  FaultStall     = 0x20,  // 堵转保护
  FaultShort     = 0x40,  // 输出短路保护
  MotorRunning   = 0x80,  // 电机运行中（非故障）
};

// ---- 速度数值编码 ----
inline constexpr std::uint16_t kClosedLoopBit    = 0x1000;  // 最高位=1 -> 闭环转速模式
inline constexpr std::uint16_t kMaxSpeedValue    = 3001;   // 开环占空比/闭环转速上限
inline constexpr std::uint16_t kMinClosedLoopRpm = 100;    // 闭环最低转速
inline constexpr std::uint16_t kCoastThreshold   = 50;     // 数值 <50 -> 0 速惯性滑行

// ---- 冻结出厂默认值（决策 D1/T1/T8；帧表见设计方案 §2）----
namespace defaults {
inline constexpr std::uint8_t kPolePairs    = 2;     // 电机规格书 FG 描述背书：两对极磁环
inline constexpr std::uint8_t kStallTime    = 0x14;  // 堵转保护 2.0s
inline constexpr std::uint8_t kCurrentLimit = 0x4D;  // 限流 ≈10A（文档默认）
inline constexpr std::uint8_t kSoftStart    = 0x10;  // 缓启动（文档建议上限；满足 D3 柔和恢复）
inline constexpr std::uint8_t kDirDelay     = 0x00;  // 正反转切换延时
inline constexpr bool kClosedLoop           = true;  // 有感方波闭环
inline constexpr std::uint8_t kHallState    = 0x00;  // 霍尔状态（出厂马达默认）
}  // namespace defaults

// ---- 频率换算（决策 D1：RPM = Hz × 60，UI 量程 10–50Hz 步进 5）----
inline constexpr int kMinFreqHz = 10;
inline constexpr int kMaxFreqHz = 50;  // 3000RPM ≤ 协议上限 3001，硬件方已背书长时间运行

constexpr std::uint16_t hzToRpm(int freqHz) noexcept {
  return static_cast<std::uint16_t>(freqHz * 60);
}
constexpr int rpmToHz(std::uint16_t rpm) noexcept { return rpm / 60; }

}  // namespace massage::core
