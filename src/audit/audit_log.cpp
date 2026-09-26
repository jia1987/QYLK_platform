#include "audit/audit_log.h"

#include <sqlite3.h>

#include <cstdio>
#include <filesystem>
#include <utility>

#include "audit/json_mini.h"
#include "audit/sha256.h"

namespace massage::audit {
namespace {

constexpr long long kMinPlausibleWallMs = 1577836800000LL;  // 2020-01-01 UTC
constexpr const char* kGenesisHash =
    "0000000000000000000000000000000000000000000000000000000000000000";

// ---- Schema v1（决策 D17/D18/D19/D21）----
const char* const kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS meta(
  k TEXT PRIMARY KEY,
  v TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions(
  boot_seq   INTEGER PRIMARY KEY,
  start_wall INTEGER NOT NULL,
  start_mono INTEGER NOT NULL,
  end_wall   INTEGER,
  end_mono   INTEGER,
  end_reason TEXT,
  app_version TEXT
);
CREATE TABLE IF NOT EXISTS events(
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  wall_utc   INTEGER NOT NULL,
  mono_ms    INTEGER NOT NULL,
  boot_seq   INTEGER NOT NULL,
  session_id INTEGER NOT NULL,
  category   TEXT NOT NULL,
  type       TEXT NOT NULL,
  slot       TEXT,
  addr       INTEGER,
  operator_id INTEGER,
  payload    TEXT
);
CREATE INDEX IF NOT EXISTS idx_events_wall    ON events(wall_utc);
CREATE INDEX IF NOT EXISTS idx_events_session ON events(session_id);
CREATE INDEX IF NOT EXISTS idx_events_cat     ON events(category);
CREATE TABLE IF NOT EXISTS snapshots(
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  session_id INTEGER NOT NULL,
  wall_utc   INTEGER NOT NULL,
  mono_ms    INTEGER NOT NULL,
  seq        INTEGER NOT NULL,
  data       TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS chain_hashes(
  seq            INTEGER PRIMARY KEY,
  first_event_id INTEGER NOT NULL,
  last_event_id  INTEGER NOT NULL,
  prev_hash      TEXT NOT NULL,
  hash           TEXT NOT NULL,
  wall_utc       INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS operators(
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  name         TEXT NOT NULL,
  code         TEXT,
  active       INTEGER NOT NULL DEFAULT 1,
  created_wall INTEGER NOT NULL
);
-- 防篡改触发器（D21）：审计数据只增不改不删，触发器随 DB 文件走
CREATE TRIGGER IF NOT EXISTS events_no_delete BEFORE DELETE ON events
BEGIN SELECT RAISE(ABORT, 'audit append-only: DELETE forbidden'); END;
CREATE TRIGGER IF NOT EXISTS events_no_update BEFORE UPDATE ON events
BEGIN SELECT RAISE(ABORT, 'audit append-only: UPDATE forbidden'); END;
CREATE TRIGGER IF NOT EXISTS snapshots_no_delete BEFORE DELETE ON snapshots
BEGIN SELECT RAISE(ABORT, 'audit append-only: DELETE forbidden'); END;
CREATE TRIGGER IF NOT EXISTS snapshots_no_update BEFORE UPDATE ON snapshots
BEGIN SELECT RAISE(ABORT, 'audit append-only: UPDATE forbidden'); END;
CREATE TRIGGER IF NOT EXISTS chain_no_delete BEFORE DELETE ON chain_hashes
BEGIN SELECT RAISE(ABORT, 'audit append-only: DELETE forbidden'); END;
CREATE TRIGGER IF NOT EXISTS chain_no_update BEFORE UPDATE ON chain_hashes
BEGIN SELECT RAISE(ABORT, 'audit append-only: UPDATE forbidden'); END;
CREATE TRIGGER IF NOT EXISTS sessions_no_delete BEFORE DELETE ON sessions
BEGIN SELECT RAISE(ABORT, 'audit append-only: DELETE forbidden'); END;
)SQL";

// 链式哈希的规范化行序列化（写入方与校验方共用，NULL → 空串）
std::string canonicalRow(long long id, long long wall, long long mono, long long boot,
                         long long sess, const std::string& cat, const std::string& type,
                         const std::string& slot, long long addr, long long opId,
                         const std::string& payload) {
  std::string r;
  r.reserve(96 + cat.size() + type.size() + slot.size() + payload.size());
  auto num = [&r](long long v) {
    r += std::to_string(v);
    r += '|';
  };
  auto txt = [&r](const std::string& v) {
    r += v;
    r += '|';
  };
  num(id); num(wall); num(mono); num(boot); num(sess);
  txt(cat); txt(type); txt(slot); num(addr); num(opId); txt(payload);
  return r;
}

std::string columnText(sqlite3_stmt* st, int col) {
  const unsigned char* t = sqlite3_column_text(st, col);
  return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
}

bool fileExists(const std::string& p) {
  std::error_code ec;
  return std::filesystem::exists(p, ec);
}

long long fileSizeOf(const std::string& p) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(p, ec);
  return ec ? 0 : static_cast<long long>(sz);
}

// SQL 字符串字面量转义（category 过滤来自 UI，防御性处理单引号）
std::string sqlQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out.push_back(c);
  }
  out += "'";
  return out;
}

}  // namespace

AuditLog::AuditLog() = default;

AuditLog::~AuditLog() {
  // 有意不写 SESSION_END：未 close() 即销毁 = 崩溃语义，留给下次启动的哨兵检测（D19）
  if (db_) sqlite3_close_v2(db_);
  if (fallback_.is_open()) fallback_.close();
}

bool AuditLog::closeDbFalse() {
  if (db_) {
    sqlite3_close_v2(db_);
    db_ = nullptr;
  }
  return false;
}

bool AuditLog::execSql(const std::string& sql, std::string* err) {
  char* msg = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &msg);
  if (rc != SQLITE_OK) {
    if (err) *err = msg ? msg : "unknown error";
    sqlite3_free(msg);
    return false;
  }
  return true;
}

bool AuditLog::openDb(std::string& err) {
  // 确保父目录存在（首次部署）
  {
    std::error_code ec;
    const auto parent = std::filesystem::path(cfg_.dbPath).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  }
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(cfg_.dbPath.c_str(), &db,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (rc != SQLITE_OK) {
    err = db ? sqlite3_errmsg(db) : "sqlite3_open_v2 failed";
    if (db) sqlite3_close_v2(db);
    return false;
  }
  db_ = db;
  if (!execSql("PRAGMA journal_mode=WAL;", &err)) return closeDbFalse();
  if (!execSql("PRAGMA synchronous=FULL;", &err)) return closeDbFalse();  // D18
  if (!execSql("PRAGMA busy_timeout=3000;", &err)) return closeDbFalse();
  if (!execSql(kSchemaSql, &err)) return closeDbFalse();
  // 损坏自检：quick_check（全量 integrity_check 留给测试/维护工具）
  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA quick_check;", -1, &st, nullptr) != SQLITE_OK) {
      err = "prepare quick_check failed";
      return closeDbFalse();
    }
    std::string result;
    if (sqlite3_step(st) == SQLITE_ROW) result = columnText(st, 0);
    sqlite3_finalize(st);
    if (result != "ok") {
      err = "quick_check: " + result;
      return closeDbFalse();
    }
  }
  return true;
}

bool AuditLog::open(const AuditConfig& cfg, const IClock& clock) {
  cfg_ = cfg;
  clock_ = &clock;
  if (cfg_.fallbackPath.empty()) cfg_.fallbackPath = cfg_.dbPath + ".fallback.ndjson";
  if (cfg_.chainInterval < 1) cfg_.chainInterval = 1;

  anchorWall_ = clock_->wallMs();
  anchorMono_ = clock_->monoMs();
  lastRecoveryMono_ = anchorMono_;
  snapSeq_ = 0;
  watermarkWarned_ = false;
  insertsSinceSizeCheck_ = 64;  // 首条写入即检查一次水位，此后每 64 条

  std::string err;
  if (!openDb(err)) {
    degrade("DB 打开失败: " + err);
    return false;
  }

  // meta：schema 版本与设备信息
  {
    execSql("INSERT OR IGNORE INTO meta(k,v) VALUES('schema_version','1');");
    char* sql = sqlite3_mprintf(
        "INSERT OR REPLACE INTO meta(k,v) VALUES('device_id',%Q);"
        "INSERT OR REPLACE INTO meta(k,v) VALUES('app_version',%Q);"
        "INSERT OR REPLACE INTO meta(k,v) VALUES('quota_mb',%Q);",
        cfg_.deviceId.c_str(), cfg_.appVersion.c_str(),
        std::to_string(cfg_.quotaMb).c_str());
    execSql(sql);
    sqlite3_free(sql);
  }

  // 会话哨兵（D19）：检测上次未正常关闭的会话
  long long unclosed = 0;
  {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
                       "SELECT boot_seq FROM sessions WHERE end_reason IS NULL "
                       "ORDER BY boot_seq DESC LIMIT 1;",
                       -1, &st, nullptr);
    if (st) {
      if (sqlite3_step(st) == SQLITE_ROW) unclosed = sqlite3_column_int64(st, 0);
      sqlite3_finalize(st);
    }
  }
  if (unclosed > 0) {
    char* sql = sqlite3_mprintf(
        "UPDATE sessions SET end_reason='abnormal' WHERE boot_seq=%lld;", unclosed);
    execSql(sql);
    sqlite3_free(sql);
  }

  // 新会话行
  bootSeq_ = 0;
  {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(boot_seq),0)+1 FROM sessions;", -1, &st,
                       nullptr);
    if (st && sqlite3_step(st) == SQLITE_ROW) bootSeq_ = sqlite3_column_int64(st, 0);
    if (st) sqlite3_finalize(st);
  }
  if (bootSeq_ < 1) bootSeq_ = 1;
  {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(
        db_,
        "INSERT INTO sessions(boot_seq,start_wall,start_mono,app_version) VALUES(?,?,?,?);",
        -1, &st, nullptr);
    if (st) {
      sqlite3_bind_int64(st, 1, bootSeq_);
      sqlite3_bind_int64(st, 2, clock_->wallMs());
      sqlite3_bind_int64(st, 3, clock_->monoMs());
      sqlite3_bind_text(st, 4, cfg_.appVersion.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_step(st);
      sqlite3_finalize(st);
    }
  }

  rebuildChainState();

  if (unclosed > 0) {
    const std::string payload = "{" + jnum("old_boot", unclosed) + "," +
                                jstr("note", "上次会话未正常关闭（崩溃/断电/看门狗重启）") +
                                "}";
    writeEvent(clock_->wallMs(), clock_->monoMs(), cat::Session, ev::AbnormalTermination,
               {}, 0, 0, payload);
  }

  // 墙钟合理性（D17）：RTC 缺失/掉电回 1970 → 标记可疑时段
  if (clock_->wallMs() < kMinPlausibleWallMs) {
    const std::string payload =
        "{" + jnum("wall", clock_->wallMs()) + "," +
        jstr("note", "墙钟早于 2020-01-01，RTC 可能失效；时序以 mono_ms 为准") + "}";
    writeEvent(clock_->wallMs(), clock_->monoMs(), cat::Clock, ev::SuspectTime, {}, 0, 0,
               payload);
  }

  // 上次运行遗留的 NDJSON 兜底 → 回填（D13）
  if (fileExists(cfg_.fallbackPath)) {
    int replayed = 0, skipped = 0;
    replayNdjson(replayed, skipped);
    const std::string payload = "{" + jnum("replayed", replayed) + "," +
                                jnum("skipped", skipped) + "," + jbool("leftover", true) +
                                "}";
    writeEvent(clock_->wallMs(), clock_->monoMs(), cat::Audit, ev::AuditRecovered, {}, 0,
               0, payload);
  }

  // SESSION_START（一切就绪后）
  {
    const std::string payload = "{" + jstr("version", cfg_.appVersion) + "," +
                                jstr("device", cfg_.deviceId) + "," +
                                jnum("boot", bootSeq_) + "}";
    writeEvent(clock_->wallMs(), clock_->monoMs(), cat::Session, ev::SessionStart, {}, 0,
               0, payload);
  }

  degraded_ = false;
  hardUnavailable_ = false;
  return true;
}

void AuditLog::close() {
  if (!db_) return;
  const std::string payload = "{" + jstr("reason", "normal") + "}";
  writeEvent(clock_->wallMs(), clock_->monoMs(), cat::Session, ev::SessionEnd, {}, 0, 0,
             payload);
  char* sql = sqlite3_mprintf(
      "UPDATE sessions SET end_wall=%lld, end_mono=%lld, end_reason='normal' "
      "WHERE boot_seq=%lld;",
      static_cast<long long>(clock_->wallMs()), static_cast<long long>(clock_->monoMs()),
      bootSeq_);
  execSql(sql);
  sqlite3_free(sql);
  sqlite3_close_v2(db_);
  db_ = nullptr;
  if (fallback_.is_open()) fallback_.close();
}

// ---- 写入路径 ----

long long AuditLog::writeEvent(std::int64_t wall, std::int64_t mono,
                               std::string_view category, std::string_view type,
                               std::string_view slot, int addr, long long operatorId,
                               std::string_view payloadJson) {
  return insertEventRow(wall, mono, bootSeq_, bootSeq_, category, type, slot, addr,
                        operatorId, payloadJson);
}

long long AuditLog::insertEventRow(std::int64_t wall, std::int64_t mono, long long boot,
                                   long long sess, std::string_view cat,
                                   std::string_view type, std::string_view slot, int addr,
                                   long long opId, std::string_view payload) {
  if (!db_) return -1;
  if (injectFails_ > 0) {  // 测试缝：模拟写失败（生产不调用）
    --injectFails_;
    return -1;
  }
  sqlite3_stmt* st = nullptr;
  const int rc = sqlite3_prepare_v2(
      db_,
      "INSERT INTO events(wall_utc,mono_ms,boot_seq,session_id,category,type,"
      "slot,addr,operator_id,payload) VALUES(?,?,?,?,?,?,?,?,?,?);",
      -1, &st, nullptr);
  if (rc != SQLITE_OK) return -1;
  const std::string catS(cat), typeS(type), slotS(slot), payloadS(payload);
  sqlite3_bind_int64(st, 1, wall);
  sqlite3_bind_int64(st, 2, mono);
  sqlite3_bind_int64(st, 3, boot);
  sqlite3_bind_int64(st, 4, sess);
  sqlite3_bind_text(st, 5, catS.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 6, typeS.c_str(), -1, SQLITE_TRANSIENT);
  if (slotS.empty()) sqlite3_bind_null(st, 7);
  else sqlite3_bind_text(st, 7, slotS.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 8, addr);
  sqlite3_bind_int64(st, 9, opId);
  if (payloadS.empty()) sqlite3_bind_null(st, 10);
  else sqlite3_bind_text(st, 10, payloadS.c_str(), -1, SQLITE_TRANSIENT);

  const int stepRc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (stepRc != SQLITE_DONE) return -1;

  const long long id = sqlite3_last_insert_rowid(db_);
  appendChain(id, wall, mono, boot, sess, catS, typeS, slotS, addr, opId, payloadS);
  return id;
}

long long AuditLog::insertSnapshotRow(std::int64_t wall, std::int64_t mono, long long sess,
                                      long long seq, std::string_view data) {
  if (!db_) return -1;
  if (injectFails_ > 0) {
    --injectFails_;
    return -1;
  }
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "INSERT INTO snapshots(session_id,wall_utc,mono_ms,seq,data) VALUES(?,?,?,?,?);",
          -1, &st, nullptr) != SQLITE_OK)
    return -1;
  const std::string dataS(data);
  sqlite3_bind_int64(st, 1, sess);
  sqlite3_bind_int64(st, 2, wall);
  sqlite3_bind_int64(st, 3, mono);
  sqlite3_bind_int64(st, 4, seq);
  sqlite3_bind_text(st, 5, dataS.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) return -1;
  return sqlite3_last_insert_rowid(db_);
}

long long AuditLog::log(std::string_view category, std::string_view type,
                        std::string_view slot, int addr, long long operatorId,
                        std::string_view payloadJson) {
  if (!clock_) return 0;
  const std::int64_t wall = clock_->wallMs();
  const std::int64_t mono = clock_->monoMs();

  if (degraded_ || !db_) {
    // 降级模式（D13）：NDJSON 兜底 + 定期重试恢复
    if (mono - lastRecoveryMono_ >= cfg_.recoveryRetryMs) {
      lastRecoveryMono_ = mono;
      if (tryRecover())
        return log(category, type, slot, addr, operatorId, payloadJson);
    }
    appendNdjsonEvent(wall, mono, bootSeq_, bootSeq_, category, type, slot, addr,
                      operatorId, payloadJson);
    return 0;
  }

  checkClockJump();

  const long long id =
      writeEvent(wall, mono, category, type, slot, addr, operatorId, payloadJson);
  if (id < 0) {
    degrade(std::string("DB 写入失败: ") + sqlite3_errmsg(db_));
    appendNdjsonEvent(wall, mono, bootSeq_, bootSeq_, category, type, slot, addr,
                      operatorId, payloadJson);
    return 0;
  }
  checkWatermark();
  return id;
}

long long AuditLog::logSnapshot(std::string_view dataJson) {
  if (!clock_) return 0;
  const std::int64_t wall = clock_->wallMs();
  const std::int64_t mono = clock_->monoMs();

  if (degraded_ || !db_) {
    if (mono - lastRecoveryMono_ >= cfg_.recoveryRetryMs) {
      lastRecoveryMono_ = mono;
      if (tryRecover()) return logSnapshot(dataJson);
    }
    appendNdjsonSnapshot(wall, mono, bootSeq_, ++snapSeq_, dataJson);
    return 0;
  }
  const long long seq = ++snapSeq_;
  const long long id = insertSnapshotRow(wall, mono, bootSeq_, seq, dataJson);
  if (id < 0) {
    degrade(std::string("快照写入失败: ") + sqlite3_errmsg(db_));
    appendNdjsonSnapshot(wall, mono, bootSeq_, seq, dataJson);
    return 0;
  }
  return id;
}

// ---- 链式哈希（D21）----

void AuditLog::appendChain(long long eventId, std::int64_t wall, std::int64_t mono,
                           long long boot, long long sess, std::string_view cat,
                           std::string_view type, std::string_view slot, int addr,
                           long long opId, std::string_view payload) {
  chainBuf_ += canonicalRow(eventId, wall, mono, boot, sess, std::string(cat),
                            std::string(type), std::string(slot), addr, opId,
                            std::string(payload));
  if (++chainCount_ >= cfg_.chainInterval) flushChain(eventId, wall);
}

void AuditLog::flushChain(long long lastEventId, std::int64_t wall) {
  if (!db_ || chainCount_ <= 0) return;
  const std::string hash = sha256Hex(chainPrevHash_ + chainBuf_);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "INSERT INTO chain_hashes(seq,first_event_id,last_event_id,"
                         "prev_hash,hash,wall_utc) VALUES(?,?,?,?,?,?);",
                         -1, &st, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int64(st, 1, chainSeq_ + 1);
  sqlite3_bind_int64(st, 2, lastChainEventId_ + 1);
  sqlite3_bind_int64(st, 3, lastEventId);
  sqlite3_bind_text(st, 4, chainPrevHash_.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 6, wall);
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) return;  // 链行写失败：缓存保留，下一条事件再试
  chainSeq_ += 1;
  chainPrevHash_ = hash;
  chainBuf_.clear();
  chainCount_ = 0;
  lastChainEventId_ = lastEventId;
}

void AuditLog::rebuildChainState() {
  chainSeq_ = 0;
  chainPrevHash_ = kGenesisHash;
  chainBuf_.clear();
  chainCount_ = 0;
  lastChainEventId_ = 0;
  if (!db_) return;
  {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
                       "SELECT seq,hash,last_event_id FROM chain_hashes "
                       "ORDER BY seq DESC LIMIT 1;",
                       -1, &st, nullptr);
    if (st) {
      if (sqlite3_step(st) == SQLITE_ROW) {
        chainSeq_ = sqlite3_column_int64(st, 0);
        chainPrevHash_ = columnText(st, 1);
        lastChainEventId_ = sqlite3_column_int64(st, 2);
      }
      sqlite3_finalize(st);
    }
  }
  // 重建未完成尾段（上次 close 时 chainBuf_ 不落库，靠行数据重算）
  const long long base = lastChainEventId_;
  sqlite3_stmt* st = nullptr;
  char* sql = sqlite3_mprintf(
      "SELECT id,wall_utc,mono_ms,boot_seq,session_id,category,type,slot,addr,"
      "operator_id,payload FROM events WHERE id>%lld ORDER BY id;",
      base);
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW) {
      chainBuf_ += canonicalRow(sqlite3_column_int64(st, 0), sqlite3_column_int64(st, 1),
                                sqlite3_column_int64(st, 2), sqlite3_column_int64(st, 3),
                                sqlite3_column_int64(st, 4), columnText(st, 5),
                                columnText(st, 6), columnText(st, 7),
                                sqlite3_column_int(st, 8), sqlite3_column_int64(st, 9),
                                columnText(st, 10));
      ++chainCount_;
    }
    sqlite3_finalize(st);
  }
  sqlite3_free(sql);
}

bool AuditLog::verifyChain(long long& brokenAtEventId) {
  brokenAtEventId = 0;
  if (!db_) return false;
  std::string prevHash = kGenesisHash;
  long long prevLast = 0;
  sqlite3_stmt* cst = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT seq,first_event_id,last_event_id,prev_hash,hash "
                         "FROM chain_hashes ORDER BY seq;",
                         -1, &cst, nullptr) != SQLITE_OK)
    return false;
  bool ok = true;
  while (sqlite3_step(cst) == SQLITE_ROW) {
    const long long first = sqlite3_column_int64(cst, 1);
    const long long last = sqlite3_column_int64(cst, 2);
    const std::string storedPrev = columnText(cst, 3);
    const std::string storedHash = columnText(cst, 4);
    if (first != prevLast + 1) {  // 段不连续 = 链行被删/库被换（触发器失守场景）
      brokenAtEventId = first;
      ok = false;
      break;
    }
    if (storedPrev != prevHash) {
      brokenAtEventId = first;
      ok = false;
      break;
    }
    std::string buf;
    sqlite3_stmt* est = nullptr;
    char* sql = sqlite3_mprintf(
        "SELECT id,wall_utc,mono_ms,boot_seq,session_id,category,type,slot,addr,"
        "operator_id,payload FROM events WHERE id>%lld AND id<=%lld ORDER BY id;",
        prevLast, last);
    const int prep = sqlite3_prepare_v2(db_, sql, -1, &est, nullptr);
    sqlite3_free(sql);
    if (prep != SQLITE_OK) {
      ok = false;
      break;
    }
    long long segFirst = -1;
    while (sqlite3_step(est) == SQLITE_ROW) {
      const long long id = sqlite3_column_int64(est, 0);
      if (segFirst < 0) segFirst = id;
      buf += canonicalRow(id, sqlite3_column_int64(est, 1), sqlite3_column_int64(est, 2),
                          sqlite3_column_int64(est, 3), sqlite3_column_int64(est, 4),
                          columnText(est, 5), columnText(est, 6), columnText(est, 7),
                          sqlite3_column_int(est, 8), sqlite3_column_int64(est, 9),
                          columnText(est, 10));
    }
    sqlite3_finalize(est);
    if (sha256Hex(prevHash + buf) != storedHash) {
      brokenAtEventId = segFirst >= 0 ? segFirst : first;
      ok = false;
      break;
    }
    prevHash = storedHash;
    prevLast = last;
  }
  sqlite3_finalize(cst);
  return ok;
}

std::string AuditLog::integrityCheck() {
  if (!db_) return "no-db";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "PRAGMA integrity_check;", -1, &st, nullptr) != SQLITE_OK)
    return "prepare-failed";
  std::string result = "no-result";
  if (sqlite3_step(st) == SQLITE_ROW) result = columnText(st, 0);
  sqlite3_finalize(st);
  return result;
}

// ---- 时钟纪律（D17）----

void AuditLog::checkClockJump() {
  const std::int64_t wall = clock_->wallMs();
  const std::int64_t mono = clock_->monoMs();
  const std::int64_t expected = anchorWall_ + (mono - anchorMono_);
  const std::int64_t dev = wall - expected;
  if (dev > -cfg_.clockJumpThresholdMs && dev < cfg_.clockJumpThresholdMs) return;
  const bool throttled = lastJumpMono_ >= 0 && (mono - lastJumpMono_) < 10000;
  // 无论是否节流都重锚定（防持续偏差反复触发）
  anchorWall_ = wall;
  anchorMono_ = mono;
  if (throttled) return;
  lastJumpMono_ = mono;
  const std::string payload =
      "{" + jnum("deviation_ms", dev) + "," + jnum("expected_wall", expected) + "," +
      jnum("actual_wall", wall) + "," +
      jstr("note", "墙钟与单调钟锚点偏差超阈：NTP/手动改钟/RTC 异常") + "}";
  writeEvent(wall, mono, cat::Clock, ev::ClockJump, {}, 0, 0, payload);
}

void AuditLog::noteClockChange(std::int64_t oldWallMs, std::int64_t newWallMs,
                               std::string_view source) {
  // 先重锚定再记事件（避免本事件自身触发 CLOCK_JUMP 双记）
  anchorWall_ = clock_->wallMs();
  anchorMono_ = clock_->monoMs();
  lastJumpMono_ = -1;
  const std::string payload = "{" + jnum("old_wall", oldWallMs) + "," +
                              jnum("new_wall", newWallMs) + "," + jstr("source", source) +
                              "}";
  log(cat::Clock, ev::ClockChange, {}, 0, 0, payload);
}

// ---- 水位（D16）----

std::int64_t AuditLog::dbSizeBytes() {
  return fileSizeOf(cfg_.dbPath) + fileSizeOf(cfg_.dbPath + "-wal") +
         fileSizeOf(cfg_.dbPath + "-shm");
}

void AuditLog::checkWatermark() {
  if (watermarkWarned_) return;
  if (++insertsSinceSizeCheck_ < 64) return;
  insertsSinceSizeCheck_ = 0;
  const long long quotaBytes = static_cast<long long>(cfg_.quotaMb) * 1024 * 1024;
  const long long size = dbSizeBytes();
  if (quotaBytes > 0 && size * 5 < quotaBytes * 4) return;  // < 80%
  watermarkWarned_ = true;
  const std::string payload = "{" + jnum("size_bytes", size) + "," +
                              jnum("quota_mb", cfg_.quotaMb) + "," +
                              jstr("action", "提示导出归档；按 D16 永不自动删除") + "}";
  writeEvent(clock_->wallMs(), clock_->monoMs(), cat::Audit, ev::DbWatermark, {}, 0, 0,
             payload);
}

// ---- 降级与恢复（D13）----

void AuditLog::degrade(const std::string& reason) {
  if (degraded_) return;
  degraded_ = true;
  degradeAtMono_ = clock_ ? clock_->monoMs() : 0;
  lastRecoveryMono_ = degradeAtMono_;
  if (!fallback_.is_open())
    fallback_.open(cfg_.fallbackPath, std::ios::app | std::ios::binary);
  if (!fallback_.is_open())
    hardUnavailable_ = true;  // DB 与兜底文件都不可写：审计完全失效（UI 必须告警）
  if (db_) {
    sqlite3_close_v2(db_);
    db_ = nullptr;
  }
  appendNdjsonEvent(clock_ ? clock_->wallMs() : 0, degradeAtMono_, bootSeq_, bootSeq_,
                    cat::Audit, ev::AuditDegraded, {}, 0, 0,
                    "{" + jstr("reason", reason) + "}");
  if (onDegradeChanged) onDegradeChanged(true, reason);
}

bool AuditLog::tryRecover() {
  if (!degraded_ || !clock_) return !degraded_;
  if (fallback_.is_open()) fallback_.close();  // Windows：改名前必须释放句柄
  std::string err;
  if (!openDb(err)) {
    fallback_.open(cfg_.fallbackPath, std::ios::app | std::ios::binary);
    return false;
  }

  // 降级期间 DB 曾被重建/从未打开成功：确保本会话行存在
  if (bootSeq_ <= 0) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(boot_seq),0)+1 FROM sessions;", -1, &st,
                       nullptr);
    if (st && sqlite3_step(st) == SQLITE_ROW) bootSeq_ = sqlite3_column_int64(st, 0);
    if (st) sqlite3_finalize(st);
  }
  {
    sqlite3_stmt* st = nullptr;
    char* sql = sqlite3_mprintf("SELECT COUNT(*) FROM sessions WHERE boot_seq=%lld;",
                                bootSeq_);
    sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
    sqlite3_free(sql);
    bool hasSession = false;
    if (st) {
      if (sqlite3_step(st) == SQLITE_ROW) hasSession = sqlite3_column_int(st, 0) > 0;
      sqlite3_finalize(st);
    }
    if (!hasSession) {
      sqlite3_stmt* ins = nullptr;
      sqlite3_prepare_v2(db_,
                         "INSERT OR IGNORE INTO sessions(boot_seq,start_wall,start_mono,"
                         "app_version) VALUES(?,?,?,?);",
                         -1, &ins, nullptr);
      if (ins) {
        sqlite3_bind_int64(ins, 1, bootSeq_);
        sqlite3_bind_int64(ins, 2, clock_->wallMs());
        sqlite3_bind_int64(ins, 3, clock_->monoMs());
        sqlite3_bind_text(ins, 4, cfg_.appVersion.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_finalize(ins);
      }
    }
  }

  rebuildChainState();

  int replayed = 0, skipped = 0;
  if (fileExists(cfg_.fallbackPath)) replayNdjson(replayed, skipped);

  const std::int64_t downtime =
      degradeAtMono_ >= 0 ? clock_->monoMs() - degradeAtMono_ : 0;
  degraded_ = false;
  hardUnavailable_ = false;
  anchorWall_ = clock_->wallMs();
  anchorMono_ = clock_->monoMs();

  const std::string payload = "{" + jnum("replayed", replayed) + "," +
                              jnum("skipped", skipped) + "," +
                              jnum("downtime_ms", downtime) + "}";
  writeEvent(clock_->wallMs(), clock_->monoMs(), cat::Audit, ev::AuditRecovered, {}, 0, 0,
             payload);
  if (onDegradeChanged) onDegradeChanged(false, "recovered");
  return true;
}

void AuditLog::appendNdjsonEvent(std::int64_t wall, std::int64_t mono, long long boot,
                                 long long sess, std::string_view cat,
                                 std::string_view type, std::string_view slot, int addr,
                                 long long opId, std::string_view payload) {
  if (!fallback_.is_open()) {
    fallback_.open(cfg_.fallbackPath, std::ios::app | std::ios::binary);
    if (!fallback_.is_open()) {
      hardUnavailable_ = true;
      ++lostEvents_;
      return;
    }
    hardUnavailable_ = false;
  }
  std::string line = "{";
  line += jnum("wall", wall) + ",";
  line += jnum("mono", mono) + ",";
  line += jnum("boot", boot) + ",";
  line += jnum("sess", sess) + ",";
  line += jstr("cat", cat) + ",";
  line += jstr("type", type) + ",";
  line += slot.empty() ? std::string("\"slot\":null,") : jstr("slot", slot) + ",";
  line += jnum("addr", addr) + ",";
  line += jnum("op", opId) + ",";
  line += payload.empty() ? std::string("\"payload\":null")
                          : "\"payload\":" + std::string(payload);
  line += "}\n";
  fallback_ << line;
  fallback_.flush();
}

void AuditLog::appendNdjsonSnapshot(std::int64_t wall, std::int64_t mono, long long sess,
                                    long long seq, std::string_view data) {
  if (!fallback_.is_open()) {
    fallback_.open(cfg_.fallbackPath, std::ios::app | std::ios::binary);
    if (!fallback_.is_open()) {
      ++lostEvents_;
      return;
    }
  }
  std::string line = "{";
  line += jnum("snap", 1) + ",";
  line += jnum("wall", wall) + ",";
  line += jnum("mono", mono) + ",";
  line += jnum("sess", sess) + ",";
  line += jnum("seq", seq) + ",";
  line += "\"data\":" + std::string(data);
  line += "}\n";
  fallback_ << line;
  fallback_.flush();
}

bool AuditLog::replayNdjson(int& replayed, int& skipped) {
  replayed = 0;
  skipped = 0;
  std::FILE* f = std::fopen(cfg_.fallbackPath.c_str(), "rb");
  if (!f) return false;
  char buf[8192];
  while (std::fgets(buf, sizeof(buf), f)) {
    std::string line = buf;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.empty()) continue;

    long long snap = 0;
    const bool isSnap = jsonGetInt(line, "snap", snap) && snap == 1;
    long long wall = 0, mono = 0, sess = 0;
    jsonGetInt(line, "wall", wall);
    jsonGetInt(line, "mono", mono);
    jsonGetInt(line, "sess", sess);
    if (isSnap) {
      long long seq = 0;
      jsonGetInt(line, "seq", seq);
      std::string_view dataRaw;
      if (!jsonFindRaw(line, "data", dataRaw) || dataRaw == "null") {
        ++skipped;
        continue;
      }
      if (insertSnapshotRow(wall, mono, sess, seq, dataRaw) < 0) {
        ++skipped;
        continue;
      }
      if (seq > snapSeq_) snapSeq_ = seq;
      ++replayed;
      continue;
    }
    std::string cat, type, slot, payloadRaw;
    long long boot = 0, addr = 0, op = 0;
    if (!jsonGetStr(line, "cat", cat) || !jsonGetStr(line, "type", type)) {
      ++skipped;
      continue;
    }
    jsonGetInt(line, "boot", boot);
    jsonGetInt(line, "addr", addr);
    jsonGetInt(line, "op", op);
    jsonGetStr(line, "slot", slot);
    std::string_view raw;
    if (jsonFindRaw(line, "payload", raw) && raw != "null")
      payloadRaw.assign(raw.data(), raw.size());
    if (insertEventRow(wall, mono, boot, sess, cat, type, slot, static_cast<int>(addr), op,
                       payloadRaw) < 0) {
      ++skipped;
      continue;
    }
    ++replayed;
  }
  std::fclose(f);

  // 回填完成：兜底文件改名留档（append-only 精神：证据不销毁）
  std::error_code ec;
  const std::string archived =
      cfg_.fallbackPath + ".replayed-" + std::to_string(clock_ ? clock_->wallMs() : 0);
  std::filesystem::rename(cfg_.fallbackPath, archived, ec);
  if (ec) {
    std::filesystem::remove(cfg_.fallbackPath, ec);
    if (ec) {
      // 都失败（文件被占用等）：截断防止重复回填
      std::FILE* tf = std::fopen(cfg_.fallbackPath.c_str(), "wb");
      if (tf) std::fclose(tf);
    }
  }
  return true;
}

// ---- 操作员名单（D14）----

long long AuditLog::addOperator(const std::string& name, const std::string& code) {
  if (!db_ || name.empty()) return 0;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(
          db_, "INSERT INTO operators(name,code,active,created_wall) VALUES(?,?,1,?);", -1,
          &st, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  if (code.empty()) sqlite3_bind_null(st, 2);
  else sqlite3_bind_text(st, 2, code.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 3, clock_->wallMs());
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) return 0;
  const long long id = sqlite3_last_insert_rowid(db_);
  log(cat::Access, ev::OperatorAdded, {}, 0, 0,
      "{" + jnum("op_id", id) + "," + jstr("name", name) + "," + jstr("code", code) + "}");
  return id;
}

bool AuditLog::renameOperator(long long id, const std::string& name,
                              const std::string& code) {
  if (!db_ || id <= 0 || name.empty()) return false;
  OperatorRow old;
  bool found = false;
  for (const auto& o : listOperators(false))
    if (o.id == id) {
      old = o;
      found = true;
      break;
    }
  if (!found) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "UPDATE operators SET name=?, code=? WHERE id=?;", -1, &st,
                         nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  if (code.empty()) sqlite3_bind_null(st, 2);
  else sqlite3_bind_text(st, 2, code.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 3, id);
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) return false;
  log(cat::Access, ev::OperatorRenamed, {}, 0, 0,
      "{" + jnum("op_id", id) + ",\"old\":{" + jstr("name", old.name) + "," +
          jstr("code", old.code) + "},\"new\":{" + jstr("name", name) + "," +
          jstr("code", code) + "}}");
  return true;
}

bool AuditLog::setOperatorActive(long long id, bool active) {
  if (!db_ || id <= 0) return false;
  char* sql =
      sqlite3_mprintf("UPDATE operators SET active=%d WHERE id=%lld;", active ? 1 : 0, id);
  const bool ok = execSql(sql);
  sqlite3_free(sql);
  if (!ok) return false;
  log(cat::Access, active ? ev::OperatorAdded : ev::OperatorRemoved, {}, 0, id,
      "{" + jnum("op_id", id) + "," + jbool("active", active) + "}");
  return true;
}

std::vector<OperatorRow> AuditLog::listOperators(bool activeOnly) {
  std::vector<OperatorRow> out;
  if (!db_) return out;
  sqlite3_stmt* st = nullptr;
  const char* sql = activeOnly
                        ? "SELECT id,name,code,active FROM operators WHERE active=1 ORDER BY id;"
                        : "SELECT id,name,code,active FROM operators ORDER BY id;";
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
  while (sqlite3_step(st) == SQLITE_ROW) {
    OperatorRow o;
    o.id = sqlite3_column_int64(st, 0);
    o.name = columnText(st, 1);
    o.code = columnText(st, 2);
    o.active = sqlite3_column_int(st, 3) != 0;
    out.push_back(std::move(o));
  }
  sqlite3_finalize(st);
  return out;
}

// ---- 查询（D23）----

namespace {
std::string buildWhere(const EventQuery& q) {
  std::string w = " WHERE 1=1";
  if (!q.category.empty()) w += " AND category=" + sqlQuote(q.category);
  if (q.sinceWall >= 0) w += " AND wall_utc>=" + std::to_string(q.sinceWall);
  if (q.untilWall >= 0) w += " AND wall_utc<=" + std::to_string(q.untilWall);
  return w;
}
}  // namespace

std::vector<EventRow> AuditLog::queryEvents(const EventQuery& q) {
  std::vector<EventRow> out;
  if (!db_) return out;
  std::string sql =
      "SELECT id,wall_utc,mono_ms,boot_seq,session_id,category,type,slot,addr,"
      "operator_id,payload FROM events" +
      buildWhere(q);
  sql += q.desc ? " ORDER BY id DESC" : " ORDER BY id ASC";
  sql += " LIMIT " + std::to_string(q.limit < 0 ? 200 : q.limit);
  if (q.offset > 0) sql += " OFFSET " + std::to_string(q.offset);
  sql += ";";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
  while (sqlite3_step(st) == SQLITE_ROW) {
    EventRow r;
    r.id = sqlite3_column_int64(st, 0);
    r.wallUtc = sqlite3_column_int64(st, 1);
    r.monoMs = sqlite3_column_int64(st, 2);
    r.bootSeq = sqlite3_column_int64(st, 3);
    r.sessionId = sqlite3_column_int64(st, 4);
    r.category = columnText(st, 5);
    r.type = columnText(st, 6);
    r.slot = columnText(st, 7);
    r.addr = sqlite3_column_int(st, 8);
    r.operatorId = sqlite3_column_int64(st, 9);
    r.payload = columnText(st, 10);
    out.push_back(std::move(r));
  }
  sqlite3_finalize(st);
  return out;
}

long long AuditLog::countEvents(const EventQuery& q) {
  if (!db_) return 0;
  const std::string sql = "SELECT COUNT(*) FROM events" + buildWhere(q) + ";";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return 0;
  long long n = 0;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
  sqlite3_finalize(st);
  return n;
}

std::vector<SnapshotRow> AuditLog::querySnapshots(long long sessionId, int limit) {
  std::vector<SnapshotRow> out;
  if (!db_) return out;
  sqlite3_stmt* st = nullptr;
  char* sql = sqlite3_mprintf(
      "SELECT id,session_id,wall_utc,mono_ms,seq,data FROM snapshots "
      "WHERE session_id=%lld ORDER BY seq LIMIT %d;",
      sessionId, limit);
  const int rc = sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
  sqlite3_free(sql);
  if (rc != SQLITE_OK) return out;
  while (sqlite3_step(st) == SQLITE_ROW) {
    SnapshotRow r;
    r.id = sqlite3_column_int64(st, 0);
    r.sessionId = sqlite3_column_int64(st, 1);
    r.wallUtc = sqlite3_column_int64(st, 2);
    r.monoMs = sqlite3_column_int64(st, 3);
    r.seq = sqlite3_column_int64(st, 4);
    r.data = columnText(st, 5);
    out.push_back(std::move(r));
  }
  sqlite3_finalize(st);
  return out;
}

std::vector<SnapshotRow> AuditLog::querySnapshotsRange(long long sinceWall,
                                                       long long untilWall, int limit) {
  std::vector<SnapshotRow> out;
  if (!db_) return out;
  std::string sql =
      "SELECT id,session_id,wall_utc,mono_ms,seq,data FROM snapshots WHERE 1=1";
  if (sinceWall >= 0) sql += " AND wall_utc>=" + std::to_string(sinceWall);
  if (untilWall >= 0) sql += " AND wall_utc<=" + std::to_string(untilWall);
  sql += " ORDER BY id LIMIT " + std::to_string(limit) + ";";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
  while (sqlite3_step(st) == SQLITE_ROW) {
    SnapshotRow r;
    r.id = sqlite3_column_int64(st, 0);
    r.sessionId = sqlite3_column_int64(st, 1);
    r.wallUtc = sqlite3_column_int64(st, 2);
    r.monoMs = sqlite3_column_int64(st, 3);
    r.seq = sqlite3_column_int64(st, 4);
    r.data = columnText(st, 5);
    out.push_back(std::move(r));
  }
  sqlite3_finalize(st);
  return out;
}

}  // namespace massage::audit
