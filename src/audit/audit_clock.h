#pragma once
// 审计时钟抽象（决策 D17）：单调钟 + 墙钟双通道注入，全离线可单测。
// 单调钟：进程内自起点毫秒，不受系统时间调整影响（对齐 QElapsedTimer 语义）。
// 墙钟：Unix epoch 毫秒（UTC）。
#include <chrono>
#include <cstdint>

namespace massage::audit {

struct IClock {
  virtual ~IClock() = default;
  virtual std::int64_t monoMs() const = 0;
  virtual std::int64_t wallMs() const = 0;
};

// 生产实现：steady_clock（单调）+ system_clock（墙钟）
class SystemClock : public IClock {
 public:
  SystemClock() : monoBase_(std::chrono::steady_clock::now()) {}
  std::int64_t monoMs() const override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - monoBase_)
        .count();
  }
  std::int64_t wallMs() const override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

 private:
  std::chrono::steady_clock::time_point monoBase_;
};

}  // namespace massage::audit
