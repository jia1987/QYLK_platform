// @srs SRS-010 SRS-011 SRS-012 SRS-013 SRS-021 SRS-022
// 协议核心层单元测试 —— 测试向量全部来自：
//   1) 设计方案文档 §2「冻结帧表」（CRC 已经独立 Python 实算核对）
//   2) 协议 PDF 附件例帧与 p11/p12 应答示例（转速 1280RPM、位置 -2784）
// 注册文档链要求：本文件即协议层验证记录的可执行载体（IEC 62304 验证活动）。
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <variant>

#include "core/crc16.h"
#include "core/frame_codec.h"

using namespace massage::core;

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      ++g_fails;                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                   \
  } while (0)

static Frame mk(std::initializer_list<int> bytes) {
  Frame f{};
  std::size_t i = 0;
  for (int b : bytes) {
    if (i < kFrameLen) f[i++] = static_cast<std::uint8_t>(b);
  }
  return f;
}

static std::string hex(const Frame& f) {
  static const char* d = "0123456789ABCDEF";
  std::string s;
  for (std::uint8_t b : f) {
    s += d[b >> 4];
    s += d[b & 0xF];
    s += ' ';
  }
  return s;
}

// 变参宏：expected 是含逗号的初始化列表，必须走 __VA_ARGS__
static void checkFrame(const char* file, int line, const Frame& actual,
                       std::initializer_list<int> expected) {
  ++g_checks;
  const Frame e = mk(expected);
  if (actual != e) {
    ++g_fails;
    std::printf("FAIL %s:%d\n  actual:   %s\n  expected: %s\n", file, line,
                hex(actual).c_str(), hex(e).c_str());
  }
}
#define CHECK_FRAME(actual, ...) \
  checkFrame(__FILE__, __LINE__, (actual), __VA_ARGS__)

// ---------- 1. CRC 算法（协议 PDF 附件例帧，独立核对过） ----------
static void testCrc() {
  const std::uint8_t f1[] = {0xAA, 0x08, 0x01, 0x00, 0x14, 0x02};
  CHECK(crc16Modbus(f1, sizeof f1) == 0xED76);  // 线上：76 ED
  const std::uint8_t f2[] = {0x55, 0x08, 0x01, 0x0B, 0xB9, 0x01};
  CHECK(crc16Modbus(f2, sizeof f2) == 0xB12F);  // 线上：2F B1
}

// ---------- 2. 冻结帧表（设计方案 §2，地址=01） ----------
static void testFrozenFrames() {
  CHECK_FRAME(encodeConfigAddress(1, true, defaults::kStallTime,
                                  defaults::kPolePairs),
              {0xAA, 0x08, 0x01, 0x10, 0x14, 0x02, 0x77, 0x28});
  CHECK_FRAME(encodeConfigParams(1, defaults::kCurrentLimit,
                                 defaults::kSoftStart, defaults::kDirDelay),
              {0x54, 0x08, 0x01, 0x4D, 0x10, 0x00, 0x70, 0x25});
  CHECK_FRAME(encodeRunHz(1, 30, MotorStatus::DirA).value(),
              {0x55, 0x08, 0x01, 0x17, 0x08, 0x00, 0x5B, 0xE7});
  CHECK_FRAME(encodeRunHz(1, 30, MotorStatus::DirB).value(),
              {0x55, 0x08, 0x01, 0x17, 0x08, 0x01, 0x9A, 0x27});
  CHECK_FRAME(encodeCoastStop(1, MotorStatus::DirA),
              {0x55, 0x08, 0x01, 0x00, 0x00, 0x00, 0xEC, 0x23});
  CHECK_FRAME(encodeFaultReset(1, false),
              {0x55, 0x08, 0x01, 0x00, 0x00, 0x02, 0x6D, 0xE2});
  CHECK_FRAME(encodeFaultReset(1, true),
              {0x55, 0x08, 0x01, 0x00, 0x00, 0x03, 0xAC, 0x22});
  CHECK_FRAME(encodeBrake(1),
              {0x55, 0x08, 0x01, 0x00, 0x00, 0x04, 0xED, 0xE0});
  CHECK_FRAME(encodeRead(Cmd::ReadSpeed, 1).value(),
              {0x56, 0x08, 0x01, 0x33, 0x33, 0x33, 0x48, 0xFA});
  CHECK_FRAME(encodeRead(Cmd::ReadInfo, 1).value(),
              {0x58, 0x08, 0x01, 0x33, 0x33, 0x33, 0x49, 0xD4});
}

// ---------- 3. 频率边界与防御性校验（决策 D1：10–50Hz） ----------
static void testFrequencyBounds() {
  CHECK(hzToRpm(10) == 600);
  CHECK(hzToRpm(50) == 3000);
  CHECK(hzToRpm(50) <= kMaxSpeedValue);  // 3000 <= 3001

  // 量程边界合法
  CHECK(encodeRunHz(1, 10, MotorStatus::DirA).has_value());
  CHECK(encodeRunHz(1, 50, MotorStatus::DirA).has_value());
  // 越界拒绝（原型错误量程 100Hz 必须被挡下）
  CHECK(!encodeRunHz(1, 9, MotorStatus::DirA).has_value());
  CHECK(!encodeRunHz(1, 51, MotorStatus::DirA).has_value());
  CHECK(!encodeRunHz(1, 100, MotorStatus::DirA).has_value());
  CHECK(!encodeRunHz(1, -5, MotorStatus::DirA).has_value());

  // 10Hz 闭环编码 = 0x1258
  const Frame f10 = encodeRunHz(1, 10, MotorStatus::DirA).value();
  CHECK(f10[3] == 0x12 && f10[4] == 0x58);

  // 闭环 RPM 下限 100：99 拒绝，100 通过
  CHECK(!encodeMotorCtrl(1, 99, true, MotorStatus::DirA).has_value());
  CHECK(encodeMotorCtrl(1, 100, true, MotorStatus::DirA).has_value());
  CHECK(!encodeMotorCtrl(1, 3002, true, MotorStatus::DirA).has_value());
  // 开环占空比 50..3001：49 拒绝，50 通过，3001 通过
  CHECK(!encodeMotorCtrl(1, 49, false, MotorStatus::DirA).has_value());
  CHECK(encodeMotorCtrl(1, 50, false, MotorStatus::DirA).has_value());
  CHECK(encodeMotorCtrl(1, 3001, false, MotorStatus::DirA).has_value());
  // 非读取指令走 encodeRead 必须被拒
  CHECK(!encodeRead(Cmd::MotorCtrl, 1).has_value());
}

// ---------- 4. 应答解码（协议 PDF p9–p12 示例） ----------
static void testReplyParsing() {
  Reply h;
  ReplyPayload p;

  // p11 转速示例：12 08 01 00 05 00 -> 0x000500 = 1280 RPM
  Frame fs = buildFrame(0x12, 0x08, 0x01, 0x00, 0x05, 0x00);
  CHECK(validateReplyFrame(fs));
  CHECK(parseReply(fs, h, p));
  CHECK(h == Reply::Speed);
  CHECK(std::get<SpeedReply>(p).rpm == 1280);
  CHECK(std::get<SpeedReply>(p).addr == 1);

  // p11/12 位置示例：13 08 01 FF F5 20 -> 0xFFF520 = -2784
  Frame fp = buildFrame(0x13, 0x08, 0x01, 0xFF, 0xF5, 0x20);
  CHECK(parseReply(fp, h, p));
  CHECK(h == Reply::Position);
  CHECK(std::get<PositionReply>(p).position == -2784);

  // p12 控制信息示例：14 08 01 00 20 01 -> 无故障、32℃、运行中
  Frame fi = buildFrame(0x14, 0x08, 0x01, 0x00, 0x20, 0x01);
  CHECK(parseReply(fi, h, p));
  CHECK(h == Reply::Info);
  CHECK(std::get<InfoReply>(p).faultBits == FaultNone);
  CHECK(std::get<InfoReply>(p).tempC == 32);
  CHECK(std::get<InfoReply>(p).motorRunning);

  // 堵转故障帧：故障位 0x20 + 40℃ + 停止
  Frame ff = buildFrame(0x14, 0x08, 0x01, 0x20, 0x28, 0x00);
  CHECK(parseReply(ff, h, p));
  CHECK(std::get<InfoReply>(p).faultBits & FaultStall);
  CHECK(std::get<InfoReply>(p).tempC == 40);
  CHECK(!std::get<InfoReply>(p).motorRunning);

  // p9 ACK 成功：11 08 01 AA 00 00
  Frame fa = buildFrame(0x11, 0x08, 0x01, 0xAA, 0x00, 0x00);
  CHECK(parseReply(fa, h, p));
  CHECK(h == Reply::Ack);
  CHECK(std::get<AckReply>(p).flag == AckFlag::Ok);

  // p10 霍尔矫正应答：11 08 01 AA 02 00 -> 霍尔状态=02
  Frame fh = buildFrame(0x11, 0x08, 0x01, 0xAA, 0x02, 0x00);
  CHECK(parseReply(fh, h, p));
  CHECK(std::get<AckReply>(p).hallState == 2);

  // 地址占用应答：EE
  Frame fe = buildFrame(0x11, 0x08, 0x01, 0xEE, 0x00, 0x00);
  CHECK(parseReply(fe, h, p));
  CHECK(std::get<AckReply>(p).flag == AckFlag::AddrBusy);

  // CRC 破坏 -> 拒绝
  Frame bad = fa;
  bad[6] ^= 0xFF;
  CHECK(!validateReplyFrame(bad));
  CHECK(!parseReply(bad, h, p));

  // 主机指令头（0x55）不是合法应答 -> 拒绝（防 TX 回显误判）
  Frame echo = mk({0x55, 0x08, 0x01, 0x17, 0x08, 0x00, 0x5B, 0xE7});
  CHECK(!validateReplyFrame(echo));

  // 长度字段错误 -> 拒绝
  Frame len0 = buildFrame(0x11, 0x09, 0x01, 0xAA, 0x00, 0x00);
  CHECK(!validateReplyFrame(len0));

  // 未知 ACK 标志 -> 拒绝
  Frame unk = buildFrame(0x11, 0x08, 0x01, 0x77, 0x00, 0x00);
  CHECK(!parseReply(unk, h, p));
}

// ---------- 5. 字节流同步器（分段/垃圾/粘包） ----------
static void testStreamFramer() {
  StreamFramer fr;
  Frame out{};

  // 分段到达：3 + 5 字节
  const Frame info = buildFrame(0x14, 0x08, 0x02, 0x00, 0x2A, 0x01);
  fr.feed(info.data(), 3);
  CHECK(!fr.next(out));
  fr.feed(info.data() + 3, 5);
  CHECK(fr.next(out));
  CHECK(out == info);
  CHECK(!fr.next(out));

  // 垃圾前缀 + 完整帧：自动重同步并计数
  StreamFramer fr2;
  const std::uint8_t garbage[] = {0xFF, 0x99, 0x55, 0x12, 0x34};
  fr2.feed(garbage, sizeof garbage);
  fr2.feed(info.data(), kFrameLen);
  CHECK(fr2.next(out));
  CHECK(out == info);
  CHECK(fr2.droppedBytes() == sizeof garbage);

  // 粘包：两帧连发，依次弹出
  StreamFramer fr3;
  const Frame ack = buildFrame(0x11, 0x08, 0x03, 0xAA, 0x00, 0x00);
  std::uint8_t both[16];
  std::memcpy(both, ack.data(), 8);
  std::memcpy(both + 8, info.data(), 8);
  fr3.feed(both, sizeof both);
  CHECK(fr3.next(out) && out == ack);
  CHECK(fr3.next(out) && out == info);
  CHECK(!fr3.next(out));

  // 帧中间被截断的垃圾不会导致死锁：灌大量噪声后仍能收下一帧
  StreamFramer fr4;
  std::uint8_t noise[64];
  for (int i = 0; i < 64; ++i) noise[i] = static_cast<std::uint8_t>(i * 7);
  fr4.feed(noise, sizeof noise);
  CHECK(!fr4.next(out));
  fr4.feed(info.data(), kFrameLen);
  CHECK(fr4.next(out));
  CHECK(out == info);
}

#include <cstring>  // memcpy（testStreamFramer 使用）

int main() {
  testCrc();
  testFrozenFrames();
  testFrequencyBounds();
  testReplyParsing();
  testStreamFramer();
  std::printf("%d checks, %d failures\n", g_checks, g_fails);
  return g_fails == 0 ? 0 : 1;
}
