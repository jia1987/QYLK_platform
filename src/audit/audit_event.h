// @srs SRS-082
#pragma once
// 审计事件类型常量 —— 与设计方案 §7.4 冻结事件表一一对应（决策 D24）。
// 增删事件类型 = 修改冻结表 = 需要变更记录（D27/D28）。
#include <cstdint>
#include <string>
#include <vector>

namespace massage::audit {

namespace cat {
inline constexpr const char* Session = "session";
inline constexpr const char* Therapy = "therapy";
inline constexpr const char* Fault = "fault";
inline constexpr const char* Presence = "presence";
inline constexpr const char* Access = "access";
inline constexpr const char* Config = "config";
inline constexpr const char* Clock = "clock";
inline constexpr const char* Audit = "audit";
}  // namespace cat

namespace ev {
// 会话（D19）
inline constexpr const char* SessionStart = "SESSION_START";
inline constexpr const char* SessionEnd = "SESSION_END";
inline constexpr const char* AbnormalTermination = "ABNORMAL_TERMINATION";
inline constexpr const char* StopAllOnBoot = "STOP_ALL_ON_BOOT";
// 治疗（D14/D15）
inline constexpr const char* TherapyStart = "THERAPY_START";
inline constexpr const char* ParamChange = "PARAM_CHANGE";
inline constexpr const char* Pause = "PAUSE";
inline constexpr const char* Resume = "RESUME";
inline constexpr const char* Stop = "STOP";
inline constexpr const char* Complete = "COMPLETE";
// 故障
inline constexpr const char* FaultDetected = "FAULT_DETECTED";
inline constexpr const char* FaultReset = "FAULT_RESET";
inline constexpr const char* HeadOffline = "HEAD_OFFLINE";
inline constexpr const char* AckRejected = "ACK_REJECTED";      // 板载拒绝指令（安全相关，补充进冻结表）
inline constexpr const char* SerialError = "SERIAL_ERROR";      // 串口级错误：拔线/设备消失（补充进冻结表）
// 在位（D24：含 idle 插拔）
inline constexpr const char* HeadPlugged = "HEAD_PLUGGED";
inline constexpr const char* HeadUnplugged = "HEAD_UNPLUGGED";
// 访问（D14/D20）
inline constexpr const char* PinSuccess = "PIN_SUCCESS";
inline constexpr const char* PinFail = "PIN_FAIL";
inline constexpr const char* PinLocked = "PIN_LOCKED";
inline constexpr const char* PinChanged = "PIN_CHANGED";
inline constexpr const char* PinInitialSet = "PIN_INITIAL_SET";
inline constexpr const char* OperatorAdded = "OPERATOR_ADDED";
inline constexpr const char* OperatorRemoved = "OPERATOR_REMOVED";
inline constexpr const char* OperatorRenamed = "OPERATOR_RENAMED";
// 配置（D9/D2）
inline constexpr const char* SettingChanged = "SETTING_CHANGED";
inline constexpr const char* BindingChanged = "BINDING_CHANGED";
inline constexpr const char* BurnStarted = "BURN_STARTED";
inline constexpr const char* BurnResult = "BURN_RESULT";
inline constexpr const char* ProbeZero = "PROBE_ZERO";
// 时钟（D17）
inline constexpr const char* ClockChange = "CLOCK_CHANGE";
inline constexpr const char* ClockJump = "CLOCK_JUMP";
inline constexpr const char* SuspectTime = "SUSPECT_TIME";
// 审计自身（D13/D16/D22）
inline constexpr const char* AuditDegraded = "AUDIT_DEGRADED";
inline constexpr const char* AuditRecovered = "AUDIT_RECOVERED";
inline constexpr const char* Export = "EXPORT";
inline constexpr const char* DbWatermark = "DB_WATERMARK";
}  // namespace ev

// 查询结果行（events 表镜像）
struct EventRow {
  long long id = 0;
  long long wallUtc = 0;   // epoch ms（墙钟，可能被改钟污染 → 配 mono 使用）
  long long monoMs = 0;    // 会话内单调 ms（D17 三列之二）
  long long bootSeq = 0;   // D17 三列之三
  long long sessionId = 0;
  std::string category;
  std::string type;
  std::string slot;        // 空 = 非槽位事件
  int addr = 0;            // 0 = 无地址
  long long operatorId = 0;// 0 = 未指定
  std::string payload;     // JSON 文本，可为空
};

struct SnapshotRow {
  long long id = 0;
  long long sessionId = 0;
  long long wallUtc = 0;
  long long monoMs = 0;
  long long seq = 0;       // 会话内快照序号
  std::string data;        // JSON：6 头目标/实际 RPM、温度
};

struct OperatorRow {
  long long id = 0;
  std::string name;
  std::string code;
  bool active = true;
};

struct EventQuery {
  std::string category;        // 空 = 不过滤
  long long sinceWall = -1;    // -1 = 不限
  long long untilWall = -1;
  int limit = 200;             // 分页（D23）
  int offset = 0;
  bool desc = false;           // true = 最新在前（UI 用）
};

}  // namespace massage::audit
