#include "core/profile.h"

#include <algorithm>
#include <cmath>

namespace massage::core::profile {

namespace {

double fmodPos(double x, double period) noexcept {
  const double r = std::fmod(x, period);
  return r < 0.0 ? r + period : r;
}

}  // namespace

double targetHz(Mode mode, double setFreqHz, double elapsedS) noexcept {
  const double f = std::min(static_cast<double>(kMaxFreqHz),
                            std::max(static_cast<double>(kMinFreqHz), setFreqHz));
  switch (mode) {
    case Mode::Constant:
      return f;

    case Mode::Sweep: {
      // 三角波：0 → f（半周期）→ 0，从 0 起振
      const double p = fmodPos(elapsedS, kSweepPeriodS) / kSweepPeriodS;  // [0,1)
      const double tri = p < 0.5 ? 2.0 * p : 2.0 * (1.0 - p);             // 0→1→0
      return f * tri;
    }

    case Mode::Step: {
      // 梯形波：低保持值 L 与设定值 f 之间循环（原型示意图占比）
      const double low =
          std::max(static_cast<double>(kMinFreqHz), f * kStepLowFactor);
      const double tRise = kStepRiseFrac * kStepPeriodS;  // 4.8s
      const double tHold = kStepHoldFrac * kStepPeriodS;  // 5.2s
      const double tFall = kStepFallFrac * kStepPeriodS;  // 4.8s
      const double t = fmodPos(elapsedS, kStepPeriodS);
      if (t < tRise) return low + (f - low) * (t / tRise);
      if (t < tRise + tHold) return f;
      if (t < tRise + tHold + tFall)
        return f - (f - low) * ((t - tRise - tHold) / tFall);
      return low;
    }
  }
  return f;  // 不可达；防御性返回恒频
}

std::uint16_t targetRpm(Mode mode, int setFreqHz, double elapsedS) noexcept {
  const double hz = targetHz(mode, static_cast<double>(setFreqHz), elapsedS);
  const double rpm = std::round(hz * 60.0);
  if (rpm < static_cast<double>(kMinClosedLoopRpm)) return 0;  // 滑行段
  if (rpm > static_cast<double>(kMaxSpeedValue)) return kMaxSpeedValue;
  return static_cast<std::uint16_t>(rpm);
}

}  // namespace massage::core::profile
