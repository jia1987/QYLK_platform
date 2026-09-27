# MDS 软件架构设计（SAD）

| | |
|---|---|
| 文档编号 | MDS-SAD-003 |
| 版本 | 1.0 |
| 状态 | 发布 |
| 开发 | ＿＿＿＿＿＿ 日期＿＿＿＿＿＿ |
| 审核 | ＿＿＿＿＿＿ 日期＿＿＿＿＿＿ |
| 批准 | ＿＿＿＿＿＿ 日期＿＿＿＿＿＿ |

> 对应 IEC 62304 §5.3。本文件描述 **as-built** 架构（与代码一致）；决策依据见
> 设计方案 D1–D32，需求追溯见 MDS-TRM-006。

## 1. 架构总览

```
┌──────────────────────────── QML UI 层 ────────────────────────────┐
│ main.qml（布局/Toast/确认弹窗） HeadCard×6 CenterPanel             │
│ SettingsOverlay（PIN 门禁后：时间/医院/串口/绑定/操作员/审计/导出） │
│ Waveform StepButton                                               │
└──────────────┬────────────────────────────────────────────────────┘
               │ Q_PROPERTY / Q_INVOKABLE / signals（唯一 UI↔逻辑通道）
┌──────────────┴──────────── 应用装配层（Qt）───────────────────────┐
│ AppCore      6×HeadItem 编排、确认流、审计挂钩、防抖/快照定时器     │
│ AppConfig    JSON 持久化（D10）    MockBus/SerialTransport 传输实现 │
└──────┬──────────────────────────────┬─────────────────────────────┘
       │                              │
┌──────┴────── 协议核心层 ──────┐ ┌───┴──────── 审计引擎 ───────────┐
│ massage_core（纯 C++17 零 Qt）│ │ massage_audit（纯 C++17 零 Qt）  │
│ FrameCodec/CRC16 帧编解码     │ │ AuditLog 事件/会话/链/降级/查询  │
│ ProfileEngine 曲线            │ │ AuditExport CSV+JSON+清单        │
│ TreatmentHead 状态机          │ │ PinGuard / Sha256 / JsonMini     │
│ BusScheduler 半双工调度       │ │ sqlite3（amalgamation 静态嵌入） │
└──────┬────────────────────────┘ └──────────────────────────────────┘
       │ 抽象接口（依赖倒置，可测性核心手段）
       ├─ ITransport（send/回调）→ QSerialPort 实机 / MockBus / socat+模拟器
       └─ IClock（mono/wall）    → QElapsedTimer+系统钟 / ManualClock（测试注入）
```

## 2. 模块清单

| 模块 | 位置 | 职责 | 依赖 | SRS 段 |
|---|---|---|---|---|
| FrameCodec/CRC16 | `src/core/frame_codec.* crc16.*` | 8 字节帧编解码、CRC16/MODBUS lo-first、字节流同步器 | 无 | 010–013,021,022 |
| ProfileEngine | `src/core/profile.*` | 恒频/扫频/阶频曲线，250ms tick 目标 RPM | 无 | 035–039 |
| TreatmentHead | `src/core/head_state.*` | 单头状态机+倒计时+故障处理 | Profile/Frame | 030–034,040–044,050–053 |
| BusScheduler | `src/core/bus_scheduler.*` | 半双工事务队列、轮询预算、重试、离线判定 | ITransport/IClock | 013–018,042 |
| AuditLog | `src/audit/audit_log.*` | 审计事件存储、会话哨兵、链式哈希、降级兜底、查询 | sqlite3/IClock | 080–095,099 |
| AuditExport | `src/audit/audit_export.*` | 导出三件套+哈希清单 | AuditLog/Sha256 | 096–098 |
| PinGuard | `src/audit/pin_guard.h` | PIN 防暴破锁定状态机 | IClock | 072 |
| Sha256/JsonMini | `src/audit/sha256.* json_mini.*` | 哈希；JSON 转义/回放解析 | 无 | 089,095,096 |
| AppCore | `src/app/app_core.*` | 装配编排、QML 桥、审计挂钩、PIN/操作员/维护流程 | core+audit+Qt | 018,043,060–064,070–075,084–087,113–118 |
| AppConfig | `src/app/app_config.*` | JSON 配置持久化 | Qt | 100–102 |
| SerialTransport | `src/app/serial_transport.*` | QSerialPort 适配 ITransport + StreamFramer | Qt SerialPort | 004,020 |
| MockBus | `src/app/mock_bus.*` | 进程内 485 从机仿真（**验证工具，不随发布部署启用**） | Qt/core | — |
| QML UI | `qml/*.qml` | 界面呈现与触控交互 | AppCore 桥 | 110–118 |

## 3. 关键数据流

### 3.1 总线事务（半双工一问一答）
```
AppCore(250ms tick) → TreatmentHead.tick() 产帧 → BusScheduler.enqueueControl(优先)
BusScheduler(10ms tick) → ITransport.send → [实机 485 / Mock] → 应答帧
→ SerialTransport(StreamFramer 同步/CRC 校验) → AppCore.deliverReply
→ BusScheduler.onReply(严格地址+帧头匹配) → 事务回调 → TreatmentHead.onAck/onInfo/onSpeed
→ HeadEvent → AppCore.onHeadEvent → { Toast(QML) + AuditLog.log(审计) }
```
超时路径：2ms 重发 ≤3 次 → 事务失败 → 连续 5 轮询周期失败 → onPresenceChange(false) → Absent + HEAD_UNPLUGGED 审计（运行中则 HEAD_OFFLINE 告警）。

### 3.2 审计写入（D13 fail-operational）
```
log() ── DB 正常 ──→ checkClockJump → INSERT(WAL,FULL) → 链缓存/满千条 flush → 水位抽检
  │                        └ 跳变 → CLOCK_JUMP 事件
  └── DB 失效 ──→ degrade(AUDIT_DEGRADED→NDJSON, 回调→UI 红灯+Toast)
                   每次 log 检查恢复期(30s) → tryRecover → 回填 NDJSON → AUDIT_RECOVERED
```

### 3.3 治疗场次审计时序
```
头上线: HEAD_PLUGGED + 0x54 初始化(SRS-022) → 启动确认(弹窗含操作员) → THERAPY_START
运行中: 改参(2s 防抖)→PARAM_CHANGE · 30s 定时→SESSION_SNAPSHOT · 故障→FAULT_DETECTED
结束:   STOP(user)/COMPLETE(到时) · 复位→FAULT_RESET · 失联→HEAD_OFFLINE
进程:   SESSION_START/END · 未 close→下次启动 ABNORMAL_TERMINATION · 上电清理→STOP_ALL_ON_BOOT
```

## 4. 线程模型

**全部逻辑在主线程**（QTimer 驱动：总线 10ms / 逻辑 250ms / UI 1s / 快照 30s / 防抖 2s 单发）。
- 无锁设计：AuditLog 文档化为单线程使用（与 AppCore 模型一致），SQLite THREADSAFE=1 但单连接。
- 审计写入同步执行（WAL+FULL 单条 1–5ms，事件频率 ≤ 数条/秒，主线程无感）——
  换取「断电零丢失」（D18）与架构简单性；性能余量见 VR 记录。
- QML 渲染线程由 Qt Quick 管理，与逻辑无共享状态（只经 QObject 属性/信号）。

## 5. 部署视图

```
目标（RK3568 arm64, Ubuntu 20.04, Qt 5.12.8, Kiosk 触摸屏 1920×1080/1280×800）
  systemd 服务（自启+看门狗+崩溃重启）→ massage_app --serial /dev/massage485
  udev: USB-RS485 转换器固定别名（T7 芯片确认）
  数据: ~/.local/share/shudot/massage-controller/{config.json, audit.db(+wal/shm)}
  USB 导出: /media/<user>/<label>（QStorageInfo 枚举可写卷）
开发（Windows 11 MinGW/Qt 6.9.2 ↔ WSL2 Ubuntu 20.04/Qt 5.12.8）
  双平台 ctest 全量 + socat/com0com 虚拟串口 + rs485_sim.py 从机模拟器
```

## 6. 架构决策与理由（摘要，全文见设计方案）

| # | 决策 | 理由 |
|---|---|---|
| 1 | core/audit 纯 C++ 零 Qt | 脱 UI 单测、跨平台同待遇、规避 Qt 部署链风险（D11/D29） |
| 2 | ITransport/IClock 注入 | 总线与时序全离线可测（MockBus/ManualClock），无需硬件 |
| 3 | SQLite amalgamation 入仓 | 版本冻结可复现（SOUP）、不依赖 libqt5sql5-sqlite（D29） |
| 4 | 审计同步写主线程 | 断电零丢失优先于吞吐；事件频率低，实测无感（D18） |
| 5 | fail-operational 降级 | 临床可用性优先；降级可视+兜底+回填闭环（D13） |
| 6 | QML 仅经 AppCore 桥 | UI 无业务逻辑，逻辑 100% 可单测（M3 起执行） |
| 7 | 触发器+链哈希双层防篡改 | 触发器防直连删改；链哈希兜底「触发器被 DROP」攻击面（D21） |

## 7. 架构级风险与约束

- **单线程瓶颈**：若未来加入网络/数据库重负载，须重新评估（当前无此需求）。
- **MockBus 与真实板卡行为差异**：以 rs485_sim.py（协议同源）+ RK 实机终验（T3）收敛。
- **SOUP 边界**：Qt/SQLite/内核的已知异常评估见 MDS-SOUP-008；升级须过变更控制（MDS-VC-010）。
