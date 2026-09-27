// @srs SRS-010 SRS-011 SRS-012 SRS-021 SRS-022
#pragma once
// 帧编解码：纯函数 + 字节流同步器。零 Qt 依赖，全部逻辑可单测。
// 编码策略（医疗器械防御性设计）：参数越界一律返回 nullopt，绝不静默钳位/回绕。
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "core/frame.h"

namespace massage::core {

// 组装完整 8 字节帧（自动追加 CRC，低字节在前）。
Frame buildFrame(std::uint8_t header, std::uint8_t len, std::uint8_t b2,
                 std::uint8_t b3, std::uint8_t b4, std::uint8_t b5) noexcept;

// ---- 主机指令编码 ----

// 0xAA 出厂初始化：地址 + 开/闭环 + 堵转时间 + 极对数。
// ⚠ 只允许对单独连接的一个头发送（总线上多设备会全部被改写地址）。
Frame encodeConfigAddress(std::uint8_t addr, bool closedLoop,
                          std::uint8_t stallTime, std::uint8_t polePairs) noexcept;

// 0x54 参数配置：限流 + 缓启动 + 换向延时。
Frame encodeConfigParams(std::uint8_t addr, std::uint8_t currentLimit,
                         std::uint8_t softStart, std::uint8_t dirDelay) noexcept;

// 0x5D 霍尔矫正。
Frame encodeHallCorrect(std::uint8_t addr) noexcept;

// 0x55 电机控制。speedValue：闭环=RPM(100..3001)，开环=占空比(50..3001)；
// closedLoop=true 时自动置 0x1000 位。越界返回 nullopt。
std::optional<Frame> encodeMotorCtrl(std::uint8_t addr, std::uint16_t speedValue,
                                     bool closedLoop, MotorStatus status) noexcept;

// 0x55 按频率运行（闭环，RPM = Hz×60）。Hz 越界 [10,50] 返回 nullopt。
std::optional<Frame> encodeRunHz(std::uint8_t addr, int freqHz,
                                 MotorStatus status) noexcept;

// 0x55 滑行停止：速度 0（<50 即 0 速，电机惯性停止）。决策 D3：暂停/停止统一语义。
Frame encodeCoastStop(std::uint8_t addr, MotorStatus dir) noexcept;

// 0x55 故障复位：状态字 02（复位+A 向）/ 03（复位+B 向）。
Frame encodeFaultReset(std::uint8_t addr, bool dirB) noexcept;

// 0x55 刹车：状态字 04（预留能力，当前 UI 不使用）。
Frame encodeBrake(std::uint8_t addr) noexcept;

// 0x56/0x57/0x58 读取指令（payload 固定 33 33 33）。readCmd 非读取指令返回 nullopt。
std::optional<Frame> encodeRead(Cmd readCmd, std::uint8_t addr) noexcept;

// ---- 从机应答解码 ----

struct AckReply {
  std::uint8_t addr = 0;
  AckFlag flag = AckFlag::Fail;
  std::uint8_t hallState = 0;  // 仅 0x5D 应答有效（第 5 字节）
  std::uint8_t dirHint = 0;    // 仅 0x5D 应答有效（第 6 字节）
};
struct SpeedReply {
  std::uint8_t addr = 0;
  std::uint32_t rpm = 0;  // 24 位
};
struct PositionReply {
  std::uint8_t addr = 0;
  std::int32_t position = 0;  // 24 位有符号（文档 p12 例程换算）
};
struct InfoReply {
  std::uint8_t addr = 0;
  std::uint8_t faultBits = 0;  // FaultBit 位掩码
  std::uint8_t tempC = 0;      // 控制器温度（十进制 ℃）
  bool motorRunning = false;   // 第 6 字节：0 停止 / 1 运行
};

using ReplyPayload =
    std::variant<AckReply, SpeedReply, PositionReply, InfoReply>;

// 校验：应答头码合法 + 长度字节 == 8 + CRC 正确。
bool validateReplyFrame(const Frame& f) noexcept;

// 解析应答帧。失败（校验不过/头码未知）返回 false。
bool parseReply(const Frame& f, Reply& outHeader, ReplyPayload& outPayload) noexcept;

// ---- 字节流同步器 ----
// 串口字节任意分段到达、可能夹杂噪声/回显：feed() 灌入原始字节，
// next() 弹出下一个通过校验的完整帧。自动跳过错位垃圾并计数（droppedBytes）。
class StreamFramer {
 public:
  void feed(const std::uint8_t* data, std::size_t len);
  bool next(Frame& out);
  std::size_t droppedBytes() const noexcept { return dropped_; }

 private:
  std::vector<std::uint8_t> buf_;
  std::size_t dropped_ = 0;
};

}  // namespace massage::core
