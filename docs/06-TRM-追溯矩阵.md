# MDS 需求↔代码↔测试 追溯矩阵（TRM）

> **本文件由 `tools/trm/gen_trm.py` 自动生成 —— 请勿手改。**
> 生成时间：2026-09-26 22:27 · git 基线：b40c3f3 · 校验规则见脚本头注（DoD#4 零缺口）

| 文档编号 | MDS-TRM-006 |
|---|---|
| 版本 | 自动生成（随 git 基线） |

**汇总：SRS 共 84 条 · 完整覆盖 80 · 推迟项在案 4 · 缺口 0**

| SRS | 需求（截断） | 来源 | 验证 | 代码 | 测试(ctest) | 状态 |
|---|---|---|---|---|---|---|
| SRS-001 | 软件应构建运行于 Ubuntu 20.04 (arm64/x86_64) + Qt 5.12.8；UI 代码保持 Qt5/Qt6 双兼容（不使用 qt_ad… | URD-017, D11 | A（ | CMake | — | ✅ |
| SRS-002 | 协议核心层（帧编解码/曲线/状态机/总线调度）应为纯 C++17、零 Qt 依赖，可脱离 UI 单测 | D11 | A（ | CMake, core/CMake | — | ✅ |
| SRS-003 | 审计引擎应为纯 C++17、零 Qt 依赖，SQLite 以 amalgamation 静态嵌入（不依赖系统/Qt SQLite 驱动） | D29 | A（ | audit/audit_log.h, CMake, audit/CMake | — | ✅ |
| SRS-004 | 串口路径与波特率应可配置；Linux 下经 udev 固定别名（/dev/massage485），未配置时以进程内模拟总线运行 | URD-016, D8 | T | app/serial_transport.h, app/main.cpp, app/CMake | SRS-084_audit_flow | ✅ |
| SRS-005 | 应用应以 Kiosk 全屏运行、systemd 自启、崩溃自动重启（看门狗）；重启即触发上电全停清理 | URD-016, D6/D8 | I（ | app/main.cpp | — | ⏸ 推迟项在案 |
| SRS-006 | 软件版本应编译期注入（APP_VERSION），界面与审计会话记录含版本号 | URD-017, D12 | I | app/main.cpp, CMake, app/CMake | — | ✅ |
| SRS-010 | 帧格式：固定 8 字节 [帧头, 0x08, 地址, D3, D4, D5, CRC_lo, CRC_hi]；CRC 为 CRC16/MODBUS（多项式 … | URD-008, 协议§2 | T | core/crc16.h, core/frame.h, core/frame_codec.h | SRS-010_frame_codec | ✅ |
| SRS-011 | 帧编码应对越界输入防御性拒绝（地址/数据超 8 位等返回无效），不得产生畸形帧上总线 | D11 防御性设计 | T | core/frame_codec.h | SRS-010_frame_codec | ✅ |
| SRS-012 | 应答帧头映射：0x11=Ack、0x12=转速、0x13=位置、0x14=故障/温度/状态；应答地址须与请求地址严格匹配，不匹配丢弃 | 协议指令表, D5 | T | core/frame_codec.h | SRS-010_frame_codec | ✅ |
| SRS-013 | 字节流同步器应从任意噪声流中滑窗定位合法帧（帧头白名单+长度+CRC 校验），坏帧静默丢弃不中断后续同步 | 协议§2, D11 | T | core/bus_scheduler.h | SRS-013_bus_scheduler, SRS-010_frame_codec | ✅ |
| SRS-014 | 总线为半双工严格一问一答：任一时刻至多一个未完成事务；控制帧（0x55/0xAA/0x54）可插队优先于轮询 | D5 | T | core/bus_scheduler.h | SRS-013_bus_scheduler | ✅ |
| SRS-015 | 无应答重试：等待 ≥2ms 重发，最多 3 次；重试耗尽判事务失败 | 协议§2, D5 | T | core/bus_scheduler.h | SRS-013_bus_scheduler | ✅ |
| SRS-016 | 连续 5 个轮询周期无应答 → 该地址判离线（Absent）；恢复应答 → 重新在位 | D5, URD-008 | T | core/bus_scheduler.h | SRS-013_bus_scheduler | ✅ |
| SRS-017 | 轮询预算：在位/状态（0x58）每头每 500ms；运行中转速（0x56）每头每 1s；6 头全轮询周期 <100ms @57600bps | D5 | T | core/bus_scheduler.h | SRS-013_bus_scheduler | ✅ |
| SRS-018 | 上电/软件启动应对全部绑定地址逐个发滑行停止帧（0x55 速度0），防主机崩溃后电机残转；该动作写入审计（STOP_ALL_ON_BOOT） | URD-014, D5/D19 | T | app/app_core.h, app/main.cpp | SRS-084_audit_flow | ✅ |
| SRS-019 | 0x60 广播指令不得使用；0xAA 仅允许维护模式单头流程使用 | 协议警告, D2 | T | core/frame.h | SRS-061_maint_burn | ✅ |
| SRS-020 | 串行传输层应经 ITransport 抽象接入（QSerialPort 实现 / Mock 注入）；串口错误（拔线等）上报并写入审计（SERIAL_ERROR） | D11, D24★ | I（ | app/serial_transport.h, core/bus_scheduler.h | — | ✅ |
| SRS-021 | 频率→帧编码：RPM=Hz×60；闭环字节=0x1000\|RPM 高字节在前；50Hz→3000RPM≤协议上限 3001 | URD-002, D1 | T | core/frame.h, core/frame_codec.h | SRS-010_frame_codec | ✅ |
| SRS-022 | 缓启动参数应在初始化写入 0x54（限流 0x4D、缓启动 0x10、换向延时 0x00），保证暂停后柔和恢复 | D3, §2 冻结帧表 | T | app/app_core.h, core/frame_codec.h | SRS-084_audit_flow, SRS-010_frame_codec | ✅ |
| SRS-030 | 每治疗头独立状态机：Absent→Idle→Starting→Running→Paused→Stopping→Fault；状态迁移仅由合法事件驱动 | URD-001, D3 | T | core/head_state.h | SRS-030_head_state | ✅ |
| SRS-031 | 频率量程 10–50Hz、步进 5；越界钳位；时长 5–60min、步进 5 | URD-002/004, D1 | T | core/head_state.h | SRS-030_head_state | ✅ |
| SRS-032 | 启动：Idle+在位才允许；启动帧下发后进入 Starting，板载缓启动自然爬升；Ack 确认进入 Running | URD-001, D3 | T | core/head_state.h | SRS-030_head_state | ✅ |
| SRS-033 | 暂停：发 0x55 速度 0（<50 判滑行停止），倒计时冻结；继续：重发目标转速，缓启动恢复 | URD-006, D3 | T | core/head_state.h | SRS-030_head_state | ✅ |
| SRS-034 | 停止/倒计时归零：滑行停止语义，计时归零回 Idle；再次启动全程重新计时 | URD-004/006, D3 | T | core/head_state.h | SRS-030_head_state | ✅ |
| SRS-035 | 恒频模式：目标 RPM 恒定 | URD-003, D4 | T | core/profile.h | SRS-035_profile | ✅ |
| SRS-036 | 扫频模式：三角波 0→设定频率→0，周期 10s | URD-003, D4 | T | core/profile.h | SRS-035_profile | ✅ |
| SRS-037 | 阶频模式：梯形波周期 20s（爬升 4.8s→保持 5.2s→下降 4.8s→低保持 5.2s），低保持频率=max(10Hz, 设定×50%) | URD-003, D4 | T | core/profile.h | SRS-035_profile | ✅ |
| SRS-038 | 曲线以 250ms tick 计算瞬时目标；变化 ≥±30RPM 才入队下发（防总线拥塞）；曲线常量集中定义（临床终审 T5 只改数值不改架构） | D4, D5 | T | core/profile.h | SRS-035_profile | ✅ |
| SRS-039 | 闭环转速下限 100RPM：低于下限的曲线段映射为滑行（速度0），不得开环抖动 | D1/D4 | T | core/profile.h | SRS-035_profile | ✅ |
| SRS-040 | 运行中允许实时改频率/模式/时长；时长改动按差值调整剩余（不清零不重启）；方向仅 Idle/Absent/Fault 可改 | URD-006, D3 | T | app/app_core.h, core/head_state.h | SRS-030_head_state | ✅ |
| SRS-041 | 指令 Ack 被板载拒绝（0xEE/0xBB）时：不得回退 UI 状态（防「UI 已停、电机在转」），事件写入审计（ACK_REJECTED） | D3 安全语义, D24★ | T | core/head_state.h | SRS-030_head_state | ✅ |
| SRS-042 | 应答丢失（重试耗尽）不得改变头部状态机；状态回退只能由真实应答或超时策略驱动 | D3/D5 | T | core/bus_scheduler.h, core/head_state.h | SRS-013_bus_scheduler, SRS-030_head_state | ✅ |
| SRS-043 | 6 个预设位：单击应用；30s 内再次单击存为当前参数；默认预设表按 D10 修正表（量程内） | URD-005, D10 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-044 | 每头运行方向（A/B 转向）应逐头可配置并持久化；绑定/换头流程中经低速试运行确认 | D2/D10 | T（ | core/head_state.h | SRS-030_head_state | ⏸ 推迟项在案 |
| SRS-050 | 0x58 应答故障位（0x01 过压/0x02 欠压/0x04 过温/0x10 霍尔/0x20 堵转/0x40 短路）非零 → 该头进入 Fault 态，停… | URD-007, D3 | T | core/head_state.h | SRS-084_audit_flow, SRS-030_head_state | ✅ |
| SRS-051 | 故障仅可手动复位：发状态字 02/03 复位帧，Ack 成功回 Idle 后才允许重新启动；复位结果写入审计 | URD-007, D3/D9 | T | core/head_state.h | SRS-084_audit_flow, SRS-030_head_state | ✅ |
| SRS-052 | 运行中总线失联 → UI 醒目告警「电机可能仍在运转，必要时使用硬件急停」，事件写入审计（HEAD_OFFLINE） | URD-009, D5/D24 | T | app/app_core.h, core/head_state.h | SRS-030_head_state | ✅ |
| SRS-053 | 失联冻结告警：失联期间不得以缓存数据刷新该头状态显示 | D3 | T | core/head_state.h | SRS-030_head_state | ✅ |
| SRS-054 | 主机死机→电机失控路径由「硬件急停（T2）+ 看门狗重启 + 重启上电全停清理（SRS-018）」三层兜底；风险分析记录该路径（HAZ-01） | URD-014, D6 | A | app/main.cpp | — | ✅ |
| SRS-055 | 审计不可用不得阻止治疗（fail-operational）：降级期间治疗功能完整，UI 持续醒目告警 | URD-014, D13 | T | audit/audit_log.h, app/main.cpp | SRS-080_audit_log | ✅ |
| SRS-060 | 维护模式（PIN 保护）应提供：总线扫描在位头、槽位↔地址↔类型绑定的查看与修改 | URD-010, D2 | T | app/app_core.h, qml:SettingsOverlay.qml | SRS-061_maint_burn | ✅ |
| SRS-061 | 换头烧录向导：提示只留新头 → 探测地址 0 → 0xAA 烧录（新地址+闭环+堵转2s+极对数2）→ 绑定槽位类型 → 方向试运行确认 | URD-010, D2 | T（ | app/app_core.h | SRS-061_maint_burn | ⏸ 推迟项在案 |
| SRS-062 | **烧录安全联锁**：仅当「地址 0 新头在线」且「其余绑定头全部离线」才允许 0xAA；联锁拒绝路径写入审计（BURN_RESULT stage=inte… | URD-010, D2/D24 | T | app/app_core.h | SRS-061_maint_burn | ✅ |
| SRS-063 | 烧录全过程写入审计：BURN_STARTED（联锁状态+参数）、BURN_RESULT（阶段+结果）、PROBE_ZERO（探测结果） | D24, D2 | T | app/app_core.h | SRS-061_maint_burn | ✅ |
| SRS-064 | 绑定变更（类型/地址）写入审计（BINDING_CHANGED，旧→新）；地址变更重启生效并提示需设备侧同步烧录 | D9/D2 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-070 | PIN 门禁范围：系统设置、绑定/维护、预设修改、日志导出；主治疗界面不设限 | URD-011, D9 | T（ | app/app_core.h, qml:SettingsOverlay.qml | SRS-084_audit_flow | ⏸ 推迟项在案 |
| SRS-071 | PIN 4–6 位数字；加盐哈希存储（SHA-256+随机盐），不得存明文；首次启动强制设置 | URD-011, D9 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-072 | PIN 连续失败 5 次 → 锁定 5 分钟；锁定计时用单调钟（改系统时间无效）；锁定期间拒绝且不累计 | URD-011, D20 | T | app/app_core.h, audit/pin_guard.h | SRS-084_audit_flow, SRS-072_pin_guard | ✅ |
| SRS-073 | PIN 成功/失败（含来源）/锁定/修改/初设全部写入审计；修改与初设仅记哈希指纹前缀，不得记明文 | D20/D24 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-074 | 操作员名单（姓名/工号，无密码）由设置面板维护（PIN 后）；增/删/改写入审计（OPERATOR_*） | URD-012, D14 | T | audit/audit_log.h, qml:SettingsOverlay.qml | SRS-084_audit_flow, SRS-080_audit_log | ✅ |
| SRS-075 | 当前操作员应记住（持久化）；启动确认弹窗显示归属人；每条治疗审计携带 operator_id；未指定不阻止启动（记 0） | D14 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-080 | 审计存储为 SQLite（WAL 模式，synchronous=FULL）：每条事件即时落盘，断电丢失窗口=0 | URD-014, D18 | T | audit/audit_log.h | SRS-083_audit_crash, SRS-080_audit_log | ✅ |
| SRS-081 | 每条事件含三列时间戳：wall_utc(epoch ms) + mono_ms(会话内单调) + boot_seq；跨改钟/重启可重建真实时序 | URD-015, D17 | T | audit/audit_clock.h, audit/audit_log.h | SRS-080_audit_log | ✅ |
| SRS-082 | 事件表覆盖冻结事件表（设计方案 §7.4）全部类型：会话/治疗/故障/在位/访问/配置/时钟/审计自身 8 组 | URD-012, D24 | T | audit/audit_event.h | SRS-084_audit_flow, SRS-080_audit_log | ✅ |
| SRS-083 | 会话哨兵：启动写 SESSION_START；正常退出写 SESSION_END；再启动检测到未关闭会话 → 标记 abnormal 并补记 ABNORMA… | URD-014, D19 | T | audit/audit_log.h | SRS-083_audit_crash, SRS-080_audit_log | ✅ |
| SRS-084 | 治疗事件：START（参数快照+操作员）/PARAM_CHANGE（运行中改参 2s 防抖后生效值）/PAUSE/RESUME/STOP（原因）/COMPL… | URD-012, D15 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-085 | 治疗会话期间每 30s 写快照行（独立表）：6 头×(状态/目标RPM/实际RPM/温度)，治疗过程可重建；曲线逐帧下发不入审计 | D15 | T | app/app_core.h | SRS-084_audit_flow, SRS-080_audit_log | ✅ |
| SRS-086 | 在位变化（含 idle 插拔）写入审计（HEAD_PLUGGED/UNPLUGGED，槽位+地址） | D24 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-087 | 设置变更写入审计（SETTING_CHANGED，key+旧→新）：医院/科室、串口、预设 | D9 | T | app/app_core.h | SRS-084_audit_flow | ✅ |
| SRS-088 | 防篡改触发器：events/snapshots/chain_hashes 表 BEFORE DELETE/UPDATE → RAISE(ABORT)；ses… | URD-012, D21 | T | audit/audit_log.h | SRS-080_audit_log | ✅ |
| SRS-089 | 链式哈希：每 N 条（默认 1000）写一行 SHA256(prev_hash + 段内规范化行串)；提供 verifyChain 校验接口定位首个被篡改事件 | D21 | T | audit/audit_log.h, audit/sha256.h | SRS-083_audit_crash, SRS-080_audit_log, SRS-089_sha256 | ✅ |
| SRS-090 | 链校验应能发现「触发器被 DROP 后篡改历史行」与「链行缺失/不连续」两类攻击 | D21 | T | audit/audit_log.h | SRS-080_audit_log | ✅ |
| SRS-091 | DB 写失败 → 自动降级：追加 NDJSON 兜底文件（原始三列时间戳保留），记 AUDIT_DEGRADED；DB 恢复后自动回填并记 AUDIT_RE… | URD-014, D13 | T | audit/audit_log.h | SRS-080_audit_log | ✅ |
| SRS-092 | DB 打开失败（损坏/路径不可写）不得阻止应用启动；全降级模式下事件仍进兜底文件；兜底也不可用时计数丢失并持续告警（auditOk=false） | D13 | T | audit/audit_log.h, app/main.cpp | SRS-080_audit_log | ✅ |
| SRS-093 | 墙钟异常：启动时墙钟早于 2020-01-01 → SUSPECT_TIME；运行中墙钟-单调锚点偏差超阈（5s）→ CLOCK_JUMP（10s 节流+重… | URD-015, D17 | T | app/app_core.h, audit/audit_clock.h, audit/audit_log.h | SRS-080_audit_log | ✅ |
| SRS-094 | 水位监控：DB 体积（含 WAL/SHM）超配额 80% → DB_WATERMARK（每次打开至多一次）；任何情况下不得自动删除审计记录 | URD-012, D16 | T | audit/audit_log.h | SRS-080_audit_log | ✅ |
| SRS-095 | NDJSON 兜底行的 payload 应逐字节保真往返（中文/引号/反斜杠/嵌套 JSON/控制字符）；损坏行跳过计数不崩溃 | D13 | T | audit/audit_log.h, audit/json_mini.h | SRS-080_audit_log | ✅ |
| SRS-096 | 导出（PIN 保护）：CSV（UTF-8 BOM+RFC4180 转义）+ JSON（meta/events/snapshots）+ 清单（逐文件 SHA2… | URD-013, D22 | T | audit/audit_export.h, audit/sha256.h | SRS-096_audit_export, SRS-084_audit_flow, SRS-089_sha256 | ✅ |
| SRS-097 | 导出支持全量或按时间范围（近30/90天/自定义起点）；导出动作写入审计（EXPORT：目标/范围/条数/逐文件哈希） | URD-013, D22 | T | app/app_core.h, audit/audit_export.h, qml:SettingsOverlay.qml | SRS-096_audit_export, SRS-084_audit_flow | ✅ |
| SRS-098 | 导出目标枚举可写挂载卷（U盘）；目标不存在/不可写时返回明确错误且不产生半成品文件集 | D22 | T | audit/audit_export.h | SRS-096_audit_export | ✅ |
| SRS-099 | 应用内查询：按类别过滤 + 分页（最新在前）+ 总数统计，供设置面板审计页展示 | URD-013, D23 | T | app/app_core.h, audit/audit_log.h, qml:SettingsOverlay.qml | SRS-084_audit_flow, SRS-080_audit_log | ✅ |
| SRS-100 | 配置 JSON 持久化（AppConfigLocation）：串口、地址↔槽位↔类型映射、每头方向、预设、医院/科室、PIN 盐/哈希、审计配额、当前操作员 | D10/D14 | T | app/app_config.h | SRS-084_audit_flow | ✅ |
| SRS-101 | 配置缺失/损坏时使用安全默认值创建（默认槽位 L1–R3↔地址1–6，预设按 D10 修正表） | D10 | A（ | app/app_config.h | — | ✅ |
| SRS-102 | 审计 DB 路径与配额可配置；默认 AppDataLocation/audit.db、配额 1024MB | D16 | T | app/app_config.h, audit/audit_log.h | SRS-080_audit_log | ✅ |
| SRS-103 | 系统时间设置：Linux 经 timedatectl（polkit）；失败提示授权错误；成功后审计锚点重定（SRS-093） | URD-015, D8 | I（ | app/app_core.h, qml:SettingsOverlay.qml | — | ✅ |
| SRS-110 | 主界面 1:1 还原原型暗色主题：左右侧 6 卡片、中央参数区、底部操作栏、Modal、Toast；基准 1920×1080，自适应 1280×800 | URD-001, D7 | I | qml:CenterPanel.qml, qml:main.qml | — | ✅ |
| SRS-111 | 卡片应显示：槽位/类型、状态着色（待机/运行/暂停/故障红）、选中箭头、波形示意、进度条、剩余时间、启停按钮组 | URD-001/007, D7 | I | qml:HeadCard.qml, qml:main.qml | — | ✅ |
| SRS-112 | 触控目标 ≥44px；▲▼ 长按连发（450ms 后 110ms 重复） | D7 | I | qml:HeadCard.qml, qml:StepButton.qml | — | ✅ |
| SRS-113 | 启动/停止/全停需确认弹窗（危险操作红色标识），弹窗显示参数摘要与操作员 | D7/D14 | T | app/app_core.h, qml:main.qml | SRS-084_audit_flow | ✅ |
| SRS-114 | 设置面板（PIN 后）分区：时间/医院科室/串口/绑定维护/修改PIN/操作员/审计记录/审计导出 | URD-010..013 | I | qml:SettingsOverlay.qml | — | ✅ |
| SRS-115 | 审计页应显示审计健康状态灯（正常/降级红色告警）；降级/恢复时全局 toast 醒目提示 | D13/D23 | T | app/app_core.h, qml:SettingsOverlay.qml | SRS-084_audit_flow | ✅ |
| SRS-116 | 波形图为静态示意（按模式绘制，非实时曲线） | D7 | I | qml:CenterPanel.qml, qml:main.qml, qml:Waveform.qml | — | ✅ |
| SRS-117 | 频率控件量程 10–50Hz（原型 100Hz 错误已修正），预设文案同步 | URD-002, D1 | T | app/app_core.h, qml:CenterPanel.qml, qml:main.qml | SRS-030_head_state | ✅ |
| SRS-118 | QML 与 C++ 经 Q_PROPERTY/Q_INVOKABLE 桥接；头列表以 QML 可绑定对象暴露 | D7/D11 | A | app/app_core.h, qml:main.qml | — | ✅ |

## 推迟项说明（豁免明细，均挂待办/问题编号）

- **SRS-005**：T3：Kiosk/systemd 部署工件随 RK 板冻结产出（上电清理部分已由 SRS-018 验证）
- **SRS-044**：T11/ISS-012：方向持久化 + 绑定流程低速试运行 UI 待补（方向字段与 Idle 限定已实现）
- **SRS-061**：T11/ISS-012：烧录向导末步「方向试运行确认」随 SRS-044 补齐
- **SRS-070**：ISS-013：预设修改的 PIN 门禁与 D9 冲突，待用户决策（设置/绑定/导出已门禁）
