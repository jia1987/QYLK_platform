// AuditLog 引擎单测（M4a，决策 D13–D22 的逐条可执行证据）：
//   T1 schema+会话启动   T2 会话哨兵（正常/异常关闭）  T3 三列时间戳+payload 保真
//   T4 触发器防删改      T5 链式哈希+触发器失守兜底     T6 降级 NDJSON+恢复回填
//   T7 open 失败全降级   T8 SUSPECT_TIME              T9 CLOCK_JUMP+节流+改钟
//   T10 水位告警         T11 操作员名单               T12 查询过滤/分页
//   T13 快照+快照降级    T14 json_mini 往返
#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audit/audit_log.h"
#include "audit/json_mini.h"
#include "audit_test_util.h"
#include "test_harness.h"

using namespace massage::audit;

namespace {

bool hasEvent(AuditLog& a, const char* type) {
  EventQuery q;
  q.limit = 10000;
  for (const auto& r : a.queryEvents(q))
    if (r.type == type) return true;
  return false;
}

long long countType(AuditLog& a, const char* type) {
  EventQuery q;
  q.limit = 10000;
  long long n = 0;
  for (const auto& r : a.queryEvents(q))
    if (r.type == type) ++n;
  return n;
}

EventRow findEvent(AuditLog& a, const char* type) {
  EventQuery q;
  q.limit = 10000;
  for (const auto& r : a.queryEvents(q))
    if (r.type == type) return r;
  return {};
}

long long lineCount(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return -1;
  long long n = 0;
  std::string line;
  while (std::getline(f, line))
    if (!line.empty()) ++n;
  return n;
}

bool globExists(const std::filesystem::path& dir, const std::string& prefix) {
  std::error_code ec;
  for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
    const std::string fn = e.path().filename().string();
    if (fn.rfind(prefix, 0) == 0) return true;
  }
  return false;
}

AuditConfig baseCfg(const atest::TempDir& td, int chainInterval = 1000) {
  AuditConfig cfg;
  cfg.dbPath = td.file("audit.db");
  cfg.chainInterval = chainInterval;
  cfg.quotaMb = 1024;
  cfg.appVersion = "test";
  return cfg;
}

}  // namespace

int main() {
  // ---- T1 schema + 会话启动 ----
  {
    atest::TempDir td("t1");
    atest::ManualClock clk;
    AuditLog a;
    const AuditConfig cfg = baseCfg(td, 4);
    CHECK(a.open(cfg, clk));
    CHECK(a.isOpen());
    CHECK(!a.degraded());
    CHECK(a.bootSeq() == 1);
    CHECK(a.integrityCheck() == "ok");
    EventQuery q;
    q.category = cat::Session;
    const auto rows = a.queryEvents(q);
    CHECK(rows.size() == 1);
    CHECK(rows[0].type == ev::SessionStart);
    CHECK(rows[0].bootSeq == 1 && rows[0].sessionId == 1);
    CHECK(rows[0].payload.find("\"boot\":1") != std::string::npos);
    long long broken = -1;
    CHECK(a.verifyChain(broken));
    a.close();
  }

  // ---- T2 会话哨兵：正常关闭无异常标记；未 close 销毁 → 下次启动补记 ----
  {
    atest::TempDir td("t2");
    atest::ManualClock clk;
    const AuditConfig cfg = baseCfg(td, 3);
    {
      AuditLog a;
      CHECK(a.open(cfg, clk));
      clk.advance(1000);
      a.log(cat::Therapy, ev::TherapyStart, "L1", 1, 3, "{\"freq\":30}");
      a.close();
    }
    {
      AuditLog b;
      CHECK(b.open(cfg, clk));
      CHECK(b.bootSeq() == 2);
      CHECK(!hasEvent(b, ev::AbnormalTermination));
      b.close();
    }
    {  // 崩溃语义：析构不 close
      AuditLog c;
      CHECK(c.open(cfg, clk));
      c.log(cat::Therapy, ev::Pause, "L1", 1);
    }
    {
      AuditLog d;
      CHECK(d.open(cfg, clk));
      CHECK(d.bootSeq() == 4);
      CHECK(hasEvent(d, ev::AbnormalTermination));
      const EventRow r = findEvent(d, ev::AbnormalTermination);
      CHECK(r.payload.find("\"old_boot\":3") != std::string::npos);
      d.close();
    }
  }

  // ---- T3 三列时间戳 + payload 逐字节保真 ----
  {
    atest::TempDir td("t3");
    atest::ManualClock clk;
    AuditLog a;
    CHECK(a.open(baseCfg(td), clk));
    clk.advance(1234);
    const std::string payload =
        "{\"中文\":\"值\\\"引号\",\"brace\":{\"a\":[1,2]},\"nl\":\"x\\ny\"}";
    const long long id =
        a.log(cat::Therapy, ev::TherapyStart, "L2", 2, 7, payload);
    CHECK(id > 0);
    EventQuery q;
    q.category = cat::Therapy;
    const auto rows = a.queryEvents(q);
    CHECK(rows.size() == 1);
    CHECK(rows[0].id == id);
    CHECK(rows[0].wallUtc == clk.wall);   // D17 列一
    CHECK(rows[0].monoMs == clk.mono);    // D17 列二
    CHECK(rows[0].bootSeq == 1);          // D17 列三
    CHECK(rows[0].slot == "L2");
    CHECK(rows[0].addr == 2);
    CHECK(rows[0].operatorId == 7);       // D14
    CHECK(rows[0].payload == payload);    // 逐字节保真
    a.close();
  }

  // ---- T4 触发器防删改（D21：任何 SQL 直连都删不掉）----
  {
    atest::TempDir td("t4");
    atest::ManualClock clk;
    AuditLog a;
    const AuditConfig cfg = baseCfg(td, 1);  // 每条事件一个链段（保证 chain_hashes 非空）
    CHECK(a.open(cfg, clk));
    a.log(cat::Therapy, ev::TherapyStart, "L1", 1);
    a.logSnapshot("[{\"slot\":\"L1\"}]");     // 保证 snapshots 非空（触发器逐行生效）
    a.close();

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open(cfg.dbPath.c_str(), &raw) == SQLITE_OK);
    char* errmsg = nullptr;
    CHECK(sqlite3_exec(raw, "DELETE FROM events WHERE id=1;", nullptr, nullptr,
                       &errmsg) != SQLITE_OK);
    sqlite3_free(errmsg);
    errmsg = nullptr;
    CHECK(sqlite3_exec(raw, "UPDATE events SET type='HACK' WHERE id=1;", nullptr,
                       nullptr, &errmsg) != SQLITE_OK);
    sqlite3_free(errmsg);
    errmsg = nullptr;
    CHECK(sqlite3_exec(raw, "DELETE FROM chain_hashes;", nullptr, nullptr,
                       &errmsg) != SQLITE_OK);
    sqlite3_free(errmsg);
    errmsg = nullptr;
    CHECK(sqlite3_exec(raw, "UPDATE snapshots SET data='x';", nullptr, nullptr,
                       &errmsg) != SQLITE_OK);
    sqlite3_free(errmsg);
    errmsg = nullptr;
    CHECK(sqlite3_exec(raw, "DELETE FROM sessions;", nullptr, nullptr,
                       &errmsg) != SQLITE_OK);
    sqlite3_free(errmsg);
    sqlite3_close(raw);

    AuditLog b;
    CHECK(b.open(cfg, clk));
    CHECK(hasEvent(b, ev::TherapyStart));  // 数据原封不动
    b.close();
  }

  // ---- T5 链式哈希 + 触发器失守（攻击者 DROP TRIGGER）→ 链兜底 ----
  {
    atest::TempDir td("t5");
    atest::ManualClock clk;
    AuditLog a;
    const AuditConfig cfg = baseCfg(td, 4);
    CHECK(a.open(cfg, clk));
    for (int i = 0; i < 10; ++i) {
      clk.advance(100);
      a.log(cat::Therapy, ev::ParamChange, "L1", 1, 0,
            std::string("{\"i\":") + std::to_string(i) + "}");
    }
    long long broken = -1;
    CHECK(a.verifyChain(broken));  // 11 事件 / 间隔4 → 2 个链段，全绿

    // 模拟拿到 DB 文件的攻击者：DROP 触发器后篡改历史
    sqlite3* raw = nullptr;
    CHECK(sqlite3_open(cfg.dbPath.c_str(), &raw) == SQLITE_OK);
    CHECK(sqlite3_exec(raw, "DROP TRIGGER events_no_update;", nullptr, nullptr,
                       nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(raw,
                       "UPDATE events SET payload='{\"i\":999}' WHERE id=2;",
                       nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);

    broken = -1;
    CHECK(!a.verifyChain(broken));  // 链式哈希抓住篡改
    CHECK(broken > 0);
    a.close();
  }

  // ---- T6 降级：写失败 → NDJSON 兜底 → 恢复回填（D13）----
  {
    atest::TempDir td("t6");
    atest::ManualClock clk;
    AuditLog a;
    AuditConfig cfg = baseCfg(td, 3);
    cfg.recoveryRetryMs = 0;  // 测试：立即重试恢复
    CHECK(a.open(cfg, clk));
    bool sawDegrade = false, sawRecover = false;
    a.onDegradeChanged = [&](bool deg, const std::string&) {
      if (deg) sawDegrade = true;
      else sawRecover = true;
    };

    const std::string tough =
        "{\"中文\":\"值\\\"引号\",\"brace\":{\"a\":1},\"nl\":\"x\\ny\"}";
    const std::int64_t failWall = clk.wall, failMono = clk.mono;
    a.injectWriteFailures(1);
    CHECK(a.log(cat::Therapy, ev::TherapyStart, "L2", 2, 5, tough) == 0);
    CHECK(a.degraded());
    CHECK(sawDegrade);
    CHECK(!a.isOpen());
    const std::string fbPath = cfg.dbPath + ".fallback.ndjson";
    CHECK(lineCount(fbPath) == 2);  // AUDIT_DEGRADED + THERAPY_START

    // 下一条 log → 立即恢复 + 回填
    clk.advance(50);
    const long long id = a.log(cat::Therapy, ev::Stop, "L2", 2);
    CHECK(id > 0);
    CHECK(!a.degraded());
    CHECK(sawRecover);

    const EventRow r = findEvent(a, ev::TherapyStart);
    CHECK(r.id > 0);
    CHECK(r.payload == tough);          // 复杂 payload 经 NDJSON 往返逐字节保真
    CHECK(r.slot == "L2" && r.addr == 2 && r.operatorId == 5);
    CHECK(r.wallUtc == failWall);       // 原始时间戳保留（非回填时刻）
    CHECK(r.monoMs == failMono);
    CHECK(hasEvent(a, ev::AuditDegraded));
    CHECK(hasEvent(a, ev::AuditRecovered));
    const EventRow rec = findEvent(a, ev::AuditRecovered);
    CHECK(rec.payload.find("\"replayed\":2") != std::string::npos);
    // 兜底文件已归档改名（证据不销毁）
    CHECK(!std::filesystem::exists(fbPath));
    CHECK(globExists(td.path(), "audit.db.fallback.ndjson.replayed-"));
    long long broken = -1;
    CHECK(a.verifyChain(broken));
    a.close();
  }

  // ---- T7 open 失败 → 全降级仍可用（fail-operational，D13）----
  {
    atest::TempDir td("t7");
    atest::ManualClock clk;
    // dbPath 指向一个「目录」→ sqlite 无法打开
    std::error_code ec;
    std::filesystem::create_directories(td.path() / "dbdir" / "audit.db", ec);
    AuditConfig cfg;
    cfg.dbPath = (td.path() / "dbdir" / "audit.db").string();
    cfg.fallbackPath = td.file("fb.ndjson");
    cfg.appVersion = "test";
    AuditLog a;
    CHECK(!a.open(cfg, clk));
    CHECK(a.degraded());
    a.log(cat::Therapy, ev::Pause, "L1", 1);  // 照常「记」（进兜底文件）
    CHECK(lineCount(cfg.fallbackPath) == 2);  // DEGRADED + PAUSE
    CHECK(!a.tryRecover());                   // DB 仍不可开
    CHECK(a.degraded());
  }

  // ---- T8 SUSPECT_TIME（RTC 失效 → 1970）----
  {
    atest::TempDir td("t8");
    atest::ManualClock clk;
    clk.wall = 1000;  // 1970-01-01
    AuditLog a;
    CHECK(a.open(baseCfg(td), clk));
    CHECK(hasEvent(a, ev::SuspectTime));
    a.close();
  }

  // ---- T9 CLOCK_JUMP：检测、节流、重锚定、改钟事件 ----
  {
    atest::TempDir td("t9");
    atest::ManualClock clk;
    AuditLog a;
    CHECK(a.open(baseCfg(td), clk));
    a.log(cat::Therapy, ev::Pause, "L1", 1);
    CHECK(!hasEvent(a, ev::ClockJump));

    clk.jumpWall(3600LL * 1000);  // 墙钟凭空 +1h（NTP/手动改钟）
    a.log(cat::Therapy, ev::Resume, "L1", 1);
    CHECK(countType(a, ev::ClockJump) == 1);
    const EventRow j = findEvent(a, ev::ClockJump);
    CHECK(j.payload.find("\"deviation_ms\":3600000") != std::string::npos);

    // 10s 节流窗口内再跳：不重复记，但重锚定
    clk.jumpWall(3600LL * 1000);
    clk.advance(100);
    a.log(cat::Therapy, ev::Pause, "L1", 1);
    CHECK(countType(a, ev::ClockJump) == 1);

    // 超出节流窗口再跳：记第二条
    clk.advance(20000);
    clk.jumpWall(100000);
    a.log(cat::Therapy, ev::Resume, "L1", 1);
    CHECK(countType(a, ev::ClockJump) == 2);

    // noteClockChange（设置面板改钟路径）：CLOCK_CHANGE 且不触发新 JUMP
    const long long oldW = clk.wall;
    clk.jumpWall(50000);
    a.noteClockChange(oldW, clk.wall, "manual");
    CHECK(hasEvent(a, ev::ClockChange));
    CHECK(countType(a, ev::ClockJump) == 2);
    a.close();
  }

  // ---- T10 水位告警（D16：quota=0 → 首条即告警，且只告警一次）----
  {
    atest::TempDir td("t10");
    atest::ManualClock clk;
    AuditLog a;
    AuditConfig cfg = baseCfg(td);
    cfg.quotaMb = 0;
    CHECK(a.open(cfg, clk));
    a.log(cat::Therapy, ev::Pause, "L1", 1);
    CHECK(countType(a, ev::DbWatermark) == 1);
    a.log(cat::Therapy, ev::Resume, "L1", 1);
    a.log(cat::Therapy, ev::Pause, "L1", 1);
    CHECK(countType(a, ev::DbWatermark) == 1);  // 不刷屏
    a.close();
  }

  // ---- T11 操作员名单（D14）----
  {
    atest::TempDir td("t11");
    atest::ManualClock clk;
    AuditLog a;
    CHECK(a.open(baseCfg(td), clk));
    const long long op1 = a.addOperator("张三", "A01");
    const long long op2 = a.addOperator("李四", "");
    CHECK(op1 > 0 && op2 > 0 && op2 != op1);
    CHECK(a.listOperators(true).size() == 2);
    CHECK(a.renameOperator(op1, "张三丰", "A01X"));
    auto all = a.listOperators(true);
    CHECK(all.size() == 2);
    CHECK(all[0].name == "张三丰" && all[0].code == "A01X");
    CHECK(a.setOperatorActive(op2, false));
    CHECK(a.listOperators(true).size() == 1);
    CHECK(a.listOperators(false).size() == 2);
    CHECK(countType(a, ev::OperatorAdded) == 2);
    CHECK(countType(a, ev::OperatorRenamed) == 1);
    CHECK(countType(a, ev::OperatorRemoved) == 1);
    // 事件载荷含旧→新
    const EventRow rr = findEvent(a, ev::OperatorRenamed);
    CHECK(rr.payload.find("\"name\":\"张三\"") != std::string::npos);
    CHECK(rr.payload.find("\"name\":\"张三丰\"") != std::string::npos);
    a.close();
  }

  // ---- T12 查询过滤 / 分页 / 排序（D23 后端）----
  {
    atest::TempDir td("t12");
    atest::ManualClock clk;
    AuditLog a;
    CHECK(a.open(baseCfg(td), clk));
    for (int i = 0; i < 3; ++i) {
      clk.advance(1000);
      a.log(cat::Therapy, ev::Pause, "L1", 1);
    }
    for (int i = 0; i < 2; ++i) {
      clk.advance(1000);
      a.log(cat::Fault, ev::FaultDetected, "L2", 2);
    }
    EventQuery q;
    q.category = cat::Therapy;
    q.limit = 100;
    CHECK(a.queryEvents(q).size() == 3);
    CHECK(a.countEvents(q) == 3);
    q = EventQuery{};
    q.limit = 100;
    q.sinceWall = clk.wall - 2500;  // 最近 2.5s：2×Fault + 可能 1×Therapy
    const auto recent = a.queryEvents(q);
    CHECK(recent.size() >= 2);
    for (const auto& r : recent) CHECK(r.wallUtc >= q.sinceWall);
    q = EventQuery{};
    q.limit = 2;
    q.offset = 1;
    const auto page = a.queryEvents(q);
    CHECK(page.size() == 2);
    CHECK(page[0].id == 2 && page[1].id == 3);  // ASC 偏移分页
    q = EventQuery{};
    q.limit = 3;
    q.desc = true;
    const auto newest = a.queryEvents(q);
    CHECK(newest.size() == 3);
    CHECK(newest[0].id > newest[1].id && newest[1].id > newest[2].id);
    a.close();
  }

  // ---- T13 快照 + 快照降级回放（D15）----
  {
    atest::TempDir td("t13");
    atest::ManualClock clk;
    AuditLog a;
    AuditConfig cfg = baseCfg(td, 100);
    cfg.recoveryRetryMs = 0;
    CHECK(a.open(cfg, clk));
    const std::string snap =
        "[{\"slot\":\"L1\",\"addr\":1,\"state\":3,\"targetRpm\":1800,"
        "\"actualRpm\":1750,\"tempC\":36}]";
    CHECK(a.logSnapshot(snap) > 0);
    clk.advance(30000);
    CHECK(a.logSnapshot(snap) > 0);
    auto snaps = a.querySnapshots(a.bootSeq());
    CHECK(snaps.size() == 2);
    CHECK(snaps[0].seq == 1 && snaps[1].seq == 2);
    CHECK(snaps[0].data == snap);

    a.injectWriteFailures(1);
    CHECK(a.logSnapshot(snap) == 0);  // 降级
    CHECK(a.degraded());
    clk.advance(100);
    CHECK(a.logSnapshot(snap) > 0);  // 恢复+回填+本条
    CHECK(!a.degraded());
    snaps = a.querySnapshots(a.bootSeq());
    CHECK(snaps.size() == 4);
    a.close();
  }

  // ---- T14 json_mini 往返（NDJSON 解析器的基础）----
  {
    const std::string src = "值\"引号\\反斜杠{花括号}\n换行\t制表";
    const std::string quoted = jsonQuote(src);
    const std::string line = std::string("{\"k\":") + quoted + ",\"n\":42,\"o\":{\"x\":[1,{\"y\":null}]}}";
    std::string outS;
    CHECK(jsonGetStr(line, "k", outS));
    CHECK(outS == src);
    long long outN = 0;
    CHECK(jsonGetInt(line, "n", outN));
    CHECK(outN == 42);
    std::string_view raw;
    CHECK(jsonFindRaw(line, "o", raw));
    CHECK(raw == "{\"x\":[1,{\"y\":null}]}");
    CHECK(!jsonGetInt(line, "missing", outN));
    // 嵌套对象里的同名键不误取（valueStart 找到的是顶层 "o"）
    CHECK(jsonFindRaw(line, "y", raw) && raw == "null");
  }

  return th::summary();
}
