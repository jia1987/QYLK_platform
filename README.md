# 台式按摩仪多设备控制软件

控制 6 个治疗头（L1–L3 / R1–R3）的三维螺旋振动治疗系统上位机。
RS485 总线 · Ubuntu 20.04 · RK3568 (arm64) · C++ / Qt QML。

**设计依据**：[`技术方案/软件设计方案-已锁定决策.md`](技术方案/软件设计方案-已锁定决策.md)（需求拷问五轮产出，含冻结帧表与全部决策记录）

## 目录结构

```
src/core/     协议核心层：纯 C++17，零 Qt 依赖（CRC、帧编解码、字节流同步器）
src/app/      Qt 应用层（Milestone 2：QML UI + QSerialPort + 状态机）
tests/        单元测试（冻结帧表 = 测试向量，注册验证记录的可执行载体）
tools/simulator/  RS485 从机模拟器（Python，无硬件闭环验证）
技术方案/      设计文档、界面原型
技术资料/      协议 PDF、厂商工具（大型厂商工具已被 .gitignore 排除）
```

## 构建与测试（Windows 开发机）

⚠ **中文路径坑**：CMake 4.x 在中文路径下配置时崩溃（0xC0000409）。
解决：用 `subst` 映射 ASCII 盘符（每个新终端会话执行一次）。

⚠ **工具链**：本机 Qt 6.9.2 为 **MinGW 版**（无 msvc 版），应用层必须用 MinGW+Ninja 构建；
核心/测试同时兼容 MSVC（纯 C++17）。两套构建目录互不干扰。

```powershell
subst M: "E:\个人开发\台式按摩仪"
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.9.2\mingw_64\bin;$env:PATH"

# 核心+测试+应用（MinGW，与目标板同为 GCC 系）
cmake -S M:\ -B M:\build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.9.2/mingw_64
cmake --build M:\build-mingw
ctest --test-dir M:\build-mingw --output-on-failure

# 运行（默认模拟总线；5 头在位、L2 演示未接入）
M:\build-mingw\src\app\massage_app.exe
```

真实总线（Qt SerialPort 模块已启用）：

```powershell
M:\build-mingw\src\app\massage_app.exe --serial COM6        # 或配置 config.json 的 serialPort
```

Qt SerialPort 模块安装备忘（MaintenanceTool 组件树可能不显示该模块，用官方归档直装）：

```powershell
pip install aqtinstall
python -m aqt install-qt windows desktop 6.9.2 win64_mingw --modules qtserialport --outputdir C:\Qt
# 装完必须删除构建目录的 CMakeCache.txt 再重新配置（find_package 结果有缓存）
```

Ubuntu（目标板/WSL）：`sudo apt install libqt5serialport5-dev`（Qt 5.12 自带枚举名 SerialError，
代码已用无参 lambda 规避 Qt5/Qt6 枚举改名差异）。

## 模拟器联调

1. 建虚拟串口对：Windows 装 [com0com](https://com0com.sourceforge.net/)（如 COM5↔COM6）；WSL/Linux 用 `socat -d -d pty,raw,echo=0 pty,raw,echo=0`
2. 启动模拟器（占一端）：`python tools/simulator/rs485_sim.py --port COM5 --verbose`
3. 主机软件连另一端（COM6）
4. 模拟器控制台可注入故障（`fault 1 stall`）、模拟插拔（`unplug 2`）

## 里程碑

- [x] **M1 协议核心层**：CRC16/MODBUS（例帧实算验证）、帧编解码（防御性越界拒绝）、字节流同步器、71 项单测全绿、从机模拟器
- [x] **M2 核心逻辑**（纯 C++17 零 Qt 依赖，260 项校验全绿）：
  - `profile.*` 曲线引擎：恒频/扫频(10s三角波)/阶频(20s梯形波)，D4 冻结常量，闭环下限→滑行段映射
  - `head_state.*` 治疗头状态机：Absent/Idle/Starting/Running/Paused/Stopping/Fault，滑行停止+缓启动恢复、倒计时冻结、故障手动复位、应答拒绝安全回退、失联冻结告警、运行中改参差值语义
  - `bus_scheduler.*` 总线调度：半双工一问一答、控制帧插队、超时重试×3、连续5周期失败判离线、应答严格匹配、时钟/传输注入式全离线单测
- [x] **M3 主界面**（Qt 6.9 MinGW 构建，模拟总线可交互）：
  - 应用装配层 AppCore：BusScheduler + 6×TreatmentHead 编排、QML 属性/命令/事件桥、确认弹窗与 toast 流
  - QML 主界面 1:1 还原原型：6 卡片（状态着色/选中箭头/波形/进度条/启停按钮组）、中央参数区（预设/模式/步进）、
    底部操作栏、确认弹窗、toast；量程已修正 10–50Hz
  - MockBus 进程内从机仿真（与 Python 模拟器同源行为）；SerialTransport（QSerialPort+StreamFramer）条件编译就绪
- [x] **M3.5 设置与运维**：
  - 设置面板 + PIN 门禁（SHA256+随机盐，首启强制设置；决策 D9）：系统时间（Linux timedatectl）/
    医院科室 / 串口配置 / 修改 PIN
  - 绑定/维护模式：槽位↔地址↔类型列表、换类型、**换头烧录向导**——安全联锁：
    仅当「地址0新头在线」且「其余绑定头全离线」才允许 0xAA（协议警告：AA 会被总线所有头接收）；
    MockBus 复现「谁收到谁改」真实语义，联锁有集成测试 `maint_burn` 双场景覆盖
  - `MASSAGE_MOCK_PRESENT=0` 环境变量可模拟「只接新头」的换头场景供演练
- [x] **M3.6 WSL 终验**（2026-09-26）：WSL2 Ubuntu 20.04.3 + **Qt 5.12.8（与目标板同版）** Release 构建零错误，
  ctest 5/5 全绿（含 `maint_burn` 烧录联锁集成测试）；socat 虚拟串口对 + `rs485_sim.py` ⇄ `massage_app --serial` 闭环联调：
  启动停止帧覆盖地址 1–6、CRC/应答帧全部正确，GUI 经 WSLg 正常存活。环境：发行版位于 `C:\WSL\Ubuntu-20.04`，
  源码副本 `~/massage`（自 `/mnt/e` 手动同步），apt 用清华源（focal 已 EOL，官方源失效）
- [ ] **M4 合规与审计**（grill-me 五轮已完成 2026-09-26：决策 **D13–D32** 锁定于设计文档 §7，含冻结事件表与新待办 T9/T10）：
  - [x] **M4a 审计引擎**（2026-09-26，Windows MinGW/Qt6.9 + WSL Qt5.12.8 双平台 ctest 10/10 全绿）：
    纯 C++ sqlite3 嵌入（零 Qt 依赖，SQLite 3.53.4 入仓 third_party/）、WAL+FULL 断电零丢失（子进程 _Exit 硬杀实测）、
    触发器+链式 SHA256 防篡改（DROP TRIGGER 后篡改仍被链校验抓获）、三列时间戳（wall+mono+boot_seq）、
    会话哨兵（socat 实总线 SIGTERM 后二次启动补记 ABNORMAL_TERMINATION 实测）、fail-operational+NDJSON 兜底回填、
    操作员名单、PIN 5 次锁 5 分钟、AppCore 全事件挂钩（治疗/故障/在位/配置/烧录/时钟/PIN）、30s 会话快照、改参 2s 防抖；
    集成测试 `audit_flow`：完整治疗场次（启动→改参→故障→复位→重启→停止→PIN 锁定→操作员）审计 DB 逐条对账
  - [x] **M4b 审计 UI + 导出**（2026-09-26，双平台 ctest 11/11 全绿 + Qt5.12 QML 运行时加载验证）：
    设置面板三新区块——操作员管理（增/删/选为当前，启动确认弹窗显示归属人）、
    审计记录页（类别筛选 chips + 分页列表 30/页 + 降级状态灯）、
    导出（QStorageInfo 枚举可写卷/自定义目录、全量/近30天/近90天、CSV+BOM/JSON/SHA256 清单三件套、
    导出时链校验结果写入清单、EXPORT 事件含逐文件哈希）；导出引擎 `audit_export.*` 纯 C++（RFC4180 转义、
    中文 payload torture 测试、独立重算哈希比对、范围过滤、目标不可写防御）
  - [ ] **M4c 文档链**：12 份中文 md（URD/SRS/SAD/SDD/RM/TRM/VR/SOUP/ISSUES/VC/PLAN/索引）+ SRS ID 嵌 ctest 测试名 + 矩阵脚本生成
  - ~~WSL Qt5.12 终验~~（已随 M3.6 完成）

## 关键约束（详见设计文档）

- 频率 10–50Hz（RPM=Hz×60，协议闭环上限 3001RPM）；原型的 100Hz 是错误，已修正
- `0xAA` 地址烧录**严禁上总线广播**，仅维护模式单头操作
- 暂停/停止 = 滑行停止（速度0），恢复 = 缓启动 0x10 柔和爬升
- 故障 → 卡片红色故障态 + 手动复位（02/03）后才能重启
