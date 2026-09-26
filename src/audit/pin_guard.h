#pragma once
// PIN 防暴破守护（决策 D20）：连续失败 N 次 → 锁定 T 时长；单调钟计时（改钟无效）。
// 纯逻辑，时钟注入可单测。审计事件（PIN_FAIL/PIN_LOCKED）由调用方记录。
#include <cstdint>

#include "audit/audit_clock.h"

namespace massage::audit {

class PinGuard {
 public:
  PinGuard(const IClock& clock, int maxFails = 5, std::int64_t lockMs = 300000)
      : clock_(clock), maxFails_(maxFails), lockMs_(lockMs) {}

  // 当前是否锁定；remainMs 非空时回填剩余锁定毫秒
  bool locked(std::int64_t* remainMs = nullptr) const {
    if (lockUntilMono_ < 0) return false;
    const std::int64_t remain = lockUntilMono_ - clock_.monoMs();
    if (remain <= 0) return false;  // 锁定已过期（惰性解除）
    if (remainMs) *remainMs = remain;
    return true;
  }

  bool pinLockedFlag() const { return locked(); }  // QML 属性用
  int failCount() const { return fails_; }

  void onSuccess() {
    fails_ = 0;
    lockUntilMono_ = -1;
  }

  // 记一次失败；返回 true = 本次失败刚好触发锁定
  bool onFailure() {
    if (locked()) return false;  // 锁定期间的尝试不累计
    ++fails_;
    if (fails_ >= maxFails_) {
      lockUntilMono_ = clock_.monoMs() + lockMs_;
      fails_ = 0;  // 锁定解除后重新计数
      return true;
    }
    return false;
  }

 private:
  const IClock& clock_;
  int maxFails_;
  std::int64_t lockMs_;
  int fails_ = 0;
  std::int64_t lockUntilMono_ = -1;
};

}  // namespace massage::audit
