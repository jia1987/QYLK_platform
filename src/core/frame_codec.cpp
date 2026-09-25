#include "core/frame_codec.h"

#include <cstring>

#include "core/crc16.h"

namespace massage::core {

namespace {

void appendCrc(Frame& f) noexcept {
  const std::uint16_t crc = crc16Modbus(f.data(), kFrameLen - 2);
  f[6] = static_cast<std::uint8_t>(crc & 0xFF);         // CRCL 低字节在前
  f[7] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);  // CRCH
}

bool isReplyHeader(std::uint8_t b) noexcept {
  return b >= static_cast<std::uint8_t>(Reply::Ack) &&
         b <= static_cast<std::uint8_t>(Reply::Info);  // 0x11..0x14
}

}  // namespace

Frame buildFrame(std::uint8_t header, std::uint8_t len, std::uint8_t b2,
                 std::uint8_t b3, std::uint8_t b4, std::uint8_t b5) noexcept {
  Frame f{header, len, b2, b3, b4, b5, 0, 0};
  appendCrc(f);
  return f;
}

// ---- 主机指令编码 ----

Frame encodeConfigAddress(std::uint8_t addr, bool closedLoop,
                          std::uint8_t stallTime, std::uint8_t polePairs) noexcept {
  // 第4字节：0x00 有感方波开环 / 0x10 有感方波闭环
  return buildFrame(static_cast<std::uint8_t>(Cmd::ConfigAddress), 0x08, addr,
                    closedLoop ? 0x10 : 0x00, stallTime, polePairs);
}

Frame encodeConfigParams(std::uint8_t addr, std::uint8_t currentLimit,
                         std::uint8_t softStart, std::uint8_t dirDelay) noexcept {
  return buildFrame(static_cast<std::uint8_t>(Cmd::ConfigParams), 0x08, addr,
                    currentLimit, softStart, dirDelay);
}

Frame encodeHallCorrect(std::uint8_t addr) noexcept {
  return buildFrame(static_cast<std::uint8_t>(Cmd::HallCorrect), 0x08, addr,
                    0x00, 0x00, 0x00);
}

std::optional<Frame> encodeMotorCtrl(std::uint8_t addr, std::uint16_t speedValue,
                                     bool closedLoop, MotorStatus status) noexcept {
  // 防御性校验：越界拒绝编码，绝不静默钳位（数值回绕=飞车风险）
  if (closedLoop) {
    if (speedValue < kMinClosedLoopRpm || speedValue > kMaxSpeedValue)
      return std::nullopt;
    speedValue = static_cast<std::uint16_t>(speedValue | kClosedLoopBit);
  } else {
    if (speedValue < kCoastThreshold || speedValue > kMaxSpeedValue)
      return std::nullopt;
  }
  return buildFrame(static_cast<std::uint8_t>(Cmd::MotorCtrl), 0x08, addr,
                    static_cast<std::uint8_t>(speedValue >> 8),
                    static_cast<std::uint8_t>(speedValue & 0xFF),
                    static_cast<std::uint8_t>(status));
}

std::optional<Frame> encodeRunHz(std::uint8_t addr, int freqHz,
                                 MotorStatus status) noexcept {
  if (freqHz < kMinFreqHz || freqHz > kMaxFreqHz) return std::nullopt;
  return encodeMotorCtrl(addr, hzToRpm(freqHz), /*closedLoop=*/true, status);
}

Frame encodeCoastStop(std::uint8_t addr, MotorStatus dir) noexcept {
  // 速度 0（<50 即 0 速）：电机惯性滑行停止（决策 D3）
  return buildFrame(static_cast<std::uint8_t>(Cmd::MotorCtrl), 0x08, addr, 0x00,
                    0x00, static_cast<std::uint8_t>(dir));
}

Frame encodeFaultReset(std::uint8_t addr, bool dirB) noexcept {
  return buildFrame(static_cast<std::uint8_t>(Cmd::MotorCtrl), 0x08, addr, 0x00,
                    0x00,
                    static_cast<std::uint8_t>(dirB ? MotorStatus::ResetB
                                                   : MotorStatus::ResetA));
}

Frame encodeBrake(std::uint8_t addr) noexcept {
  return buildFrame(static_cast<std::uint8_t>(Cmd::MotorCtrl), 0x08, addr, 0x00,
                    0x00, static_cast<std::uint8_t>(MotorStatus::Brake));
}

std::optional<Frame> encodeRead(Cmd readCmd, std::uint8_t addr) noexcept {
  const auto h = static_cast<std::uint8_t>(readCmd);
  if (h != static_cast<std::uint8_t>(Cmd::ReadSpeed) &&
      h != static_cast<std::uint8_t>(Cmd::ReadPosition) &&
      h != static_cast<std::uint8_t>(Cmd::ReadInfo))
    return std::nullopt;
  return buildFrame(h, 0x08, addr, 0x33, 0x33, 0x33);
}

// ---- 从机应答解码 ----

bool validateReplyFrame(const Frame& f) noexcept {
  if (!isReplyHeader(f[0])) return false;
  if (f[1] != static_cast<std::uint8_t>(kFrameLen)) return false;
  const std::uint16_t crc = crc16Modbus(f.data(), kFrameLen - 2);
  return f[6] == static_cast<std::uint8_t>(crc & 0xFF) &&
         f[7] == static_cast<std::uint8_t>((crc >> 8) & 0xFF);
}

bool parseReply(const Frame& f, Reply& outHeader,
                ReplyPayload& outPayload) noexcept {
  if (!validateReplyFrame(f)) return false;
  outHeader = static_cast<Reply>(f[0]);
  switch (outHeader) {
    case Reply::Ack: {
      AckReply r;
      r.addr = f[2];
      switch (f[3]) {
        case static_cast<std::uint8_t>(AckFlag::Ok):       r.flag = AckFlag::Ok; break;
        case static_cast<std::uint8_t>(AckFlag::Fail):     r.flag = AckFlag::Fail; break;
        case static_cast<std::uint8_t>(AckFlag::AddrBusy): r.flag = AckFlag::AddrBusy; break;
        default: return false;  // 未知应答标志，视为无效帧
      }
      r.hallState = f[4];
      r.dirHint = f[5];
      outPayload = r;
      return true;
    }
    case Reply::Speed: {
      SpeedReply r;
      r.addr = f[2];
      r.rpm = (static_cast<std::uint32_t>(f[3]) << 16) |
              (static_cast<std::uint32_t>(f[4]) << 8) | f[5];
      outPayload = r;
      return true;
    }
    case Reply::Position: {
      PositionReply r;
      r.addr = f[2];
      std::uint32_t raw = (static_cast<std::uint32_t>(f[3]) << 16) |
                          (static_cast<std::uint32_t>(f[4]) << 8) | f[5];
      // 文档 p12 例程：FG1 >= 8388608 时 FG2 = FG1 - 16777216（24 位补码）
      r.position = raw >= 0x800000U ? static_cast<std::int32_t>(raw) - 0x1000000
                                    : static_cast<std::int32_t>(raw);
      outPayload = r;
      return true;
    }
    case Reply::Info: {
      InfoReply r;
      r.addr = f[2];
      r.faultBits = f[3];
      r.tempC = f[4];
      r.motorRunning = f[5] != 0;
      outPayload = r;
      return true;
    }
  }
  return false;
}

// ---- 字节流同步器 ----

void StreamFramer::feed(const std::uint8_t* data, std::size_t len) {
  buf_.insert(buf_.end(), data, data + len);
}

bool StreamFramer::next(Frame& out) {
  for (std::size_t i = 0; i + kFrameLen <= buf_.size(); ++i) {
    Frame f{};
    std::memcpy(f.data(), buf_.data() + i, kFrameLen);
    if (validateReplyFrame(f)) {
      buf_.erase(buf_.begin(),
                 buf_.begin() + static_cast<std::ptrdiff_t>(i + kFrameLen));
      dropped_ += i;
      out = f;
      return true;
    }
  }
  // 无完整帧：丢弃不可能是帧起始的前缀垃圾，仅保留尾部最多 7 字节等待续传
  if (buf_.size() > kFrameLen - 1) {
    dropped_ += buf_.size() - (kFrameLen - 1);
    buf_.erase(buf_.begin(),
               buf_.end() - static_cast<std::ptrdiff_t>(kFrameLen - 1));
  }
  return false;
}

}  // namespace massage::core
