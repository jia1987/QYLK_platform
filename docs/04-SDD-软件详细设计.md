# MDS 软件详细设计（SDD）

| | |
|---|---|
| 文档编号 | MDS-SDD-004 |
| 版本 | 1.0 |
| 状态 | 发布 |
| 开发 | ＿＿＿＿＿＿ 日期＿＿＿＿＿＿ |
| 审核 | ＿＿＿＿＿＿ 日期＿＿＿＿＿＿ |
| 批准 | ＿＿＿＿＿＿ 日期＿＿＿＿＿＿ |

> 对应 IEC 62304 §5.4/§5.5。粒度按决策 D26「B 级结构、详设高一档」：组件接口 + 关键算法
> （伪码/公式/状态表）。若 T6 判 C 级，按本文档结构向下补单元级细化与覆盖率分析。

## 1. FrameCodec / CRC16（SRS-010…013, 021, 022）

- 帧布局：`[0]帧头 [1]=0x08 [2]地址 [3]D3 [4]D4 [5]D5 [6]CRC_lo [7]CRC_hi`。
- CRC16/MODBUS：初值 0xFFFF，逐位右移，多项式 0xA001（反射）；例帧实算验证（协议 PDF p2）。
- `buildFrame/encodeXxx` 全为 `noexcept` 纯函数；输入越界（如 RPM>3001）返回 `std::nullopt`
  ——**防御性拒绝**，调用方不得上总线（SRS-011）。
- `StreamFramer::feed(bytes)`：滑窗扫描，帧头 ∈ 应答白名单 {0x11,0x12,0x13,0x14} 且 [1]==0x08
  且 CRC 通过 → 弹出一帧；否则窗口前移；缓冲上限截断防内存膨胀（SRS-013）。
- 频率编码：`hzToRpm(f)=f*60`；闭环值 `0x1000|rpm`，D3=高字节、D4=低字节（SRS-021）。
- 冻结默认参数（`defaults::` 命名空间）：极对数 2、堵转 0x14、限流 0x4D、缓启动 0x10、
  换向延时 0x00、闭环 true —— 与设计方案 §2 冻结帧表逐字节一致（SRS-022）。

## 2. ProfileEngine 曲线（SRS-035…039）

250ms tick 输入 `elapsedS`，输出瞬时目标 RPM：
```
恒频:  target = hzToRpm(f)
扫频:  三角波，周期 P=10s：phase = elapsed mod P
       phase < P/2: frac = phase/(P/2)         # 0→1 爬升
       否则:        frac = 2 - phase/(P/2)*... # 1→0 回落（对称）
       target = frac * hzToRpm(f)
阶频:  梯形波，周期 P=20s：爬升 4.8s → 保持 5.2s → 下降 4.8s → 低保持 5.2s
       低保持频率 lowHz = max(10Hz, f×50%)
低速段映射: target < 100RPM(闭环下限) → 0（滑行），不开环抖动（SRS-039）
下发过滤: |target − lastSent| ≥ 30RPM 才产帧（SRS-038，防总线拥塞）
```
曲线常量集中于 `profile.h` 常量表——临床数值终审（T5）只改数值不改结构。

## 3. TreatmentHead 状态机（SRS-030…034, 040–044, 050–053）

### 3.1 状态迁移表（核心）

| 当前态 | 事件/条件 | 次态 | 动作 |
|---|---|---|---|
| Absent | onPresence(true) | Idle | — |
| 任意运行态 | onPresence(false) | Absent | 运行中 → fire(WentOfflineDuringRun)；冻结显示（SRS-053） |
| Idle | start() | Starting | 计时器装载 total=remain=timeMin×60；发运行帧（或等 tick） |
| Starting | onAck(Ok) | Running | fire(Started) |
| Starting/Running | pause() | Paused | 发 0x55 速度0（滑行）；倒计时冻结；fire(Paused) |
| Paused | resume() | Running/Starting | 重发目标转速（板载缓启动爬升）；fire(Resumed) |
| Starting/Running/Paused | stop() | Stopping | 计时归零；发速度0；fire(StopRequested) |
| Stopping | 转速确认归零/超时 | Idle | finishStop()；到时路径 fire(Completed) |
| Running/Starting | 倒计时归零 | Stopping | fire(TimerExpired)+滑行停止（自动完成路径） |
| 任意在位态 | onInfo(fault≠0) | Fault | fire(FaultEntered)；停发运行指令 |
| Fault | resetFault()→onAck(Ok) | Idle | fire(FaultCleared)；复位帧状态字 02/03（含方向） |
| 任意 | onAck(0xEE/0xBB) | 不变 | fire(AckRejected)；**绝不回退 UI 状态**（SRS-041） |

### 3.2 关键语义
- **应答丢失不回退**：重试耗尽的事务不投递任何状态变化；头部状态只由真实应答或超时策略驱动（SRS-042）。
- **运行中改参**：`applyConfig` 时长差值语义（remain += ΔT，钳位 [1,total]）；方向仅 Idle/Absent/Fault 可改（SRS-040/044）。
- **暂停=滑行**：速度值 <50 → 板载 0 速惯性滑行，无电气制动（D3；机械刹车 0x04 为预留能力）。

## 4. BusScheduler（SRS-013…018, 042）

```
队列: deque<Transaction{frame, prio, cb, tries, deadline}>；控制帧 prio=1 插队
tick(now):  无未完成事务 → 取队首 → send → 记 deadline=now+超时窗
onReply(f): 严格匹配 {地址==请求地址 && 帧头∈该指令的合法应答集} → cb(ok,payload) → 清事务
            不匹配 → 丢弃（乱序/串扰防御）
超时: now>deadline → tries<3 且距上次发送≥2ms → 重发；否则 cb(false) → 该地址失败计数++
轮询预算: 0x58 每头 500ms；0x56 仅运行态每头 1s（setSpeedPollEnabled）
离线判定: 连续 5 个轮询周期失败 → onPresenceChange(addr,false)；成功应答即恢复（SRS-016）
时钟注入: IClock（测试用 ManualClock 全离线驱动时序）
```

## 5. AuditLog（SRS-080…095, 099）

### 5.1 Schema v1（六表 + 七触发器）
```
meta(k,v)                          —— schema_version/device_id/app_version/quota_mb
sessions(boot_seq PK, start_wall, start_mono, end_wall, end_mono, end_reason, app_version)
events(id AUTOINC, wall_utc, mono_ms, boot_seq, session_id, category, type,
       slot, addr, operator_id, payload)      + 索引 wall/session/category
snapshots(id, session_id, wall_utc, mono_ms, seq, data)
chain_hashes(seq PK, first_event_id, last_event_id, prev_hash, hash, wall_utc)
operators(id, name, code, active, created_wall)     —— 软删（active=0），变更入审计
触发器: events/snapshots/chain_hashes 禁 DELETE+UPDATE；sessions 禁 DELETE（RAISE ABORT）
```

### 5.2 写入路径（伪码）
```
log(cat,type,…):
  if degraded: 到期(30s)?→tryRecover()→成功则重入; 否则 appendNdjson; return 0
  checkClockJump()                       # |wall−(anchorWall+Δmono)|>5s → CLOCK_JUMP(10s 节流)+重锚定
  id = INSERT(events)                    # WAL + synchronous=FULL：返回即已落盘（D18）
  失败 → degrade(errmsg)+appendNdjson; return 0
  chainBuf += canonical(id,…); 每满 N(1000) 条 → flushChain
  每 64 条插入 → dbSize>80%配额 且未告警过 → DB_WATERMARK
canonical(id,wall,mono,boot,sess,cat,type,slot,addr,op,payload)
  = 各列十进制/原文以 '|' 连接（NULL→空串）；hash = SHA256(prevHex + 段内全部 canonical 串接)
```

### 5.3 会话哨兵（D19）
```
open():  SELECT 未关闭会话(end_reason IS NULL) → 存在则 UPDATE end_reason='abnormal'
         并在新会话下补记 ABNORMAL_TERMINATION{old_boot}
         boot_seq = max+1；墙钟<2020 → SUSPECT_TIME；遗留 NDJSON → 回放+AUDIT_RECOVERED
close(): SESSION_END + UPDATE end_reason='normal'      # 析构≠close：崩溃语义留给哨兵
```

### 5.4 降级/恢复（D13）
```
degrade: 关 DB → 开 NDJSON 追加流（每行 flush）→ AUDIT_DEGRADED 行 → 回调 UI 红灯
NDJSON 行: {"wall":…,"mono":…,"boot":…,"sess":…,"cat":…,"type":…,"slot":…,"addr":…,"op":…,"payload":{…}}
           快照行含 "snap":1；payload 原样内嵌（回放零解析损失）
tryRecover: openDb(含 quick_check) → rebuildChainState → 逐行回放（原时间戳保留，
           json_mini 解析失败行计 skipped）→ 兜底文件改名 .replayed-<ts> 留档 → AUDIT_RECOVERED
双失效（DB+兜底都写不了）: unavailable=true，lostEvents 计数，UI 必须持续告警（SRS-092）
```

### 5.5 链校验（D21/D22 共用）
```
verifyChain(): 按 seq 遍历 chain_hashes；段连续性(first==prevLast+1) ∧ prev_hash 链接
              ∧ SHA256(prevHex+SELECT 段内行 canonical) == 存储 hash；任一失败 → 返回首个坏段起点 id
攻击面覆盖: 直连删改(触发器拦) / DROP TRIGGER 后改行(链拦) / 删链行(连续性拦) / 整库旧备份替换(链头+会话哨兵+导出清单交叉拦)
```

## 6. AuditExport（SRS-096…098）

```
exportAudit(log, opt):
  chainVerified = log.verifyChain()                    # 导出时点自检，结果写入清单
  events = queryEvents(range); snaps = querySnapshotsRange(range)
  CSV:  BOM + 表头 + RFC4180 行（含 , " \n \r 的字段双引号包裹、内引号翻倍）
        wall_utc 双列：ISO8601(UTC,纯算术 civil_from_days) + epoch ms
  JSON: {export_meta{device,version,generated,range,counts,chain_verified}, events[…payload 合法 JSON 原样内嵌…], snapshots[…]}
  落盘 → sha256FileHex 回读两文件 → manifest.json{files:[{name,bytes,sha256}],…} → 回读 manifest 哈希
  log(EXPORT, {dest,range,counts,三文件 sha256,chain_verified})   # 导出留痕（D22）
失败语义: 目标不存在/不可写 → error 返回，不产生半成品（先数据文件后清单，清单缺失即导出无效）
```

## 7. PinGuard（SRS-072）
状态：`fails`（计数）、`lockUntilMono`（-1=未锁）。失败累计 5 → `lockUntilMono=mono+300000`、计数清零；
`locked()` 惰性判定（到期即解锁，无定时器）；成功清零；锁定期间尝试不累计。单调钟计时 → 改系统时间无效。

## 8. AppCore 装配（SRS-018,043,060…064,070…075,084…087,113,115,117,118）

- 定时器组：总线 10ms / 逻辑 250ms / UI 1s / 快照 30s / 预设窗 30s 单发 / 改参防抖 2s 单发。
- **改参防抖（D15）**：运行态改参 → 记窗口起点参数并(重)启 2s 单发；到期时头仍在运行态且
  净变化非零 → PARAM_CHANGE{before,after}；新 START 或停止丢弃窗口。
- **上线初始化（SRS-022/ISS-011）**：onPresenceChange(online) → 插队下发 0x54（限流/缓启动/换向延时
  冻结默认值）→ 再记 HEAD_PLUGGED。板载带记忆，重复写幂等；烧录后的新头自动完成初始化。
- PIN：verifyPin 前置 PinGuard 锁定检查（锁定期拒绝且不累计，PIN_FAIL 记 locked 标记）；
  第 5 败 → PIN_LOCKED + toast + pinLockChanged。哈希 = SHA256(盐+PIN)，审计只记哈希前 8 位指纹。
- 维护/烧录：联锁判定同步执行（其余绑定头任一在线 → 拒绝 + BURN_RESULT{stage:interlock} 审计）；
  探测地址 0 → BURN_STARTED{interlock:passed,参数} → 0xAA → BURN_RESULT{stage:burn,ok}。
- 确认流：requestStart/Stop/StopAll → confirmRequest(info{icon,title,msg(含操作员行),okText,danger})
  → QML 弹窗 → confirmResponse(accepted) → 执行/丢弃（回调单次持有）。

## 9. 关键数据结构

| 结构 | 定义处 | 要点 |
|---|---|---|
| `Frame` | core/frame.h | `std::array<uint8_t,8>` 值语义；`std::optional<Frame>` 表达编码拒绝 |
| `HeadConfig` | core/head_state.h | freqHz[10,50] / mode{Constant,Sweep,Step} / timeMin[5,60] / dir{DirA,DirB} |
| `AuditConfig` | audit/audit_log.h | dbPath/fallbackPath/deviceId/appVersion/quotaMb=1024/chainInterval=1000/jumpThreshold=5s/recoveryRetry=30s |
| `EventRow/SnapshotRow/OperatorRow/EventQuery` | audit/audit_event.h | 查询投影；EventQuery{category,since,until,limit=200,offset,desc} |
| `ExportOptions/ExportResult` | audit/audit_export.h | 范围+目标 → 三件套路径+三哈希+计数+chainVerified |
