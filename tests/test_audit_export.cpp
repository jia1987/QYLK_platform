// 审计导出引擎单测（M4b / 决策 D22，DoD#3）：
//   E1 全量导出：BOM/表头/转义、JSON 结构、清单哈希=独立重算、EXPORT 事件留痕
//   E2 时间范围导出   E3 目标不可写   E4 iso8601Utc 已知值
#include <filesystem>
#include <fstream>
#include <string>

#include "audit/audit_export.h"
#include "audit/sha256.h"
#include "audit_test_util.h"
#include "test_harness.h"

using namespace massage::audit;

namespace {

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return {};
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  // ---- E4（先测纯函数）iso8601Utc 已知值 ----
  CHECK(iso8601Utc(0) == "1970-01-01T00:00:00.000Z");
  CHECK(iso8601Utc(1000000000000LL) == "2001-09-09T01:46:40.000Z");
  CHECK(iso8601Utc(1709164800000LL) == "2024-02-29T00:00:00.000Z");  // 闰日
  CHECK(iso8601Utc(1758844799999LL) == "2025-09-25T23:59:59.999Z");

  // ---- E1 全量导出 ----
  atest::TempDir td("e1");
  atest::ManualClock clk;
  AuditLog a;
  AuditConfig cfg;
  cfg.dbPath = td.file("audit.db");
  cfg.chainInterval = 10;
  cfg.appVersion = "exp-test";
  CHECK(a.open(cfg, clk));

  // 恶劣 payload：中文、逗号、双引号、换行、嵌套花括号（CSV/JSON 转义 torture）
  const std::string nasty =
      "{\"note\":\"值,\\\"引号\\\"\\n换行\",\"brace\":{\"a\":[1,2]}}";
  for (int i = 0; i < 25; ++i) {
    clk.advance(1000);
    a.log(cat::Therapy, ev::TherapyStart, "L1", 1, 0,
          i == 3 ? nasty : std::string("{\"i\":") + std::to_string(i) + "}");
  }
  a.logSnapshot("[{\"slot\":\"L1\",\"targetRpm\":1800}]");

  ExportOptions o;
  o.destDir = td.path().string();
  o.deviceId = "TEST-01";
  o.appVersion = "exp-test";
  const ExportResult r = exportAudit(a, o);
  CHECK(r.ok);
  if (!r.ok) std::printf("  export error: %s\n", r.error.c_str());
  CHECK(r.eventCount == 26);  // SESSION_START + 25（EXPORT 事件在文件生成后写入）
  CHECK(r.snapshotCount == 1);
  CHECK(r.chainVerified);
  CHECK(std::filesystem::exists(r.csvPath));
  CHECK(std::filesystem::exists(r.jsonPath));
  CHECK(std::filesystem::exists(r.manifestPath));

  // CSV：BOM + 表头 + RFC4180 转义（内嵌引号翻倍、含换行字段带引号）
  const std::string csv = readFile(r.csvPath);
  CHECK(csv.rfind("\xEF\xBB\xBF", 0) == 0);
  CHECK(csv.find("id,wall_utc,wall_utc_ms,mono_ms,boot_seq,session_id,category,type,"
                 "slot,addr,operator_id,payload\r\n") == 3);
  CHECK(csv.find("THERAPY_START") != std::string::npos);
  // nasty payload 的 CSV 形态：引号翻倍、反斜杠原样保留
  CHECK(csv.find("\"\"note\"\"") != std::string::npos);   // "note" → ""note""
  CHECK(csv.find("\\\"\"引号") != std::string::npos);      // \" → \""（反斜杠不动，引号翻倍）
  CHECK(csv.find("\\n换行") != std::string::npos);         // \n 两字符原样保留

  // JSON：meta/events/snapshots 结构 + 链校验标记 + nasty 原样内嵌
  const std::string js = readFile(r.jsonPath);
  CHECK(js.find("\"export_meta\"") != std::string::npos);
  CHECK(js.find("\"manifest\":\"massage-audit-export-v1\"") != std::string::npos);
  CHECK(js.find("\"event_count\":26") != std::string::npos);
  CHECK(js.find("\"snapshot_count\":1") != std::string::npos);
  CHECK(js.find("\"chain_verified\":true") != std::string::npos);
  CHECK(js.find("\"device_id\":\"TEST-01\"") != std::string::npos);
  CHECK(js.find("\"snapshots\":[{") != std::string::npos);
  CHECK(js.find("\\n换行") != std::string::npos);
  CHECK(js.find("\"payload\":{\"note\"") != std::string::npos);  // 合法 JSON 内嵌为对象

  // 清单：含两个数据文件哈希，且与独立重算一致
  const std::string man = readFile(r.manifestPath);
  CHECK(man.find("massage-audit-export-manifest-v1") != std::string::npos);
  CHECK(man.find(r.csvSha256) != std::string::npos);
  CHECK(man.find(r.jsonSha256) != std::string::npos);
  CHECK(sha256FileHex(r.csvPath) == r.csvSha256);
  CHECK(sha256FileHex(r.jsonPath) == r.jsonSha256);
  CHECK(sha256FileHex(r.manifestPath) == r.manifestSha256);
  CHECK(r.csvSha256.size() == 64);

  // EXPORT 事件已入审计（含逐文件哈希 + 链校验结果）
  {
    EventQuery q;
    q.category = cat::Audit;
    q.limit = 100;
    const auto rows = a.queryEvents(q);
    const EventRow* ex = nullptr;
    for (const auto& row : rows)
      if (row.type == ev::Export) ex = &row;
    CHECK(ex != nullptr);
    if (ex) {
      CHECK(ex->payload.find(r.csvSha256) != std::string::npos);
      CHECK(ex->payload.find(r.manifestSha256) != std::string::npos);
      CHECK(ex->payload.find("\"chain_verified\":true") != std::string::npos);
      CHECK(ex->payload.find("\"events\":26") != std::string::npos);
    }
  }
  // 导出后链仍可验证（EXPORT 事件已入链）
  long long broken = -1;
  CHECK(a.verifyChain(broken));

  // ---- E2 时间范围导出（近段过滤：事件写在 wall-25s..wall，取最近 5s）----
  ExportOptions o2 = o;
  o2.sinceWall = clk.wall - 5000;
  const ExportResult r2 = exportAudit(a, o2);
  CHECK(r2.ok);
  CHECK(r2.eventCount > 0);
  CHECK(r2.eventCount < r.eventCount);  // 明显少于全量

  // ---- E3 目标目录不存在 ----
  ExportOptions o3 = o;
  o3.destDir = td.file("no_such_dir");
  const ExportResult r3 = exportAudit(a, o3);
  CHECK(!r3.ok);
  CHECK(!r3.error.empty());

  a.close();
  return th::summary();
}
