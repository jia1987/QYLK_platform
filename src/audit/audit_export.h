#pragma once
// 审计导出引擎（决策 D22）：CSV（人读/Excel）+ JSON（机读）+ SHA256 清单。
// 纯 C++（与 AuditLog 同库同待遇，双平台单测）；导出动作本身由本模块写入
// EXPORT 审计事件（含范围、条数、逐文件哈希、链校验结果）。
//
// 产物文件名（<ts> = 导出时刻 UTC 紧凑格式 YYYYMMDD-HHMMSS）：
//   audit_events_<ts>.csv      —— 事件表（含 BOM，Excel 直接打开不乱码）
//   audit_full_<ts>.json       —— 元信息 + 全部事件 + 会话快照（机读）
//   audit_manifest_<ts>.json   —— 清单：设备标识/范围/条数/逐文件 SHA256/链校验
#include <string>

#include "audit/audit_log.h"

namespace massage::audit {

struct ExportOptions {
  std::string destDir;            // 目标目录（U 盘挂载点/任意可写目录）
  long long sinceWall = -1;       // -1 = 不限（全量）
  long long untilWall = -1;
  std::string deviceId = "DEV-UNSET";
  std::string appVersion = "unknown";
};

struct ExportResult {
  bool ok = false;
  std::string error;              // ok=false 时的原因
  std::string csvPath;
  std::string jsonPath;
  std::string manifestPath;
  std::string csvSha256;
  std::string jsonSha256;
  std::string manifestSha256;
  long long eventCount = 0;
  long long snapshotCount = 0;
  bool chainVerified = false;     // 导出时的链校验结果（写入清单，注册证据）
};

ExportResult exportAudit(AuditLog& log, const ExportOptions& opt);

// epoch ms → "YYYY-MM-DDTHH:MM:SS.mmmZ"（UTC，纯算法无 Qt/无 tz 库）
std::string iso8601Utc(long long epochMs);

}  // namespace massage::audit
