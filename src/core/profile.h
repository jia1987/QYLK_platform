// @srs SRS-035 SRS-036 SRS-037 SRS-038 SRS-039
#pragma once
// ProfileEngine —— 恒频/扫频/阶频曲线（决策 D4：参数冻结为常量，临床终审=T5）。
// 纯函数：给定模式+设定频率+已运行秒数 → 瞬时目标。无状态、无时钟，直接单测。
#include <cstdint>

#include "core/frame.h"

namespace massage::core {

enum class Mode : std::uint8_t {
  Constant = 0,  // 恒频：输出频率恒定不变
  Sweep    = 1,  // 扫频：三角波 0→设定频率→0 往复
  Step     = 2,  // 阶频：梯形波 爬升-保持-下降-低保持 循环
};

namespace profile {

// ---- 冻结曲线参数（改数值只动这里，不动架构 —— D4）----
inline constexpr double kSweepPeriodS  = 10.0;  // 扫频三角波周期
inline constexpr double kStepPeriodS   = 20.0;  // 阶频梯形波循环周期
inline constexpr double kStepRiseFrac  = 0.24;  // 爬升段占比
inline constexpr double kStepHoldFrac  = 0.26;  // 高保持段占比
inline constexpr double kStepFallFrac  = 0.24;  // 下降段占比
// 低保持段占比 = 1 - 0.24 - 0.26 - 0.24 = 0.26
inline constexpr double kStepLowFactor = 0.5;   // 低保持频率 = max(10Hz, 设定×0.5)

// 瞬时目标频率（Hz）。setFreqHz 越界会被钳到 [10,50]（防御）。
double targetHz(Mode mode, double setFreqHz, double elapsedS) noexcept;

// 瞬时目标转速（RPM，闭环编码用）。
// 关键约束：闭环最低 100RPM≈1.67Hz，曲线低于此值无法用闭环表达 →
// 返回 0，调用方发滑行停止帧（扫频谷底电机会惯性停转，符合「0→f」语义）。
std::uint16_t targetRpm(Mode mode, int setFreqHz, double elapsedS) noexcept;

}  // namespace profile
}  // namespace massage::core
