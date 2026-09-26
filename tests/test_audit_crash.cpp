// 断电/崩溃完整性测试（D18/D19 的可执行证据，DoD#1）：
//   子进程：打开审计 DB → 写 7 条事件（每条 synchronous=FULL 落盘）→ _Exit 硬杀
//           （不跑析构、不跑 atexit、不 close —— 与拔电源等价）
//   父进程：重开同一 DB → WAL 自动恢复 → 7 条事件全在、链式哈希可验证、
//           integrity_check=ok、会话哨兵补记 ABNORMAL_TERMINATION。
// ctest 通过生成器表达式把本 exe 的完整路径作为 argv[1] 传入（避免路径解析歧义）。
#include <cstdlib>
#include <string>

#include "audit/audit_log.h"
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

std::string shellQuote(const std::string& path) {
#ifdef _WIN32
  return "\"" + path + "\"";
#else
  return "'" + path + "'";
#endif
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "--child") {
    // ---- 子进程：写入后硬退出（崩溃语义）----
    SystemClock clk;
    AuditConfig cfg;
    cfg.dbPath = argv[2];
    cfg.chainInterval = 3;
    cfg.appVersion = "crash-child";
    AuditLog a;
    if (!a.open(cfg, clk)) return 2;
    for (int i = 0; i < 7; ++i)
      a.log(cat::Therapy, ev::TherapyStart, "L1", 1, 0,
            std::string("{\"i\":") + std::to_string(i) + "}");
    std::fflush(nullptr);
    std::_Exit(9);  // 不走任何清理路径
  }

  // ---- 父进程 ----
  std::string self = (argc >= 2) ? argv[1] : "test_audit_crash";
#ifndef _WIN32
  if (self.find('/') == std::string::npos) self = "./" + self;
#endif
  atest::TempDir td("crash");
  const std::string db = td.file("audit.db");
  std::string cmd = shellQuote(self) + " --child " + shellQuote(db);
#ifdef _WIN32
  // cmd.exe /c 的剥引号规则：命令串以引号开头且含多对引号时，首尾引号会被剥掉
  // 导致「文件名、目录名或卷标语法不正确」——整体再包一层是标准解法
  cmd = "\"" + cmd + "\"";
#endif
  const int rc = std::system(cmd.c_str());
  // rc 语义随平台（Windows=退出码 / POSIX=wait 编码），只确认子进程确实跑过
  CHECK(rc != -1);

  // 重开：WAL 恢复 + 哨兵 + 链完整性
  SystemClock clk2;
  AuditLog b;
  AuditConfig cfg2;
  cfg2.dbPath = db;
  cfg2.chainInterval = 3;
  cfg2.appVersion = "crash-parent";
  CHECK(b.open(cfg2, clk2));
  CHECK(b.bootSeq() == 2);
  CHECK(hasEvent(b, ev::AbnormalTermination));  // D19：崩溃留痕

  EventQuery q;
  q.category = cat::Therapy;
  q.limit = 100;
  const auto rows = b.queryEvents(q);
  CHECK(rows.size() == 7);  // D18：断电丢失窗口 = 0
  for (int i = 0; i < 7 && i < static_cast<int>(rows.size()); ++i)
    CHECK(rows[static_cast<std::size_t>(i)].payload ==
          std::string("{\"i\":") + std::to_string(i) + "}");

  long long broken = -1;
  CHECK(b.verifyChain(broken));          // D21：链式哈希穿越崩溃仍可验证
  CHECK(b.integrityCheck() == "ok");     // DB 物理完整性
  b.close();
  return th::summary();
}
