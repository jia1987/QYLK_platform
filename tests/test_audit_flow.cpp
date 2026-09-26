// M4a 集成测试（DoD#2 的可执行证据）：AppCore + MockBus + AuditLog 完整治疗场次，
// 断言审计 DB 与事件流逐条一致：
//   在位→启动→运行中改参(2s防抖)→快照→故障注入→复位→停止→PIN 防暴破→操作员→
//   链完整性→正常关闭（无异常会话）→realBus 上电清理留痕。
// 组织名/应用名设为测试专用 → AppConfig/审计目录与真实用户配置完全隔离。
#include <QCoreApplication>
#include <QElapsedTimer>

#include <filesystem>
#include <string>
#include <vector>

#include "app_core.h"
#include "audit/audit_log.h"
#include "mock_bus.h"
#include "test_harness.h"

using namespace massage;

static void spin(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

namespace {

using audit::EventQuery;
using audit::EventRow;

std::vector<EventRow> eventsOf(audit::AuditLog& a, const char* category) {
  EventQuery q;
  q.category = category ? category : "";
  q.limit = 10000;
  return a.queryEvents(q);
}

const EventRow* findType(const std::vector<EventRow>& v, const char* type) {
  for (const auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}

const EventRow* findLastType(const std::vector<EventRow>& v, const char* type) {
  const EventRow* out = nullptr;
  for (const auto& r : v)
    if (r.type == type) out = &r;
  return out;
}

long long countType(const std::vector<EventRow>& v, const char* type) {
  long long n = 0;
  for (const auto& r : v)
    if (r.type == type) ++n;
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication qapp(argc, argv);
  qapp.setOrganizationName(QStringLiteral("massage-audit-test"));
  qapp.setApplicationName(QStringLiteral("test-audit-flow"));

  // 测试专用临时目录（DB 与兜底文件）
  std::error_code ec;
  const auto tmp = std::filesystem::temp_directory_path() /
                   ("massage_flow_" + std::to_string(static_cast<long long>(qapp.applicationPid())));
  std::filesystem::remove_all(tmp, ec);
  std::filesystem::create_directories(tmp, ec);
  // 测试专用 AppConfig 也须每次全新（组织/应用名已隔离，防上次运行遗留 PIN）
  std::filesystem::remove(app::AppConfig::configPath().toStdString(), ec);

  audit::SystemClock clk;
  audit::AuditLog auditDb;
  audit::AuditConfig acfg;
  acfg.dbPath = (tmp / "audit.db").string();
  acfg.chainInterval = 20;
  acfg.appVersion = "flow-test";
  CHECK(auditDb.open(acfg, clk));

  app::AppConfig cfg = app::AppConfig::loadOrCreate();  // 测试专属配置（无 PIN）
  app::MockBus mock({1, 2, 3, 4, 5, 6});
  app::AppCore core(&cfg, mock, /*realBus=*/false, &auditDb);
  mock.replySink = [&core](const core::Frame& f) { core.deliverReply(f); };

  CHECK(core.auditOk());

  // ---- 在位轮询 → HEAD_PLUGGED（D24：含 idle 插拔）----
  spin(900);
  auto presence = eventsOf(auditDb, audit::cat::Presence);
  CHECK(countType(presence, audit::ev::HeadPlugged) >= 5);  // 6 头全在位（至少 5 已上报）

  // ---- 启动治疗 → THERAPY_START（含参数快照）----
  // 注意：selectHead 是 toggle 语义（再点取消选择），首个上线头会被自动选中——
  // 仅在当前选中不是 L1 时才点选
  if (core.targetName() != QStringLiteral("L1")) core.selectHead(QStringLiteral("L1"));
  CHECK(core.targetName() == QStringLiteral("L1"));
  core.requestStart(QStringLiteral("L1"));
  core.confirmResponse(true);
  spin(500);
  auto therapy = eventsOf(auditDb, audit::cat::Therapy);
  const EventRow* start = findType(therapy, audit::ev::TherapyStart);
  CHECK(start != nullptr);
  if (start) {
    CHECK(start->slot == "L1");
    CHECK(start->addr == 1);
    CHECK(start->payload.find("\"freq\":") != std::string::npos);
    CHECK(start->payload.find("\"mode\":") != std::string::npos);
    CHECK(start->payload.find("\"timeMin\":") != std::string::npos);
  }

  // ---- 运行中改参 → 2s 防抖（D15）----
  core.adjustFreq(+1);
  spin(400);
  therapy = eventsOf(auditDb, audit::cat::Therapy);
  CHECK(findType(therapy, audit::ev::ParamChange) == nullptr);  // 防抖窗内不记
  spin(2400);
  therapy = eventsOf(auditDb, audit::cat::Therapy);
  const EventRow* pc = findType(therapy, audit::ev::ParamChange);
  CHECK(pc != nullptr);
  if (pc) {
    CHECK(pc->payload.find("\"before\"") != std::string::npos);
    CHECK(pc->payload.find("\"after\"") != std::string::npos);
    CHECK(pc->slot == "L1");
  }

  // ---- 30s 会话快照（D15，测试直接触发）----
  core.snapshotNow();
  const auto snaps = auditDb.querySnapshots(auditDb.bootSeq());
  CHECK(snaps.size() >= 1);
  if (!snaps.empty()) {
    CHECK(snaps[0].data.find("\"slot\":\"L1\"") != std::string::npos);
    CHECK(snaps[0].data.find("targetRpm") != std::string::npos);
  }

  // ---- 故障注入 → FAULT_DETECTED；复位 → FAULT_RESET ----
  mock.setFault(1, 0x04);  // 过温
  spin(1500);              // 0x58 轮询周期 500ms
  auto faults = eventsOf(auditDb, audit::cat::Fault);
  const EventRow* fd = findType(faults, audit::ev::FaultDetected);
  CHECK(fd != nullptr);
  if (fd) {
    CHECK(fd->slot == "L1");
    CHECK(fd->payload.find("\"faultBits\":4") != std::string::npos);
  }
  core.headAction(QStringLiteral("L1"), QStringLiteral("reset"));
  spin(1500);
  faults = eventsOf(auditDb, audit::cat::Fault);
  CHECK(findType(faults, audit::ev::FaultReset) != nullptr);

  // ---- 复位后头回 Idle → 重新启动再用户停止 → 第二次 THERAPY_START + STOP ----
  core.requestStart(QStringLiteral("L1"));
  core.confirmResponse(true);
  spin(500);
  core.requestStop(QStringLiteral("L1"));
  core.confirmResponse(true);
  spin(2500);  // Stopping → 滑行 → Idle
  therapy = eventsOf(auditDb, audit::cat::Therapy);
  const EventRow* st = findType(therapy, audit::ev::Stop);
  CHECK(st != nullptr);
  if (st) CHECK(st->payload.find("\"reason\":\"user\"") != std::string::npos);
  CHECK(countType(therapy, audit::ev::TherapyStart) == 2);  // 两次启动都有痕迹

  // ---- PIN：首设 + 防暴破锁定（D20）----
  CHECK(!core.pinSet());
  CHECK(core.setInitialPin(QStringLiteral("1234")));
  auto access = eventsOf(auditDb, audit::cat::Access);
  CHECK(findType(access, audit::ev::PinInitialSet) != nullptr);
  for (int i = 0; i < 5; ++i)
    CHECK(!core.verifyPin(QStringLiteral("9999"), QStringLiteral("test")));
  CHECK(core.pinLocked());
  CHECK(!core.verifyPin(QStringLiteral("1234"), QStringLiteral("test")));  // 锁定期间正确 PIN 也拒绝
  access = eventsOf(auditDb, audit::cat::Access);
  CHECK(findType(access, audit::ev::PinLocked) != nullptr);
  CHECK(countType(access, audit::ev::PinFail) == 6);  // 5 次失败 + 1 次锁定期间尝试

  // ---- 操作员名单（D14）：新治疗的 operator_id ----
  const qlonglong op1 = core.addOperator(QStringLiteral("张三"), QStringLiteral("A01"));
  CHECK(op1 > 0);
  core.setCurrentOperator(op1);
  CHECK(core.currentOperatorName() == QStringLiteral("张三"));
  core.requestStart(QStringLiteral("L3"));
  core.confirmResponse(true);
  spin(500);
  therapy = eventsOf(auditDb, audit::cat::Therapy);
  const EventRow* start3 = findLastType(therapy, audit::ev::TherapyStart);
  CHECK(start3 != nullptr);
  if (start3) {
    CHECK(start3->slot == "L3");
    CHECK(start3->operatorId == op1);
  }

  // ---- 事件时序单调（同一会话内 id 与 mono 同序）----
  {
    const auto all = eventsOf(auditDb, nullptr);
    for (std::size_t i = 1; i < all.size(); ++i) {
      CHECK(all[i].id > all[i - 1].id);
      CHECK(all[i].monoMs >= all[i - 1].monoMs);
    }
  }

  // ---- 链完整性 + 物理完整性（D21）----
  long long broken = -1;
  CHECK(auditDb.verifyChain(broken));
  CHECK(auditDb.integrityCheck() == "ok");
  auditDb.close();

  // ---- 正常关闭 → 重开无异常会话（D19 反向验证）----
  {
    audit::AuditLog b;
    CHECK(b.open(acfg, clk));
    CHECK(b.bootSeq() == 2);
    const auto sessions = eventsOf(b, audit::cat::Session);
    CHECK(findType(sessions, audit::ev::AbnormalTermination) == nullptr);
    CHECK(findType(sessions, audit::ev::SessionEnd) != nullptr);  // 上一会话正常收尾
    b.close();
  }

  // ---- realBus=true：上电全停清理留痕（D5/D19）----
  {
    audit::AuditLog c;
    audit::AuditConfig ccfg = acfg;
    ccfg.dbPath = (tmp / "audit2.db").string();
    CHECK(c.open(ccfg, clk));
    app::AppConfig cfg2 = app::AppConfig::loadOrCreate();
    app::MockBus mock2({1, 2, 3, 4, 5, 6});
    app::AppCore core2(&cfg2, mock2, /*realBus=*/true, &c);
    mock2.replySink = [&core2](const core::Frame& f) { core2.deliverReply(f); };
    spin(300);
    const auto sess = eventsOf(c, audit::cat::Session);
    const EventRow* sb = findType(sess, audit::ev::StopAllOnBoot);
    CHECK(sb != nullptr);
    if (sb) CHECK(sb->payload.find("\"addrs\":[") != std::string::npos);
    c.close();
  }

  std::filesystem::remove_all(tmp, ec);
  return th::summary();
}
