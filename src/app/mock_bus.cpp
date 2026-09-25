#include "mock_bus.h"

#include <cmath>

#include "core/crc16.h"
#include "core/frame_codec.h"

namespace massage::app {

using namespace core;

namespace {
bool masterFrameOk(const Frame& f) {
  const std::uint16_t crc = crc16Modbus(f.data(), kFrameLen - 2);
  return f[1] == 0x08 && f[6] == (crc & 0xFF) && f[7] == ((crc >> 8) & 0xFF);
}
}  // namespace

MockBus::MockBus(std::vector<std::uint8_t> presentAddrs, QObject* parent)
    : QObject(parent) {
  for (std::uint8_t a : presentAddrs) {
    MHead h;
    h.addr = a;
    heads_[a] = h;
  }
  deliverTimer_.setInterval(5);
  physicsTimer_.setInterval(50);
  connect(&deliverTimer_, &QTimer::timeout, this, [this] {
    if (outQueue_.empty()) return;
    const Frame f = outQueue_.front();
    outQueue_.pop();
    if (replySink) replySink(f);
  });
  connect(&physicsTimer_, &QTimer::timeout, this, [this] {
    const double dt = 0.05;
    for (auto& kv : heads_) {
      MHead& h = kv.second;
      if (h.fault) h.targetRpm = 0;
      const double accel = 20000.0 / (h.soft + 1);
      if (h.braking) {
        h.rpm = std::max(0.0, h.rpm - 8000.0 * dt);
        if (h.rpm <= 0.0) h.braking = false;
      } else {
        const double target = static_cast<double>(h.targetRpm);
        if (h.rpm < target)
          h.rpm = std::min(target, h.rpm + accel * dt);
        else if (h.rpm > target)
          h.rpm = std::max(target, h.rpm - accel * 2.0 * dt);
      }
      const double sign = h.dir ? -1.0 : 1.0;
      h.position += sign * h.rpm / 60.0 * dt * 6.0 * h.polePairs;
      if (h.position > 8388607.0) h.position -= 16777216.0;
      if (h.position < -8388608.0) h.position += 16777216.0;
      const double load = 25.0 * h.rpm / static_cast<double>(kMaxSpeedValue);
      const double targetTemp = 32.0 + load;
      h.temp += (targetTemp - h.temp) * dt * 0.05;
    }
  });
  deliverTimer_.start();
  physicsTimer_.start();
}

void MockBus::setPresent(std::uint8_t addr, bool present) {
  auto it = heads_.find(addr);
  if (it != heads_.end()) {
    it->second.present = present;
    if (!present) {
      it->second.rpm = 0.0;
      it->second.targetRpm = 0;
    }
  }
}

bool MockBus::send(const Frame& f) {
  if (!masterFrameOk(f)) return true;  // 坏帧：静默丢弃（协议规定无应答）
  handle(f);
  return true;
}

void MockBus::applyCtrl(MHead& h, int value, int status) {
  if (status == 0x02 || status == 0x03) {  // 故障复位
    h.fault = 0;
    h.braking = false;
    h.dir = status & 0x01;
  }
  if (status == 0x04) {  // 刹车
    h.braking = true;
    h.targetRpm = 0;
    return;
  }
  if (status == 0x00 || status == 0x01) h.dir = status;
  const bool closed = (value & kClosedLoopBit) != 0;
  const int mag = value & 0x0FFF;
  if (mag < static_cast<int>(kCoastThreshold))
    h.targetRpm = 0;  // 滑行
  else if (closed && mag >= static_cast<int>(kMinClosedLoopRpm))
    h.targetRpm = std::min(mag, static_cast<int>(kMaxSpeedValue));
  else if (!closed)
    h.targetRpm = mag;
  h.braking = false;
}

void MockBus::enqueueReply(std::uint8_t header, std::uint8_t addr,
                           std::uint8_t b3, std::uint8_t b4, std::uint8_t b5) {
  outQueue_.push(buildFrame(header, 0x08, addr, b3, b4, b5));
}

void MockBus::handle(const Frame& f) {
  const std::uint8_t cmd = f[0];
  const std::uint8_t addr = f[2];

  if (cmd == static_cast<std::uint8_t>(Cmd::Broadcast)) {
    if (addr == 0xEE)
      for (auto& kv : heads_)
        if (kv.second.present)
          applyCtrl(kv.second, (f[3] << 8) | f[4], f[5]);
    return;  // 广播无应答
  }

  // 0xAA 地址配置：真实语义是「谁收到谁改」（协议严禁上总线广播的原因）。
  // 模拟器忠实复现：所有在位头都会执行——维护联锁（其余头离线）因此可被测到。
  if (cmd == static_cast<std::uint8_t>(Cmd::ConfigAddress)) {
    const std::uint8_t newAddr = addr;
    std::vector<std::uint8_t> presentAddrs;
    for (const auto& kv : heads_)
      if (kv.second.present) presentAddrs.push_back(kv.first);
    for (std::uint8_t oldA : presentAddrs) {
      auto it = heads_.find(oldA);
      if (it == heads_.end()) continue;
      MHead h = it->second;
      if (h.rpm > 5.0) {  // 运行中拒绝改地址
        enqueueReply(0x11, oldA, 0xEE, 0x00, 0x00);
        continue;
      }
      h.closedLoop = (f[3] & 0x10) != 0;
      h.polePairs = f[5];
      if (newAddr != oldA) {
        heads_.erase(it);
        h.addr = newAddr;
        heads_[newAddr] = h;
      } else {
        it->second = h;
      }
      enqueueReply(0x11, newAddr, 0xAA, 0x00, 0x00);
    }
    return;
  }

  auto it = heads_.find(addr);
  if (it == heads_.end() || !it->second.present) return;  // 不在位：静默
  MHead& h = it->second;

  switch (cmd) {
    case static_cast<std::uint8_t>(Cmd::MotorCtrl): {
      const int status = f[5];
      if (h.fault && status != 0x02 && status != 0x03 && status != 0x04) {
        // 故障中：运行指令应答成功但不执行（与 Python 模拟器一致）
      } else {
        applyCtrl(h, (f[3] << 8) | f[4], status);
      }
      enqueueReply(0x11, addr, 0xAA, 0x00, 0x00);
      break;
    }
    case static_cast<std::uint8_t>(Cmd::ConfigParams):
      h.soft = f[4];
      enqueueReply(0x11, addr, 0xAA, 0x00, 0x00);
      break;
    case static_cast<std::uint8_t>(Cmd::HallCorrect):
      enqueueReply(0x11, addr, 0xAA, 0x00, 0x01);
      break;
    case static_cast<std::uint8_t>(Cmd::ReadSpeed): {
      const auto v = static_cast<std::uint32_t>(std::llround(h.rpm));
      enqueueReply(0x12, addr, static_cast<std::uint8_t>((v >> 16) & 0xFF),
                   static_cast<std::uint8_t>((v >> 8) & 0xFF),
                   static_cast<std::uint8_t>(v & 0xFF));
      break;
    }
    case static_cast<std::uint8_t>(Cmd::ReadPosition): {
      auto v = static_cast<std::int32_t>(std::llround(h.position)) & 0xFFFFFF;
      enqueueReply(0x13, addr, static_cast<std::uint8_t>((v >> 16) & 0xFF),
                   static_cast<std::uint8_t>((v >> 8) & 0xFF),
                   static_cast<std::uint8_t>(v & 0xFF));
      break;
    }
    case static_cast<std::uint8_t>(Cmd::ReadInfo): {
      const bool running = h.rpm > 5.0;
      const auto fb = static_cast<std::uint8_t>(h.fault | (running ? 0x80 : 0x00));
      enqueueReply(0x14, addr, fb, static_cast<std::uint8_t>(h.temp),
                   running ? 0x01 : 0x00);
      break;
    }
    default:
      break;  // 未知指令：静默
  }
}

}  // namespace massage::app
