#pragma once
// BusScheduler —— RS485 半双工总线调度（决策 D5）。
// 严格一问一答：同一时刻最多一个在途事务；控制帧插队优先；
// 超时重试 3 次（协议要求间隔 ≥2ms）；连续 5 个轮询周期无应答判离线。
// 纯逻辑：时钟（IClock）与传输（ITransport）注入，完整可离线单测。
//
// 轮询预算（57600bps，8 字节帧 ≈1.4ms 单向）：
//   每头 0x58 状态 500ms + 运行中 0x56 转速 1s → 6 头全轮询周期 <100ms，余量充足。
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

#include "core/frame.h"
#include "core/frame_codec.h"

namespace massage::core {

// 毫秒时钟。生产 = steady_clock 适配器；测试 = 手动推进的 FakeClock。
struct IClock {
  virtual ~IClock() = default;
  virtual std::int64_t nowMs() const = 0;
};

// 传输抽象。生产 = QSerialPort 适配器（M3 接入）；测试 = MockTransport。
struct ITransport {
  virtual ~ITransport() = default;
  virtual bool send(const Frame& f) = 0;
};

struct BusConfig {
  int statusPollMs = 500;      // 0x58 状态轮询间隔/头
  int speedPollMs = 1000;      // 0x56 转速轮询间隔/头（仅 speedPoll 开启的头）
  int responseTimeoutMs = 50;  // 单事务应答超时
  int maxRetries = 3;          // 超时后重试次数
  int frameGapMs = 2;          // 帧间最小间隔（协议：异常后等 2ms 再发）
  int offlineAfterFails = 5;   // 连续失败事务数 → 判离线
};

class BusScheduler {
 public:
  BusScheduler(BusConfig cfg, ITransport& transport, const IClock& clock);

  // 绑定/维护后更新设备表（重置轮询计划与在位状态）
  void setDevices(std::vector<std::uint8_t> addrs);
  // 运行中的头开启转速轮询（应用层按 HeadState 联动）
  void setSpeedPollEnabled(std::uint8_t addr, bool enabled);

  using ReplyCallback =
      std::function<void(bool ok, Reply header, const ReplyPayload& payload)>;

  // 控制帧插队（0x55/0xAA/0x54/0x5D），cb 在收到应答或事务最终失败时回调
  void enqueueControl(const Frame& f, ReplyCallback cb = nullptr);

  // 传输入口：把 StreamFramer 校验通过的应答帧喂进来
  void onReply(const Frame& validatedReply);

  // 周期驱动（建议 5–10ms）
  void tick();

  // ---- 事件出口 ----
  std::function<void(std::uint8_t addr, bool online)> onPresenceChange;
  // 所有成功应答的统一路由（应用层分发到 TreatmentHead::onAck/onInfo/onSpeed）
  std::function<void(std::uint8_t addr, Reply header, const ReplyPayload& payload)>
      onReplyRouted;
  std::function<void(std::uint8_t addr)> onPollError;  // 单事务重试耗尽

  // ---- 诊断 ----
  int consecutiveFails(std::uint8_t addr) const;
  bool deviceOnline(std::uint8_t addr) const;
  bool hasPending() const;
  std::size_t ignoredReplyCount() const noexcept { return ignoredReplies_; }

 private:
  struct DevSlot {
    std::uint8_t addr = 0;
    std::int64_t nextStatusDue = 0;
    std::int64_t nextSpeedDue = 0;
    int fails = 0;
    bool online = false;
    bool speedPoll = false;
  };
  struct Active {
    Frame frame{};
    Reply expect = Reply::Ack;
    std::uint8_t addr = 0;
    int attempt = 0;
    std::int64_t sentAt = 0;
    ReplyCallback cb;
  };
  struct CtrlItem {
    Frame frame{};
    std::optional<Reply> expect;  // nullopt = 无应答指令（0x60 等），发出即完成
    std::uint8_t addr = 0;
    ReplyCallback cb;
  };

  static std::optional<Reply> expectedReplyFor(std::uint8_t cmdHeader) noexcept;
  DevSlot* find(std::uint8_t addr) noexcept;
  const DevSlot* find(std::uint8_t addr) const noexcept;
  bool startTransaction(const Frame& f, Reply expect, std::uint8_t addr,
                        ReplyCallback cb);
  void failActive();
  void succeedActive(Reply header, const ReplyPayload& payload);

  BusConfig cfg_;
  ITransport& transport_;
  const IClock& clock_;
  std::vector<DevSlot> devs_;
  std::deque<CtrlItem> ctrlQueue_;
  std::optional<Active> active_;
  std::int64_t lastTxAt_ = -1000;
  std::size_t ignoredReplies_ = 0;
};

}  // namespace massage::core
