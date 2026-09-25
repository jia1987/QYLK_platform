#include "core/bus_scheduler.h"

#include <utility>

namespace massage::core {

BusScheduler::BusScheduler(BusConfig cfg, ITransport& transport,
                           const IClock& clock)
    : cfg_(cfg), transport_(transport), clock_(clock) {}

void BusScheduler::setDevices(std::vector<std::uint8_t> addrs) {
  devs_.clear();
  ctrlQueue_.clear();
  active_.reset();
  const std::int64_t now = clock_.nowMs();
  for (std::uint8_t a : addrs) {
    DevSlot d;
    d.addr = a;
    d.nextStatusDue = now;  // 立即开始首轮探测
    d.nextSpeedDue = now;
    devs_.push_back(d);
  }
}

void BusScheduler::setSpeedPollEnabled(std::uint8_t addr, bool enabled) {
  if (DevSlot* d = find(addr)) {
    d->speedPoll = enabled;
    d->nextSpeedDue = clock_.nowMs();
  }
}

std::optional<Reply> BusScheduler::expectedReplyFor(std::uint8_t cmdHeader) noexcept {
  switch (cmdHeader) {
    case 0x55:
    case 0x54:
    case 0xAA:
    case 0x5D:
      return Reply::Ack;
    case 0x56:
      return Reply::Speed;
    case 0x57:
      return Reply::Position;
    case 0x58:
      return Reply::Info;
    default:
      return std::nullopt;  // 0x60 广播等无应答指令不由调度器管理
  }
}

BusScheduler::DevSlot* BusScheduler::find(std::uint8_t addr) noexcept {
  for (DevSlot& d : devs_)
    if (d.addr == addr) return &d;
  return nullptr;
}

const BusScheduler::DevSlot* BusScheduler::find(std::uint8_t addr) const noexcept {
  for (const DevSlot& d : devs_)
    if (d.addr == addr) return &d;
  return nullptr;
}

void BusScheduler::enqueueControl(const Frame& f, ReplyCallback cb) {
  // 无应答指令（如广播 0x60，本软件按 D5 不使用）也入队，由 tick 按序发出，
  // 绝不绕过半双工事务直接写串口
  ctrlQueue_.push_back(CtrlItem{f, expectedReplyFor(f[0]), f[2], std::move(cb)});
}

bool BusScheduler::startTransaction(const Frame& f, Reply expect,
                                    std::uint8_t addr, ReplyCallback cb) {
  if (!transport_.send(f)) return false;
  Active a;
  a.frame = f;
  a.expect = expect;
  a.addr = addr;
  a.attempt = 0;
  a.sentAt = clock_.nowMs();
  a.cb = std::move(cb);
  lastTxAt_ = a.sentAt;
  active_ = a;
  return true;
}

void BusScheduler::failActive() {
  if (!active_) return;
  const std::uint8_t addr = active_->addr;
  ReplyCallback cb = std::move(active_->cb);
  active_.reset();
  if (DevSlot* d = find(addr)) {
    d->fails++;
    const std::int64_t now = clock_.nowMs();
    d->nextStatusDue = now + cfg_.statusPollMs;
    d->nextSpeedDue = now + cfg_.speedPollMs;
    if (d->online && d->fails >= cfg_.offlineAfterFails) {
      d->online = false;
      if (onPresenceChange) onPresenceChange(addr, false);
    }
  }
  if (onPollError) onPollError(addr);
  if (cb) cb(false, Reply::Ack, ReplyPayload{});
}

void BusScheduler::succeedActive(Reply header, const ReplyPayload& payload) {
  if (!active_) return;
  const std::uint8_t addr = active_->addr;
  ReplyCallback cb = std::move(active_->cb);
  active_.reset();
  const std::int64_t now = clock_.nowMs();
  if (DevSlot* d = find(addr)) {
    d->fails = 0;
    if (!d->online) {
      d->online = true;
      if (onPresenceChange) onPresenceChange(addr, true);
    }
    // 按应答类型刷新对应轮询到期时间
    if (header == Reply::Speed)
      d->nextSpeedDue = now + cfg_.speedPollMs;
    else
      d->nextStatusDue = now + cfg_.statusPollMs;
  }
  if (onReplyRouted) onReplyRouted(addr, header, payload);
  if (cb) cb(true, header, payload);
}

void BusScheduler::onReply(const Frame& validatedReply) {
  Reply h;
  ReplyPayload p;
  if (!parseReply(validatedReply, h, p)) {
    ignoredReplies_++;
    return;
  }
  const std::uint8_t addr = validatedReply[2];
  // 严格匹配在途事务：头码类型 + 地址。不匹配 = 噪声/串扰/迟到帧，丢弃
  if (active_ && active_->expect == h && active_->addr == addr) {
    succeedActive(h, p);
    return;
  }
  // 无在途事务时的合法应答（如控制帧应答在超时后才到）：仍路由给应用层，
  // 保证 onInfo/onSpeed 数据不丢，但不影响轮询计划
  if (!active_) {
    if (DevSlot* d = find(addr)) {
      if (!d->online) {
        d->online = true;
        d->fails = 0;
        if (onPresenceChange) onPresenceChange(addr, true);
      }
    }
    if (onReplyRouted) onReplyRouted(addr, h, p);
    return;
  }
  ignoredReplies_++;
}

void BusScheduler::tick() {
  const std::int64_t now = clock_.nowMs();

  // 1) 在途事务：超时则重试/失败
  if (active_) {
    if (now - active_->sentAt >= cfg_.responseTimeoutMs) {
      if (active_->attempt < cfg_.maxRetries) {
        active_->attempt++;
        if (transport_.send(active_->frame)) {
          active_->sentAt = now;
          lastTxAt_ = now;
        } else {
          failActive();
        }
      } else {
        failActive();
      }
    }
    return;
  }

  // 帧间间隔（协议 2ms）
  if (now - lastTxAt_ < cfg_.frameGapMs) return;

  // 2) 控制帧插队优先
  while (!ctrlQueue_.empty()) {
    CtrlItem item = std::move(ctrlQueue_.front());
    ctrlQueue_.pop_front();
    ReplyCallback cb = item.cb;  // 拷贝而非移动：发送失败路径也要回调通知
    if (!item.expect) {
      // 无应答指令：发出即完成，不建事务（每 tick 至多一次发送动作）
      const bool sent = transport_.send(item.frame);
      if (sent) lastTxAt_ = now;
      if (cb) cb(sent, Reply::Ack, ReplyPayload{});
      return;
    }
    if (startTransaction(item.frame, *item.expect, item.addr, cb)) return;
    if (cb) cb(false, Reply::Ack, ReplyPayload{});
  }

  // 3) 轮询计划：选最过期的请求
  DevSlot* best = nullptr;
  bool bestSpeed = false;
  std::int64_t bestDue = now + 1;
  for (DevSlot& d : devs_) {
    if (d.nextStatusDue <= now && d.nextStatusDue < bestDue) {
      best = &d;
      bestSpeed = false;
      bestDue = d.nextStatusDue;
    }
    if (d.speedPoll && d.online && d.nextSpeedDue <= now &&
        d.nextSpeedDue < bestDue) {
      best = &d;
      bestSpeed = true;
      bestDue = d.nextSpeedDue;
    }
  }
  if (!best) return;

  const Cmd readCmd = bestSpeed ? Cmd::ReadSpeed : Cmd::ReadInfo;
  const Reply expect = bestSpeed ? Reply::Speed : Reply::Info;
  const std::uint8_t addr = best->addr;
  const auto frame = encodeRead(readCmd, addr);
  if (!frame) return;  // 不可达（readCmd 恒为读取指令）

  // 乐观刷新到期时间；发送/事务失败路径会再次推后
  if (bestSpeed)
    best->nextSpeedDue = now + cfg_.speedPollMs;
  else
    best->nextStatusDue = now + cfg_.statusPollMs;

  if (!startTransaction(*frame, expect, addr, nullptr)) {
    // 传输层故障：到期时间已推后，下个 tick 重试，不会热循环
  }
}

int BusScheduler::consecutiveFails(std::uint8_t addr) const {
  const DevSlot* d = find(addr);
  return d ? d->fails : -1;
}

bool BusScheduler::deviceOnline(std::uint8_t addr) const {
  const DevSlot* d = find(addr);
  return d && d->online;
}

bool BusScheduler::hasPending() const {
  return active_.has_value() || !ctrlQueue_.empty();
}

}  // namespace massage::core
