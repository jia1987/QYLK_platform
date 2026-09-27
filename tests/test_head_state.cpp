// @srs SRS-030 SRS-031 SRS-032 SRS-033 SRS-034 SRS-040 SRS-041 SRS-042 SRS-044 SRS-050 SRS-051 SRS-052 SRS-053 SRS-117
// TreatmentHead 状态机单测 —— 决策 D3 语义验证：
// 滑行停止/缓启动恢复、倒计时冻结、故障手动复位、应答拒绝回退、失联冻结、
// 运行中改参（原型差值语义）、扫频谷底滑行段。
#include <vector>

#include "core/head_state.h"
#include "test_harness.h"

using namespace massage::core;

static Frame frozen(std::initializer_list<int> bytes) {
  Frame f{};
  std::size_t i = 0;
  for (int b : bytes)
    if (i < kFrameLen) f[i++] = static_cast<std::uint8_t>(b);
  return f;
}

// 冻结帧（设计方案 §2）
static const Frame kRun30A = frozen({0x55, 0x08, 0x01, 0x17, 0x08, 0x00, 0x5B, 0xE7});
static const Frame kCoastA = frozen({0x55, 0x08, 0x01, 0x00, 0x00, 0x00, 0xEC, 0x23});
static const Frame kResetA = frozen({0x55, 0x08, 0x01, 0x00, 0x00, 0x02, 0x6D, 0xE2});

struct Recorder {
  std::vector<HeadEvent> events;
  std::function<void(HeadEvent)> fn() {
    return [this](HeadEvent e) { events.push_back(e); };
  }
  bool has(HeadEvent e) const {
    for (auto x : events)
      if (x == e) return true;
    return false;
  }
  void clear() { events.clear(); }
};

static TreatmentHead makeOnlineHead(Recorder& rec, std::uint8_t addr = 1) {
  TreatmentHead h("L1", addr);
  h.setEventCallback(rec.fn());
  h.onPresence(true);
  return h;
}

// ---------- 1. 完整生命周期：启动→运行→倒计时归零→完成 ----------
static void testLifecycle() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);
  CHECK(h.state() == HeadState::Idle);

  auto f = h.start();
  CHECK(f.has_value() && *f == kRun30A);  // 30Hz 恒频 A向 = 冻结帧
  CHECK(h.state() == HeadState::Starting);
  CHECK(h.remainingSeconds() == 1200);
  CHECK(rec.has(HeadEvent::Started));

  h.onAck(AckFlag::Ok);
  CHECK(h.state() == HeadState::Running);

  // 恒频稳态：1199 秒内不应产生任何重发帧（Δ<30RPM）
  bool steadySilent = true;
  for (int i = 0; i < 1198; ++i)
    if (h.tick(1.0).has_value()) steadySilent = false;
  CHECK(steadySilent);
  CHECK(h.remainingSeconds() == 2);

  // 归零：发滑行停止帧 + TimerExpired + Stopping
  auto stop = h.tick(1.0);
  CHECK(h.remainingSeconds() == 1);
  auto exp = h.tick(1.0);
  CHECK(exp.has_value() && *exp == kCoastA);
  CHECK(h.state() == HeadState::Stopping);
  CHECK(rec.has(HeadEvent::TimerExpired));
  (void)stop;

  // 转速降到阈值以下 → Idle + Completed
  h.onSpeed(30);
  h.tick(0.25);
  CHECK(h.state() == HeadState::Idle);
  CHECK(rec.has(HeadEvent::Completed));
  CHECK(h.remainingSeconds() == 0 && h.totalSeconds() == 0);
}

// ---------- 2. 暂停/继续：滑行停止、倒计时冻结、缓启动恢复 ----------
static void testPauseResume() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);
  h.start();
  h.onAck(AckFlag::Ok);
  for (int i = 0; i < 100; ++i) h.tick(1.0);
  const int before = h.remainingSeconds();

  auto p = h.pause();
  CHECK(p.has_value() && *p == kCoastA);  // 暂停 = 滑行停止（D3）
  CHECK(h.state() == HeadState::Paused);
  h.onAck(AckFlag::Ok);

  for (int i = 0; i < 60; ++i) h.tick(1.0);  // 暂停 60 秒
  CHECK(h.remainingSeconds() == before);      // 倒计时冻结
  CHECK_NEAR(h.profileElapsedSeconds(), 100.0, 1e-9);  // 曲线也冻结

  auto r = h.resume();
  CHECK(r.has_value() && *r == kRun30A);  // 恢复 = 重发目标，板载缓启动爬升
  CHECK(h.state() == HeadState::Running);
  h.onAck(AckFlag::Ok);
  h.tick(1.0);
  CHECK(h.remainingSeconds() == before - 1);  // 倒计时继续
  CHECK(rec.has(HeadEvent::Paused) && rec.has(HeadEvent::Resumed));
}

// ---------- 3. 用户停止 ----------
static void testUserStop() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);
  h.start();
  h.onAck(AckFlag::Ok);
  h.onSpeed(1800);
  for (int i = 0; i < 10; ++i) h.tick(1.0);

  auto s = h.stop();
  CHECK(s.has_value() && *s == kCoastA);
  CHECK(h.state() == HeadState::Stopping);
  CHECK(rec.has(HeadEvent::StopRequested));

  h.tick(0.5);  // 转速仍高：保持 Stopping
  CHECK(h.state() == HeadState::Stopping);
  h.onSpeed(20);
  h.tick(0.5);
  CHECK(h.state() == HeadState::Idle);
  CHECK(!rec.has(HeadEvent::Completed));  // 用户停止不算「治疗完成」
  CHECK(h.remainingSeconds() == 0);

  // 停止超时兜底：5 秒后强制 Idle（板载堵转保护兜底）
  h.start();
  h.onAck(AckFlag::Ok);
  h.onSpeed(1800);
  h.stop();
  for (int i = 0; i < 24; ++i) h.tick(0.25);  // 6 秒
  CHECK(h.state() == HeadState::Idle);
}

// ---------- 4. 故障与手动复位（D3：不自动恢复）----------
static void testFault() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);
  h.start();
  h.onAck(AckFlag::Ok);
  h.tick(1.0);

  h.onInfo(0x20 | 0x80, 45, true);  // 堵转故障 + 运行位
  CHECK(h.state() == HeadState::Fault);
  CHECK(h.faultBits() == 0x20);
  CHECK(h.tempC() == 45);
  CHECK(rec.has(HeadEvent::FaultEntered));

  CHECK(!h.start().has_value());       // 故障中禁止启动
  CHECK(!h.pause().has_value());
  CHECK(!h.tick(1.0).has_value());     // 全冻结
  h.onInfo(0x00, 40, false);           // 故障位消失也不自动恢复
  CHECK(h.state() == HeadState::Fault);

  auto rf = h.resetFault();
  CHECK(rf.has_value() && *rf == kResetA);  // 复位帧 02（A向）
  h.onAck(AckFlag::Ok);
  CHECK(h.state() == HeadState::Idle);
  CHECK(h.faultBits() == 0);
  CHECK(rec.has(HeadEvent::FaultCleared));
  CHECK(h.start().has_value());  // 复位后可重新启动

  // 运行位（0x80）不是故障
  Recorder rec2;
  TreatmentHead h2 = makeOnlineHead(rec2);
  h2.onInfo(0x80, 33, true);
  CHECK(h2.state() == HeadState::Idle);
  CHECK(h2.faultBits() == 0);
}

// ---------- 5. 应答拒绝的安全回退 ----------
static void testAckRejection() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);

  // 启动被拒 → 回 Idle，计时复位
  h.start();
  h.onAck(AckFlag::Fail);
  CHECK(h.state() == HeadState::Idle);
  CHECK(h.remainingSeconds() == 0);
  CHECK(rec.has(HeadEvent::AckRejected));

  // 暂停被拒 → 回 Running（电机没停成就继续计时，UI 与物理一致）
  h.start();
  h.onAck(AckFlag::Ok);
  h.tick(1.0);
  h.pause();
  h.onAck(AckFlag::Fail);
  CHECK(h.state() == HeadState::Running);
  h.tick(1.0);
  CHECK(h.remainingSeconds() == 1198);

  // 继续被拒 → 回 Paused
  h.pause();
  h.onAck(AckFlag::Ok);
  h.resume();
  h.onAck(AckFlag::Fail);
  CHECK(h.state() == HeadState::Paused);
}

// ---------- 6. 运行中实时改参（原型差值语义）----------
static void testLiveConfig() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);
  h.start();
  h.onAck(AckFlag::Ok);
  for (int i = 0; i < 10; ++i) h.tick(1.0);

  // 时间 20→25min：剩余 +300s
  HeadConfig c = h.config();
  const int before = h.remainingSeconds();
  c.timeMin = 25;
  CHECK(h.applyConfig(c));
  CHECK(h.remainingSeconds() == before + 300);
  CHECK(h.totalSeconds() == 1500);

  // 频率 30→50Hz：下个 tick 下发新目标（0x1BB8）
  c = h.config();
  c.freqHz = 50;
  CHECK(h.applyConfig(c));
  h.onAck(AckFlag::Ok);  // 清掉上一帧的等待
  auto f = h.tick(0.25);
  CHECK(f.has_value());
  CHECK((*f)[0] == 0x55 && (*f)[3] == 0x1B && (*f)[4] == 0xB8);

  // 越界拒绝（原型量程错误 100Hz 必须被挡）
  c = h.config();
  c.freqHz = 100;
  CHECK(!h.applyConfig(c));
  CHECK(h.config().freqHz == 50);
  c.freqHz = 9;
  CHECK(!h.applyConfig(c));
  c = h.config();
  c.timeMin = 61;
  CHECK(!h.applyConfig(c));

  // 运行中禁止改方向；Idle 允许
  c = h.config();
  c.dir = MotorStatus::DirB;
  CHECK(!h.applyConfig(c));
  h.stop();
  h.onSpeed(0);
  h.tick(0.25);
  CHECK(h.state() == HeadState::Idle);
  CHECK(h.applyConfig(c));
  CHECK(h.config().dir == MotorStatus::DirB);
  auto g = h.start();  // B向运行帧
  CHECK(g.has_value() && (*g)[5] == 0x01);
}

// ---------- 7. 失联冻结（D5：电机可能仍在转）----------
static void testOfflineDuringRun() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);
  h.start();
  h.onAck(AckFlag::Ok);
  for (int i = 0; i < 50; ++i) h.tick(1.0);
  const int before = h.remainingSeconds();

  h.onPresence(false);
  CHECK(rec.has(HeadEvent::WentOfflineDuringRun));
  CHECK(h.state() == HeadState::Running);  // 状态冻结不回退
  CHECK(!h.tick(5.0).has_value());          // tick 停摆
  CHECK(h.remainingSeconds() == before);    // 倒计时冻结
  CHECK(!h.pause().has_value());            // 命令全部拒绝
  CHECK(!h.stop().has_value());

  h.onPresence(true);  // 恢复：倒计时继续
  h.tick(1.0);
  CHECK(h.remainingSeconds() == before - 1);
}

// ---------- 8. 扫频曲线推进与谷底滑行 ----------
static void testSweepProfile() {
  Recorder rec;
  TreatmentHead h = makeOnlineHead(rec);
  HeadConfig c = h.config();
  c.mode = Mode::Sweep;
  c.freqHz = 50;
  CHECK(h.applyConfig(c));

  // t=0 位于滑行段：start 不发帧（无有效闭环目标）
  CHECK(!h.start().has_value());
  CHECK(h.state() == HeadState::Starting);

  // t=1s：10Hz → 600RPM 闭环帧（0x1258）
  auto f = h.tick(1.0);
  CHECK(f.has_value());
  CHECK((*f)[3] == 0x12 && (*f)[4] == 0x58);
  h.onAck(AckFlag::Ok);
  CHECK(h.state() == HeadState::Running);

  // t=2.5s：25Hz → 1500RPM（0x15DC）
  f = h.tick(1.5);
  CHECK(f.has_value());
  CHECK((*f)[3] == 0x15 && (*f)[4] == 0xDC);
  h.onAck(AckFlag::Ok);

  // t=5s 顶点 50Hz → 3000RPM（0x1BB8）
  f = h.tick(2.5);
  CHECK(f.has_value());
  CHECK((*f)[3] == 0x1B && (*f)[4] == 0xB8);
  h.onAck(AckFlag::Ok);

  // 稳态小步 tick 不重发（Δ<30RPM 阈值）
  CHECK(!h.tick(0.01).has_value());

  // 推进到 t≈9.86s（回落至 1.4Hz → 滑行段）：应发滑行停止帧
  h.onSpeed(2900);  // 转速反馈跟随，避免 Stopping 判定干扰
  f = h.tick(4.85);
  CHECK(f.has_value() && *f == kCoastA);
  h.onAck(AckFlag::Ok);
}

// ---------- 9. 缺位头拒绝一切命令 ----------
static void testAbsent() {
  Recorder rec;
  TreatmentHead h("L2", 2);
  h.setEventCallback(rec.fn());
  CHECK(h.state() == HeadState::Absent);
  CHECK(!h.start().has_value());
  CHECK(!h.stop().has_value());
  CHECK(!h.resetFault().has_value());
  h.onPresence(true);
  CHECK(h.state() == HeadState::Idle);
  h.onPresence(false);
  CHECK(h.state() == HeadState::Absent);
}

int main() {
  testLifecycle();
  testPauseResume();
  testUserStop();
  testFault();
  testAckRejection();
  testLiveConfig();
  testOfflineDuringRun();
  testSweepProfile();
  testAbsent();
  return th::summary();
}
