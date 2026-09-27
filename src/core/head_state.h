// @srs SRS-030 SRS-031 SRS-032 SRS-033 SRS-034 SRS-040 SRS-041 SRS-042 SRS-044 SRS-050 SRS-051 SRS-052 SRS-053
#pragma once
// TreatmentHead —— 单治疗头状态机 + 倒计时 + 曲线推进（决策 D3/D4）。
// 纯逻辑：无时钟无串口。外部以 tick(dt) 驱动、以 onXxx() 注入设备事件，
// 命令方法返回「需要下发的帧」，由应用层转交 BusScheduler。
//
// 状态图（设计方案 D3）：
//   Absent ⇄ Idle → Starting → Running ⇄ Paused
//                  Running/Paused/Starting → Stopping → Idle
//                  任意运行态 → Fault →(手动复位+ack)→ Idle
// 语义冻结：暂停/停止 = 滑行停止（速度0）；恢复 = 缓启动爬升（板载 0x10）；
//           倒计时归零 = 自动滑行停止 + Completed；暂停/故障冻结倒计时。
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/frame.h"
#include "core/frame_codec.h"
#include "core/profile.h"

namespace massage::core {

enum class HeadState : std::uint8_t {
  Absent,    // 总线无应答（未插入/离线）
  Idle,      // 在位待机
  Starting,  // 已发运行帧，缓启动爬升中
  Running,   // 治疗进行（倒计时走）
  Paused,    // 已暂停（倒计时冻结，电机滑行停止）
  Stopping,  // 停止中（等待转速归零确认）
  Fault,     // 板载故障（需手动复位）
};

enum class HeadEvent : std::uint8_t {
  Started,               // 治疗启动（帧已发出）
  Paused,
  Resumed,
  StopRequested,         // 用户点停止
  TimerExpired,          // 倒计时归零（自动停止路径）
  Completed,             // 归零且电机已停稳
  FaultEntered,          // 0x58 报出故障码
  FaultCleared,          // 手动复位成功
  WentOfflineDuringRun,  // 治疗中总线失联（D5：电机可能仍在转 → UI 提示急停）
  AckRejected,           // 板子应答 0xBB/0xEE（命令被拒）
};

struct HeadConfig {
  int freqHz = 30;                          // 10..50（决策 D1）
  Mode mode = Mode::Constant;
  int timeMin = 20;                         // 5..60
  MotorStatus dir = MotorStatus::DirA;      // 每头方向（决策 D10：绑定时试运行确认）
};

class TreatmentHead {
 public:
  TreatmentHead(std::string slotId, std::uint8_t addr);

  // ---- 查询（UI/审计只读）----
  const std::string& slotId() const noexcept { return slotId_; }
  std::uint8_t addr() const noexcept { return addr_; }
  HeadState state() const noexcept { return state_; }
  bool online() const noexcept { return online_; }
  int remainingSeconds() const noexcept;
  int totalSeconds() const noexcept;
  double profileElapsedSeconds() const noexcept { return profileElapsedS_; }
  std::uint8_t faultBits() const noexcept { return faultBits_; }
  std::uint8_t tempC() const noexcept { return tempC_; }
  std::uint16_t reportedRpm() const noexcept { return reportedRpm_; }
  bool boardMotorRunning() const noexcept { return boardMotorRunning_; }
  const HeadConfig& config() const noexcept { return config_; }
  std::uint16_t currentTargetRpm() const;  // 曲线当前瞬时目标（UI 显示实际频率）

  // ---- 设备事件（BusScheduler 应答路由驱动）----
  void onPresence(bool online);
  void onAck(AckFlag flag);
  void onInfo(std::uint8_t faultRawBits, std::uint8_t tempC, bool motorRunning);
  void onSpeed(std::uint32_t rpm);

  // ---- UI 命令（返回待下发帧；nullopt = 状态不允许或本步无需发帧）----
  std::optional<Frame> start();       // 仅 Idle + 在位
  std::optional<Frame> pause();       // Running/Starting → Paused
  std::optional<Frame> resume();      // Paused → Running
  std::optional<Frame> stop();        // 运行态 → Stopping（计时归零）
  std::optional<Frame> resetFault();  // Fault → 复位帧（ack 成功回 Idle）

  // 运行中实时改参数（原型行为）：时间按差值调整剩余；方向仅 Idle 可改。
  bool applyConfig(const HeadConfig& c);

  // ---- 周期驱动（建议 250ms，与曲线下发节奏一致 —— D4）----
  // 返回需要下发的帧（曲线目标变化 ≥ 阈值 / 停止重发等），无则 nullopt。
  std::optional<Frame> tick(double dtSeconds);

  void setEventCallback(std::function<void(HeadEvent)> cb) { eventCb_ = std::move(cb); }

 private:
  enum class Pending { None, Start, Pause, Resume, Stop, Reset, Profile };

  void fire(HeadEvent e);
  std::uint16_t profileCommandRpm() const;
  std::optional<Frame> makeRunFrame(std::uint16_t rpm) const;
  std::optional<Frame> maybeEmitProfile();
  bool advanceCountdown(double dt);  // true = 归零
  void finishStop();
  void resetTimers();

  std::string slotId_;
  std::uint8_t addr_;
  HeadState state_ = HeadState::Absent;
  bool online_ = false;
  HeadConfig config_;

  double totalS_ = 0.0;
  double remainS_ = 0.0;
  double profileElapsedS_ = 0.0;
  double startingElapsedS_ = 0.0;
  double stopElapsedS_ = 0.0;
  double stopResendS_ = 0.0;
  double ackWaitS_ = 0.0;
  bool awaitingAck_ = false;
  Pending pending_ = Pending::None;
  int lastSentRpm_ = -1;  // -1 = 尚未下发过任何速度帧

  std::uint8_t faultBits_ = 0;
  std::uint8_t tempC_ = 0;
  std::uint16_t reportedRpm_ = 0;
  bool boardMotorRunning_ = false;
  bool stopByTimer_ = false;

  std::function<void(HeadEvent)> eventCb_;
};

}  // namespace massage::core
