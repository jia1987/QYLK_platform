#pragma once
// AuditLog —— 审计追踪引擎（决策 D13–D22，设计方案 §7）。
// 纯 C++17，零 Qt 依赖（D29：sqlite3 amalgamation 静态嵌入）。
//
// 核心保证：
//  - WAL + synchronous=FULL：断电丢失窗口 = 0（D18）
//  - 三列时间戳 wall_utc + mono_ms + boot_seq（D17）
//  - 触发器防 DELETE/UPDATE + 每 N 条链式 SHA256（D21）
//  - 会话哨兵：上次未正常关闭 → ABNORMAL_TERMINATION 补记（D19）
//  - DB 写失败 → fail-operational：自动降级 NDJSON 文件兜底，恢复后回填（D13）
//  - 水位监控：超配额 80% 记 DB_WATERMARK（D16，永不删除）
//
// 线程模型：单线程使用（与 AppCore 主线程模型一致），不承诺线程安全。
#include <cstdint>
#include <functional>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "audit/audit_clock.h"
#include "audit/audit_event.h"

struct sqlite3;

namespace massage::audit {

struct AuditConfig {
  std::string dbPath;
  std::string fallbackPath;  // 空 = dbPath + ".fallback.ndjson"
  std::string deviceId = "DEV-UNSET";   // T3 板卡冻结后填设备 SN
  std::string appVersion = "unknown";
  int quotaMb = 1024;                    // D16 水位配额
  int chainInterval = 1000;              // D21 链式哈希间隔（条）
  std::int64_t clockJumpThresholdMs = 5000;   // D17 跳变判定阈值
  std::int64_t recoveryRetryMs = 30000;       // D13 降级期恢复重试间隔
};

class AuditLog {
 public:
  AuditLog();
  // 注意：析构不写 SESSION_END（等价崩溃语义，供哨兵检测）；正常退出必须调 close()
  ~AuditLog();
  AuditLog(const AuditLog&) = delete;
  AuditLog& operator=(const AuditLog&) = delete;

  // 返回 false = DB 不可用，已进入全降级模式（仍可 log：写 NDJSON 并定期重试恢复）
  bool open(const AuditConfig& cfg, const IClock& clock);
  void close();  // 正常会话结束（SESSION_END + end_reason='normal'）

  bool isOpen() const { return db_ != nullptr && !degraded_; }
  bool degraded() const { return degraded_; }
  bool unavailable() const { return hardUnavailable_; }  // DB 与兜底文件都写不了
  long long lostEvents() const { return lostEvents_; }
  long long bootSeq() const { return bootSeq_; }
  std::int64_t dbSizeBytes();

  // ---- 写入 ----
  // 返回事件 id；降级期间返回 0（已写入 NDJSON 兜底）
  long long log(std::string_view category, std::string_view type,
                std::string_view slot = {}, int addr = 0,
                long long operatorId = 0, std::string_view payloadJson = {});
  long long logSnapshot(std::string_view dataJson);  // 30s 会话快照（D15）

  // ---- 时钟纪律（D17）----
  void noteClockChange(std::int64_t oldWallMs, std::int64_t newWallMs,
                       std::string_view source);

  // ---- 操作员名单（D14）----
  long long addOperator(const std::string& name, const std::string& code);
  bool renameOperator(long long id, const std::string& name, const std::string& code);
  bool setOperatorActive(long long id, bool active);  // 软删（变更均入审计）
  std::vector<OperatorRow> listOperators(bool activeOnly);

  // ---- 查询（D23 UI 用）----
  std::vector<EventRow> queryEvents(const EventQuery& q);
  long long countEvents(const EventQuery& q);
  std::vector<SnapshotRow> querySnapshots(long long sessionId, int limit = 4096);

  // ---- 完整性（D21）----
  bool verifyChain(long long& brokenAtEventId);
  std::string integrityCheck();  // PRAGMA integrity_check（"ok" = 健康）

  // ---- 降级恢复（D13）----
  bool tryRecover();  // 降级态下立即尝试恢复 DB 并回填 NDJSON

  // ---- 测试缝（仅测试使用；生产代码不得调用）----
  void injectWriteFailures(int n) { injectFails_ = n; }

  // 降级状态变化回调（AppCore → UI 醒目告警，D13）
  std::function<void(bool degraded, const std::string& reason)> onDegradeChanged;

 private:
  bool openDb(std::string& err);
  bool closeDbFalse();
  bool execSql(const std::string& sql, std::string* err = nullptr);
  // 内部直写（不做时钟跳变/水位检查——防递归）；返回事件 id，失败 -1
  long long writeEvent(std::int64_t wall, std::int64_t mono, std::string_view category,
                       std::string_view type, std::string_view slot, int addr,
                       long long operatorId, std::string_view payloadJson);
  long long insertEventRow(std::int64_t wall, std::int64_t mono, long long boot,
                           long long sess, std::string_view cat, std::string_view type,
                           std::string_view slot, int addr, long long opId,
                           std::string_view payload);
  long long insertSnapshotRow(std::int64_t wall, std::int64_t mono, long long sess,
                              long long seq, std::string_view data);
  void appendChain(long long eventId, std::int64_t wall, std::int64_t mono,
                   long long boot, long long sess, std::string_view cat,
                   std::string_view type, std::string_view slot, int addr,
                   long long opId, std::string_view payload);
  void flushChain(long long lastEventId, std::int64_t wall);
  void rebuildChainState();
  void checkClockJump();
  void checkWatermark();
  void degrade(const std::string& reason);
  void appendNdjsonEvent(std::int64_t wall, std::int64_t mono, long long boot,
                         long long sess, std::string_view cat, std::string_view type,
                         std::string_view slot, int addr, long long opId,
                         std::string_view payload);
  void appendNdjsonSnapshot(std::int64_t wall, std::int64_t mono, long long sess,
                            long long seq, std::string_view data);
  bool replayNdjson(int& replayed, int& skipped);

  AuditConfig cfg_;
  const IClock* clock_ = nullptr;
  sqlite3* db_ = nullptr;
  long long bootSeq_ = 0;
  bool degraded_ = false;
  bool hardUnavailable_ = false;
  long long lostEvents_ = 0;

  // 链式哈希状态（D21）
  long long chainSeq_ = 0;
  std::string chainPrevHash_;
  std::string chainBuf_;
  int chainCount_ = 0;
  long long lastChainEventId_ = 0;

  // 时钟锚点（D17）
  std::int64_t anchorWall_ = 0;
  std::int64_t anchorMono_ = 0;
  std::int64_t lastJumpMono_ = -1;

  // 降级/恢复（D13）
  std::ofstream fallback_;
  std::int64_t degradeAtMono_ = -1;
  std::int64_t lastRecoveryMono_ = 0;

  // 其他状态
  bool watermarkWarned_ = false;
  int insertsSinceSizeCheck_ = 0;
  long long snapSeq_ = 0;
  int injectFails_ = 0;
};

}  // namespace massage::audit
