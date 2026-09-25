#include "core/head_state.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace massage::core {

namespace {

// 决策 D4：曲线目标变化 ≥30RPM 才重新下发，避免总线拥塞
constexpr double kRpmResendThreshold = 30.0;
// 转速低于此值视为已停稳（协议：<50 即 0 速）
constexpr double kStopRpmThreshold = 50.0;
// 应答丢失判定（板子可能已执行，绝不因丢 ack 回退到「电机在转而 UI 显示停」的状态）
constexpr double kAckTimeoutS = 1.0;
// 停止确认超时：滑行停不下来也强制回 Idle（板载堵转保护 2s 兜底）
constexpr double kStopTimeoutS = 5.0;
// 缓启动爬升窗口（0x10 ≈ 2.5s 到 3000RPM，留裕量）
constexpr double kStartRampTimeoutS = 6.0;
// Stopping 期间停止帧重发节流
constexpr double kStopResendS = 1.0;
constexpr int kMinTimeMin = 5;
constexpr int kMaxTimeMin = 60;

}  // namespace

TreatmentHead::TreatmentHead(std::string slotId, std::uint8_t addr)
    : slotId_(std::move(slotId)), addr_(addr) {}

void TreatmentHead::fire(HeadEvent e) {
  if (eventCb_) eventCb_(e);
}

std::uint16_t TreatmentHead::profileCommandRpm() const {
  return profile::targetRpm(config_.mode, config_.freqHz, profileElapsedS_);
}

std::uint16_t TreatmentHead::currentTargetRpm() const { return profileCommandRpm(); }

std::optional<Frame> TreatmentHead::makeRunFrame(std::uint16_t rpm) const {
  if (rpm == 0) return encodeCoastStop(addr_, config_.dir);  // 滑行段
  return encodeMotorCtrl(addr_, rpm, /*closedLoop=*/true, config_.dir);
}

int TreatmentHead::remainingSeconds() const noexcept {
  return static_cast<int>(remainS_);
}
int TreatmentHead::totalSeconds() const noexcept {
  return static_cast<int>(totalS_);
}

void TreatmentHead::resetTimers() {
  totalS_ = 0.0;
  remainS_ = 0.0;
  profileElapsedS_ = 0.0;
  startingElapsedS_ = 0.0;
  stopElapsedS_ = 0.0;
  stopResendS_ = 0.0;
  ackWaitS_ = 0.0;
  awaitingAck_ = false;
  pending_ = Pending::None;
  lastSentRpm_ = -1;
}

bool TreatmentHead::advanceCountdown(double dt) {
  remainS_ -= dt;
  if (remainS_ <= 0.0) {
    remainS_ = 0.0;
    return true;
  }
  return false;
}

std::optional<Frame> TreatmentHead::maybeEmitProfile() {
  if (awaitingAck_) return std::nullopt;
  const std::uint16_t rpm = profileCommandRpm();
  // 本次治疗尚未出过力且曲线仍在滑行段（扫频起点）：无帧可发
  if (lastSentRpm_ < 0 && rpm == 0) return std::nullopt;
  if (lastSentRpm_ >= 0 &&
      std::fabs(static_cast<double>(rpm) - static_cast<double>(lastSentRpm_)) <
          kRpmResendThreshold)
    return std::nullopt;
  lastSentRpm_ = static_cast<int>(rpm);
  awaitingAck_ = true;
  ackWaitS_ = 0.0;
  pending_ = Pending::Profile;
  return makeRunFrame(rpm);
}

// ---------------- 设备事件 ----------------

void TreatmentHead::onPresence(bool online) {
  if (online == online_) {
    if (online && state_ == HeadState::Absent) state_ = HeadState::Idle;
    return;
  }
  online_ = online;
  if (!online) {
    if (state_ == HeadState::Absent || state_ == HeadState::Idle) {
      state_ = HeadState::Absent;
      awaitingAck_ = false;
      pending_ = Pending::None;
    } else {
      // 治疗中失联：状态冻结（tick 停摆），UI 必须醒目告警——
      // 电机可能仍在按最后指令运转，提示操作者使用硬件急停（决策 D5/D6）
      fire(HeadEvent::WentOfflineDuringRun);
    }
  } else {
    if (state_ == HeadState::Absent) state_ = HeadState::Idle;
  }
}

void TreatmentHead::onAck(AckFlag flag) {
  if (!awaitingAck_) return;  // 迟到/重复应答
  awaitingAck_ = false;
  if (flag == AckFlag::Ok) {
    if (pending_ == Pending::Reset) {
      if (state_ == HeadState::Fault) {
        state_ = HeadState::Idle;
        faultBits_ = 0;
        resetTimers();
        fire(HeadEvent::FaultCleared);
      }
    } else if (state_ == HeadState::Starting) {
      state_ = HeadState::Running;  // 运行帧已确认，缓启动爬升中即视为治疗进行
    }
    pending_ = Pending::None;
    return;
  }

  // 板子明确拒绝（0xBB 失败 / 0xEE 运行中改地址）：按命令类型安全回退
  fire(HeadEvent::AckRejected);
  switch (pending_) {
    case Pending::Start:
      if (state_ == HeadState::Starting) {
        state_ = HeadState::Idle;
        resetTimers();
      }
      break;
    case Pending::Pause:
      if (state_ == HeadState::Paused) state_ = HeadState::Running;  // 没停成就继续计时
      break;
    case Pending::Resume:
      if (state_ == HeadState::Running) state_ = HeadState::Paused;  // 没启成就保持暂停
      break;
    case Pending::Stop:
      break;  // 停止是安全动作：留在 Stopping，tick 会持续重发直到停稳
    case Pending::Reset:
      break;  // 复位被拒：留在 Fault，操作者排查后重试
    case Pending::Profile:
    case Pending::None:
      break;  // 曲线帧被拒：不重发本点，等下一个变化点
  }
  if (pending_ != Pending::Stop) pending_ = Pending::None;
}

void TreatmentHead::onInfo(std::uint8_t faultRawBits, std::uint8_t tempC,
                           bool motorRunning) {
  tempC_ = tempC;
  boardMotorRunning_ = motorRunning;
  const auto fb = static_cast<std::uint8_t>(faultRawBits & 0x77);  // 去掉 bit7 运行标志
  if (fb != 0) {
    if (state_ != HeadState::Fault && state_ != HeadState::Absent) {
      state_ = HeadState::Fault;
      faultBits_ = fb;
      awaitingAck_ = false;
      pending_ = Pending::None;
      fire(HeadEvent::FaultEntered);
    } else if (state_ == HeadState::Fault) {
      faultBits_ = fb;  // 故障态内更新故障码
    }
  }
  // fb==0 不自动离开 Fault —— 决策 D3：必须手动复位后才能重新启动
}

void TreatmentHead::onSpeed(std::uint32_t rpm) {
  reportedRpm_ = rpm > 0xFFFFU ? 0xFFFFU : static_cast<std::uint16_t>(rpm);
}

// ---------------- UI 命令 ----------------

std::optional<Frame> TreatmentHead::start() {
  if (!online_ || state_ != HeadState::Idle) return std::nullopt;
  totalS_ = config_.timeMin * 60.0;
  remainS_ = totalS_;
  profileElapsedS_ = 0.0;
  startingElapsedS_ = 0.0;
  stopElapsedS_ = 0.0;
  stopResendS_ = 0.0;
  ackWaitS_ = 0.0;
  state_ = HeadState::Starting;
  fire(HeadEvent::Started);

  const std::uint16_t rpm = profileCommandRpm();
  if (rpm == 0) {
    // 扫频起点处于滑行段：无有效目标可发，首个 ≥100RPM 的目标由 tick 下发
    awaitingAck_ = false;
    pending_ = Pending::None;
    return std::nullopt;
  }
  awaitingAck_ = true;
  pending_ = Pending::Start;
  lastSentRpm_ = static_cast<int>(rpm);
  return makeRunFrame(rpm);
}

std::optional<Frame> TreatmentHead::pause() {
  if (!online_) return std::nullopt;
  if (state_ != HeadState::Running && state_ != HeadState::Starting)
    return std::nullopt;
  state_ = HeadState::Paused;
  awaitingAck_ = true;
  ackWaitS_ = 0.0;
  pending_ = Pending::Pause;
  lastSentRpm_ = 0;
  fire(HeadEvent::Paused);
  return encodeCoastStop(addr_, config_.dir);  // 滑行停止（D3）
}

std::optional<Frame> TreatmentHead::resume() {
  if (!online_ || state_ != HeadState::Paused) return std::nullopt;
  state_ = HeadState::Running;
  awaitingAck_ = true;
  ackWaitS_ = 0.0;
  pending_ = Pending::Resume;
  fire(HeadEvent::Resumed);
  const std::uint16_t rpm = profileCommandRpm();
  lastSentRpm_ = static_cast<int>(rpm);
  return makeRunFrame(rpm);  // 缓启动由板载 0x10 参数保证（D3）；曲线谷底则发滑行帧
}

std::optional<Frame> TreatmentHead::stop() {
  if (!online_) return std::nullopt;
  switch (state_) {
    case HeadState::Starting:
    case HeadState::Running:
    case HeadState::Paused:
      break;
    default:
      return std::nullopt;
  }
  state_ = HeadState::Stopping;
  stopElapsedS_ = 0.0;
  stopResendS_ = 0.0;
  stopByTimer_ = false;
  awaitingAck_ = true;
  ackWaitS_ = 0.0;
  pending_ = Pending::Stop;
  lastSentRpm_ = 0;
  fire(HeadEvent::StopRequested);
  return encodeCoastStop(addr_, config_.dir);
}

std::optional<Frame> TreatmentHead::resetFault() {
  if (!online_ || state_ != HeadState::Fault) return std::nullopt;
  awaitingAck_ = true;
  ackWaitS_ = 0.0;
  pending_ = Pending::Reset;
  return encodeFaultReset(addr_, config_.dir == MotorStatus::DirB);
}

bool TreatmentHead::applyConfig(const HeadConfig& c) {
  if (c.freqHz < kMinFreqHz || c.freqHz > kMaxFreqHz) return false;
  if (c.timeMin < kMinTimeMin || c.timeMin > kMaxTimeMin) return false;
  // 运行中禁止改方向（机械/电气风险）：仅待机、缺位、故障态允许
  if (c.dir != config_.dir && state_ != HeadState::Idle &&
      state_ != HeadState::Absent && state_ != HeadState::Fault)
    return false;

  const bool inSession = state_ == HeadState::Starting ||
                         state_ == HeadState::Running ||
                         state_ == HeadState::Paused ||
                         state_ == HeadState::Stopping;
  if (inSession && c.timeMin != config_.timeMin) {
    // 原型语义：时间按差值调整剩余（不清零不重启）
    const double dT = (c.timeMin - config_.timeMin) * 60.0;
    totalS_ += dT;
    remainS_ = std::min(totalS_, std::max(1.0, remainS_ + dT));
  }
  config_ = c;
  return true;
}

// ---------------- 周期驱动 ----------------

std::optional<Frame> TreatmentHead::tick(double dtSeconds) {
  if (!online_ || dtSeconds <= 0.0) return std::nullopt;

  if (awaitingAck_) {
    ackWaitS_ += dtSeconds;
    if (ackWaitS_ >= kAckTimeoutS) {
      // 应答丢失 ≠ 被拒：板子可能已执行。只解除发送闭锁并强制重发当前目标，
      // 绝不回退状态（防止「UI 显示停、电机在转」）
      awaitingAck_ = false;
      if (pending_ == Pending::Start || pending_ == Pending::Profile)
        lastSentRpm_ = -1;
    }
  }

  switch (state_) {
    case HeadState::Starting: {
      startingElapsedS_ += dtSeconds;
      profileElapsedS_ += dtSeconds;
      if (advanceCountdown(dtSeconds)) {  // 极端：时间设太短还没爬升就归零
        state_ = HeadState::Stopping;
        stopElapsedS_ = 0.0;
        stopResendS_ = 0.0;
        stopByTimer_ = true;
        awaitingAck_ = true;
        ackWaitS_ = 0.0;
        pending_ = Pending::Stop;
        lastSentRpm_ = 0;
        fire(HeadEvent::TimerExpired);
        return encodeCoastStop(addr_, config_.dir);
      }
      if (startingElapsedS_ >= kStartRampTimeoutS)
        state_ = HeadState::Running;  // 爬升窗口结束（板载保护兜底异常）
      return maybeEmitProfile();
    }

    case HeadState::Running: {
      profileElapsedS_ += dtSeconds;
      if (advanceCountdown(dtSeconds)) {
        state_ = HeadState::Stopping;
        stopElapsedS_ = 0.0;
        stopResendS_ = 0.0;
        stopByTimer_ = true;
        awaitingAck_ = true;
        ackWaitS_ = 0.0;
        pending_ = Pending::Stop;
        lastSentRpm_ = 0;
        fire(HeadEvent::TimerExpired);
        return encodeCoastStop(addr_, config_.dir);
      }
      return maybeEmitProfile();
    }

    case HeadState::Paused:
      // 倒计时/曲线全冻结；若暂停帧始终未确认，每秒重发滑行停止直到板子应答
      if (pending_ == Pending::Pause && !awaitingAck_) {
        awaitingAck_ = true;
        ackWaitS_ = 0.0;
        return encodeCoastStop(addr_, config_.dir);
      }
      return std::nullopt;

    case HeadState::Stopping: {
      stopElapsedS_ += dtSeconds;
      stopResendS_ += dtSeconds;
      if (reportedRpm_ < kStopRpmThreshold || stopElapsedS_ >= kStopTimeoutS) {
        state_ = HeadState::Idle;
        if (stopByTimer_) fire(HeadEvent::Completed);
        stopByTimer_ = false;
        resetTimers();
        return std::nullopt;
      }
      if (!awaitingAck_ && stopResendS_ >= kStopResendS) {
        stopResendS_ = 0.0;
        awaitingAck_ = true;
        ackWaitS_ = 0.0;
        pending_ = Pending::Stop;
        return encodeCoastStop(addr_, config_.dir);
      }
      return std::nullopt;
    }

    case HeadState::Fault:    // 冻结，等手动复位（D3）
    case HeadState::Idle:
    case HeadState::Absent:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace massage::core
