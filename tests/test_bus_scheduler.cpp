// @srs SRS-013 SRS-014 SRS-015 SRS-016 SRS-017 SRS-042
// BusScheduler 单测 —— 决策 D5：半双工一问一答、轮询计划、超时重试、
// 离线判定与恢复、控制帧插队、应答严格匹配。FakeClock+MockTransport 全离线。
#include <vector>

#include "core/bus_scheduler.h"
#include "test_harness.h"

using namespace massage::core;

// ---------- 测试替身 ----------
struct FakeClock : IClock {
  std::int64_t t = 0;
  std::int64_t nowMs() const override { return t; }
  void advance(std::int64_t ms) { t += ms; }
};

struct MockTransport : ITransport {
  std::vector<Frame> sent;
  bool ok = true;
  bool send(const Frame& f) override {
    if (!ok) return false;
    sent.push_back(f);
    return true;
  }
  const Frame& last() const { return sent.back(); }
  void clear() { sent.clear(); }
};

static Frame infoReply(std::uint8_t addr, std::uint8_t fault = 0x00,
                       std::uint8_t temp = 32, std::uint8_t running = 0) {
  return buildFrame(0x14, 0x08, addr, fault, temp, running);
}
static Frame speedReply(std::uint8_t addr, std::uint32_t rpm) {
  return buildFrame(0x12, 0x08, addr, static_cast<std::uint8_t>((rpm >> 16) & 0xFF),
                    static_cast<std::uint8_t>((rpm >> 8) & 0xFF),
                    static_cast<std::uint8_t>(rpm & 0xFF));
}
static Frame ackReply(std::uint8_t addr, std::uint8_t flag = 0xAA) {
  return buildFrame(0x11, 0x08, addr, flag, 0x00, 0x00);
}

struct PresenceLog {
  std::vector<std::pair<std::uint8_t, bool>> entries;
  int countOf(std::uint8_t addr, bool online) const {
    int n = 0;
    for (auto& e : entries)
      if (e.first == addr && e.second == online) ++n;
    return n;
  }
};

// ---------- 1. 轮询计划与在位探测 ----------
static void testPollingAndPresence() {
  FakeClock clk;
  MockTransport tp;
  BusScheduler sched(BusConfig{}, tp, clk);
  PresenceLog pres;
  sched.onPresenceChange = [&](std::uint8_t a, bool on) {
    pres.entries.emplace_back(a, on);
  };
  std::vector<std::pair<std::uint8_t, Reply>> routed;
  sched.onReplyRouted = [&](std::uint8_t a, Reply h, const ReplyPayload&) {
    routed.emplace_back(a, h);
  };

  sched.setDevices({1, 2});
  sched.tick();  // t=0：轮询 dev1 状态
  CHECK(tp.sent.size() == 1);
  CHECK(tp.last()[0] == 0x58 && tp.last()[2] == 1);

  sched.onReply(infoReply(1, 0x00, 33, 0));
  CHECK(pres.countOf(1, true) == 1);  // 首个应答 → 在位事件
  CHECK(routed.size() == 1 && routed[0].second == Reply::Info);
  CHECK(!sched.hasPending());

  clk.advance(3);  // 帧间隔 2ms
  sched.tick();  // 轮询 dev2
  CHECK(tp.last()[0] == 0x58 && tp.last()[2] == 2);
  sched.onReply(infoReply(2));
  CHECK(pres.countOf(2, true) == 1);

  // dev1 下次状态轮询应在 t≈500
  clk.advance(100);
  sched.tick();
  CHECK(tp.sent.size() == 2);  // 都未到期：不发
  clk.advance(400);            // t=503 ≥ 500
  sched.tick();
  CHECK(tp.sent.size() == 3);
  CHECK(tp.last()[2] == 1);
  CHECK(sched.consecutiveFails(1) == 0);
}

// ---------- 2. 超时重试与事务失败 ----------
static void testTimeoutRetry() {
  FakeClock clk;
  MockTransport tp;
  BusScheduler sched(BusConfig{}, tp, clk);
  std::vector<std::uint8_t> pollErrors;
  sched.onPollError = [&](std::uint8_t a) { pollErrors.push_back(a); };
  sched.setDevices({1});

  sched.tick();  // t=0 首发
  CHECK(tp.sent.size() == 1);
  sched.tick();  // 未超时：不重发
  CHECK(tp.sent.size() == 1);

  for (int r = 1; r <= 3; ++r) {
    clk.advance(50);  // 超时 → 重试
    sched.tick();
    CHECK(tp.sent.size() == static_cast<std::size_t>(1 + r));
  }
  CHECK(pollErrors.empty());
  clk.advance(50);  // 第 4 次超时：重试耗尽 → 事务失败
  sched.tick();
  CHECK(tp.sent.size() == 4);  // 不再发送
  CHECK(pollErrors.size() == 1 && pollErrors[0] == 1);
  CHECK(sched.consecutiveFails(1) == 1);
  CHECK(!sched.hasPending());

  // 失败后按 statusPollMs 推后，不热循环
  clk.advance(100);
  sched.tick();
  CHECK(tp.sent.size() == 4);
  clk.advance(400);  // t=600 ≥ 失败时刻(200)+500
  sched.tick();
  CHECK(tp.sent.size() == 5);
}

// ---------- 3. 离线判定（连续 5 个失败周期）与恢复 ----------
static void testOfflineAndRecovery() {
  FakeClock clk;
  MockTransport tp;
  BusScheduler sched(BusConfig{}, tp, clk);
  PresenceLog pres;
  sched.onPresenceChange = [&](std::uint8_t a, bool on) {
    pres.entries.emplace_back(a, on);
  };
  sched.setDevices({1});

  // 先上线
  sched.tick();
  sched.onReply(infoReply(1));
  CHECK(sched.deviceOnline(1));

  // 之后 5 个周期全部无应答：驱动 6 秒
  for (std::int64_t t = 0; t < 6000; t += 10) {
    clk.t = t;
    sched.tick();
  }
  CHECK(!sched.deviceOnline(1));
  CHECK(pres.countOf(1, false) == 1);
  CHECK(sched.consecutiveFails(1) >= 5);

  // 恢复：探测帧得到应答 → 重新在位
  bool backOnline = false;
  for (std::int64_t t = 6000; t < 12000 && !backOnline; t += 5) {
    clk.t = t;
    const std::size_t before = tp.sent.size();
    sched.tick();
    if (tp.sent.size() > before && sched.hasPending()) {
      sched.onReply(infoReply(1));
      backOnline = sched.deviceOnline(1);
    }
  }
  CHECK(backOnline);
  CHECK(pres.countOf(1, true) == 2);  // 首次上线 + 恢复
  CHECK(sched.consecutiveFails(1) == 0);
}

// ---------- 4. 控制帧插队优先 ----------
static void testControlPriority() {
  FakeClock clk;
  MockTransport tp;
  BusScheduler sched(BusConfig{}, tp, clk);
  sched.setDevices({1, 2});
  sched.tick();
  sched.onReply(infoReply(1));
  clk.advance(3);
  sched.tick();
  sched.onReply(infoReply(2));

  // 推到两者都到期，同时压入控制帧：必须先发控制帧
  clk.advance(600);
  bool cbCalled = false;
  bool cbOk = false;
  Reply cbHeader = Reply::Ack;
  const Frame run = encodeRunHz(1, 30, MotorStatus::DirA).value();
  sched.enqueueControl(run, [&](bool ok, Reply h, const ReplyPayload&) {
    cbCalled = true;
    cbOk = ok;
    cbHeader = h;
  });
  sched.tick();
  CHECK(tp.last() == run);  // 0x55 插队成功
  sched.onReply(ackReply(1));
  CHECK(cbCalled && cbOk && cbHeader == Reply::Ack);

  clk.advance(3);
  sched.tick();  // 控制帧处理完才轮到轮询
  CHECK(tp.last()[0] == 0x58);
}

// ---------- 5. 应答严格匹配（错地址/错类型忽略）----------
static void testReplyMatching() {
  FakeClock clk;
  MockTransport tp;
  BusScheduler sched(BusConfig{}, tp, clk);
  sched.setDevices({1});
  sched.tick();  // 轮询 dev1 的 0x58
  CHECK(tp.last()[2] == 1);

  // 在途事务期间的错地址应答 → 计为忽略（严格请求-应答序）
  sched.onReply(infoReply(2));
  CHECK(sched.hasPending());
  CHECK(sched.ignoredReplyCount() == 1);

  sched.onReply(speedReply(1, 1800));  // 错类型（期待 Info 来了 Speed）→ 忽略
  CHECK(sched.ignoredReplyCount() == 2);
  CHECK(sched.hasPending());

  sched.onReply(infoReply(1, 0x04, 50, 1));  // 正确应答
  CHECK(!sched.hasPending());

  // 路由出去的 Info 载荷正确
  InfoReply got;
  bool routed = false;
  sched.onReplyRouted = [&](std::uint8_t, Reply, const ReplyPayload& p) {
    got = std::get<InfoReply>(p);
    routed = true;
  };
  clk.advance(500);
  sched.tick();
  sched.onReply(infoReply(1, 0x04, 50, 1));
  CHECK(routed);
  CHECK(got.faultBits == 0x04 && got.tempC == 50 && got.motorRunning);
}

// ---------- 6. 转速轮询开关 ----------
static void testSpeedPolling() {
  FakeClock clk;
  MockTransport tp;
  BusScheduler sched(BusConfig{}, tp, clk);
  std::vector<Reply> routedHeaders;
  sched.onReplyRouted = [&](std::uint8_t, Reply h, const ReplyPayload&) {
    routedHeaders.push_back(h);
  };
  sched.setDevices({1});
  sched.tick();
  sched.onReply(infoReply(1));
  CHECK(tp.sent.size() == 1);  // 未开转速轮询：只有状态帧

  sched.setSpeedPollEnabled(1, true);
  bool sawSpeedFrame = false;
  for (std::int64_t t = 0; t < 2500; t += 5) {
    clk.t = t;
    const std::size_t before = tp.sent.size();
    sched.tick();
    if (tp.sent.size() > before && sched.hasPending()) {
      if (tp.last()[0] == 0x56) {
        sawSpeedFrame = true;
        sched.onReply(speedReply(1, 1800));
      } else {
        sched.onReply(infoReply(1));
      }
    }
  }
  CHECK(sawSpeedFrame);
  bool sawSpeedRoute = false;
  for (auto h : routedHeaders)
    if (h == Reply::Speed) sawSpeedRoute = true;
  CHECK(sawSpeedRoute);
}

// ---------- 7. 无应答指令不进事务（广播路径防御）----------
static void testNoReplyCommand() {
  FakeClock clk;
  MockTransport tp;
  BusScheduler sched(BusConfig{}, tp, clk);
  sched.setDevices({1});
  // 0x60 广播：入队按序发出、不建事务、发出即回调（本软件按 D5 不使用广播，
  // 此路径仅防御性覆盖）
  Frame bc = buildFrame(0x60, 0x08, 0xEE, 0x00, 0x00, 0x00);
  bool called = false;
  sched.enqueueControl(bc, [&](bool ok, Reply, const ReplyPayload&) { called = ok; });
  CHECK(sched.hasPending());  // 已入队待 tick
  CHECK(!called);
  sched.tick();
  CHECK(called);
  CHECK(tp.sent.size() >= 1 && tp.sent.front() == bc);
  CHECK(!sched.hasPending());  // 未占用事务
}

int main() {
  testPollingAndPresence();
  testTimeoutRetry();
  testOfflineAndRecovery();
  testControlPriority();
  testReplyMatching();
  testSpeedPolling();
  testNoReplyCommand();
  return th::summary();
}
