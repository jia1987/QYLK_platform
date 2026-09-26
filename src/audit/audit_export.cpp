#include "audit/audit_export.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "audit/json_mini.h"
#include "audit/sha256.h"

namespace massage::audit {
namespace {

// CSV 字段转义（RFC4180）：含分隔符/引号/换行 → 双引号包裹，内部引号翻倍
std::string csvField(const std::string& s) {
  if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += "\"\"";
    else out.push_back(c);
  }
  out += "\"";
  return out;
}

bool looksLikeJson(const std::string& s) {
  if (s.size() < 2) return false;
  return (s.front() == '{' && s.back() == '}') || (s.front() == '[' && s.back() == ']');
}

bool writeAll(const std::string& path, const std::string& content) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) return false;
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  f.flush();
  return f.good();
}

std::int64_t fileSizeOf(const std::string& p) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(p, ec);
  return ec ? -1 : static_cast<std::int64_t>(sz);
}

}  // namespace

std::string iso8601Utc(long long epochMs) {
  // civil_from_days（Howard Hinnant 算法）：epoch 天数 → 公历年月日，纯算术无依赖
  long long days = epochMs / 86400000LL;
  long long rem = epochMs % 86400000LL;
  if (rem < 0) {
    rem += 86400000LL;
    --days;
  }
  const int hh = static_cast<int>(rem / 3600000);
  const int mi = static_cast<int>(rem % 3600000 / 60000);
  const int ss = static_cast<int>(rem % 60000 / 1000);
  const int ms = static_cast<int>(rem % 1000);

  const long long z = days + 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long long y = static_cast<long long>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) ++y;

  char buf[48];
  std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02d:%02d:%02d.%03dZ", y, m, d,
                hh, mi, ss, ms);
  return buf;
}

ExportResult exportAudit(AuditLog& log, const ExportOptions& opt) {
  ExportResult res;
  if (opt.destDir.empty()) {
    res.error = "目标目录为空";
    return res;
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(opt.destDir, ec)) {
    res.error = "目标目录不存在: " + opt.destDir;
    return res;
  }

  // 导出时点自检：链完整性（结果写入清单——注册现场的完整性证据，D21/D22）
  long long broken = 0;
  res.chainVerified = log.verifyChain(broken);

  EventQuery q;
  q.sinceWall = opt.sinceWall;
  q.untilWall = opt.untilWall;
  q.limit = 100000000;
  const std::vector<EventRow> events = log.queryEvents(q);
  const std::vector<SnapshotRow> snaps =
      log.querySnapshotsRange(opt.sinceWall, opt.untilWall);
  res.eventCount = static_cast<long long>(events.size());
  res.snapshotCount = static_cast<long long>(snaps.size());

  const auto nowMs = static_cast<long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  // 文件名时间戳：UTC 紧凑格式 YYYYMMDD-HHMMSS
  const std::string iso = iso8601Utc(nowMs);  // "2026-09-26T12:34:56.789Z"
  std::string ts = "00000000-000000";
  if (iso.size() >= 19) {
    ts = iso.substr(0, 4) + iso.substr(5, 2) + iso.substr(8, 2) + "-" +
         iso.substr(11, 2) + iso.substr(14, 2) + iso.substr(17, 2);
  }
  const std::string csvName = "audit_events_" + ts + ".csv";
  const std::string jsonName = "audit_full_" + ts + ".json";
  const std::string manName = "audit_manifest_" + ts + ".json";
  res.csvPath = (std::filesystem::path(opt.destDir) / csvName).string();
  res.jsonPath = (std::filesystem::path(opt.destDir) / jsonName).string();
  res.manifestPath = (std::filesystem::path(opt.destDir) / manName).string();

  // ---- CSV（含 BOM：Excel 直接打开 UTF-8 不乱码）----
  std::string csv = "\xEF\xBB\xBF";
  csv += "id,wall_utc,wall_utc_ms,mono_ms,boot_seq,session_id,category,type,slot,addr,"
         "operator_id,payload\r\n";
  for (const auto& r : events) {
    csv += std::to_string(r.id) + ",";
    csv += csvField(iso8601Utc(r.wallUtc)) + ",";
    csv += std::to_string(r.wallUtc) + ",";
    csv += std::to_string(r.monoMs) + ",";
    csv += std::to_string(r.bootSeq) + ",";
    csv += std::to_string(r.sessionId) + ",";
    csv += csvField(r.category) + ",";
    csv += csvField(r.type) + ",";
    csv += csvField(r.slot) + ",";
    csv += std::to_string(r.addr) + ",";
    csv += std::to_string(r.operatorId) + ",";
    csv += csvField(r.payload) + "\r\n";
  }

  // ---- JSON（机读全量：meta + events + snapshots）----
  std::string js = "{\"export_meta\":{";
  js += jstr("manifest", "massage-audit-export-v1") + ",";
  js += jstr("device_id", opt.deviceId) + ",";
  js += jstr("app_version", opt.appVersion) + ",";
  js += jnum("generated_wall_ms", nowMs) + ",";
  js += jstr("generated_iso", iso8601Utc(nowMs)) + ",";
  js += "\"range\":{" + jnum("since", opt.sinceWall) + "," + jnum("until", opt.untilWall) + "},";
  js += jnum("event_count", res.eventCount) + ",";
  js += jnum("snapshot_count", res.snapshotCount) + ",";
  js += jbool("chain_verified", res.chainVerified);
  js += "},\"events\":[";
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& r = events[i];
    if (i) js += ",";
    js += "{";
    js += jnum("id", r.id) + ",";
    js += jnum("wall_utc_ms", r.wallUtc) + ",";
    js += jstr("wall_utc_iso", iso8601Utc(r.wallUtc)) + ",";
    js += jnum("mono_ms", r.monoMs) + ",";
    js += jnum("boot_seq", r.bootSeq) + ",";
    js += jnum("session_id", r.sessionId) + ",";
    js += jstr("category", r.category) + ",";
    js += jstr("type", r.type) + ",";
    js += r.slot.empty() ? std::string("\"slot\":null,") : jstr("slot", r.slot) + ",";
    js += jnum("addr", r.addr) + ",";
    js += jnum("operator_id", r.operatorId) + ",";
    // payload 为合法 JSON 对象/数组时原样内嵌（机读友好）；否则退化为字符串
    if (r.payload.empty()) js += "\"payload\":null";
    else if (looksLikeJson(r.payload)) js += "\"payload\":" + r.payload;
    else js += jstr("payload", r.payload);
    js += "}";
  }
  js += "],\"snapshots\":[";
  for (std::size_t i = 0; i < snaps.size(); ++i) {
    const auto& s = snaps[i];
    if (i) js += ",";
    js += "{";
    js += jnum("id", s.id) + ",";
    js += jnum("session_id", s.sessionId) + ",";
    js += jnum("wall_utc_ms", s.wallUtc) + ",";
    js += jnum("mono_ms", s.monoMs) + ",";
    js += jnum("seq", s.seq) + ",";
    js += looksLikeJson(s.data) ? "\"data\":" + s.data : jstr("data", s.data);
    js += "}";
  }
  js += "]}\n";

  // ---- 写文件（先数据文件，后清单）----
  if (!writeAll(res.csvPath, csv)) {
    res.error = "CSV 写入失败（U盘写保护/空间不足？）: " + res.csvPath;
    return res;
  }
  if (!writeAll(res.jsonPath, js)) {
    res.error = "JSON 写入失败: " + res.jsonPath;
    return res;
  }
  res.csvSha256 = sha256FileHex(res.csvPath);
  res.jsonSha256 = sha256FileHex(res.jsonPath);
  if (res.csvSha256.empty() || res.jsonSha256.empty()) {
    res.error = "哈希计算失败（文件回读异常）";
    return res;
  }

  std::string man = "{\"manifest\":\"massage-audit-export-manifest-v1\",";
  man += jstr("device_id", opt.deviceId) + ",";
  man += jstr("generated_iso", iso8601Utc(nowMs)) + ",";
  man += "\"range\":{" + jnum("since", opt.sinceWall) + "," + jnum("until", opt.untilWall) + "},";
  man += jnum("event_count", res.eventCount) + ",";
  man += jnum("snapshot_count", res.snapshotCount) + ",";
  man += jbool("chain_verified", res.chainVerified) + ",";
  man += "\"files\":[";
  man += "{" + jstr("name", csvName) + "," + jnum("bytes", fileSizeOf(res.csvPath)) +
         "," + jstr("sha256", res.csvSha256) + "},";
  man += "{" + jstr("name", jsonName) + "," + jnum("bytes", fileSizeOf(res.jsonPath)) +
         "," + jstr("sha256", res.jsonSha256) + "}";
  man += "],";
  man += jstr("note", "数据文件哈希可用 sha256sum 独立复核；导出事件（EXPORT）已写入设备审计库，含本清单哈希");
  man += "}\n";
  if (!writeAll(res.manifestPath, man)) {
    res.error = "清单写入失败: " + res.manifestPath;
    return res;
  }
  res.manifestSha256 = sha256FileHex(res.manifestPath);

  // ---- EXPORT 事件入审计（D22：导出动作本身留痕，含逐文件哈希）----
  std::string payload = "{";
  payload += jstr("dest", opt.destDir) + ",";
  payload += jnum("since", opt.sinceWall) + ",";
  payload += jnum("until", opt.untilWall) + ",";
  payload += jnum("events", res.eventCount) + ",";
  payload += jnum("snapshots", res.snapshotCount) + ",";
  payload += jbool("chain_verified", res.chainVerified) + ",";
  payload += "\"files\":[";
  payload += "{" + jstr("name", csvName) + "," + jstr("sha256", res.csvSha256) + "},";
  payload += "{" + jstr("name", jsonName) + "," + jstr("sha256", res.jsonSha256) + "},";
  payload += "{" + jstr("name", manName) + "," + jstr("sha256", res.manifestSha256) + "}";
  payload += "]}";
  log.log(cat::Audit, ev::Export, {}, 0, 0, payload);

  res.ok = true;
  return res;
}

}  // namespace massage::audit
