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

⚠ **中文路径坑**：CMake 4.0.2 在中文路径下配置时崩溃（0xC0000409）。
解决：用 `subst` 映射 ASCII 盘符（每个新终端会话执行一次）：

```powershell
subst M: "E:\个人开发\台式按摩仪"
cmake -S M:\ -B M:\build -G "Visual Studio 17 2022" -A x64
cmake --build M:\build --config Debug
ctest --test-dir M:\build -C Debug --output-on-failure
```

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
- [ ] **M3 应用与界面**：ISerialPort Qt 适配器（QSerialPort）、应用装配层、QML 主界面 1:1 还原原型（量程修正 10–50Hz）、设置面板 + PIN、绑定/维护模式
- [ ] **M4 合规与审计**：SQLite 审计追踪、IEC 62304 文档链、WSL Qt5.12 终验

## 关键约束（详见设计文档）

- 频率 10–50Hz（RPM=Hz×60，协议闭环上限 3001RPM）；原型的 100Hz 是错误，已修正
- `0xAA` 地址烧录**严禁上总线广播**，仅维护模式单头操作
- 暂停/停止 = 滑行停止（速度0），恢复 = 缓启动 0x10 柔和爬升
- 故障 → 卡片红色故障态 + 手动复位（02/03）后才能重启
