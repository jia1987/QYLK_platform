#pragma once
// IClock 生产实现：QElapsedTimer（单调时钟，不受系统时间调整影响）
#include <QElapsedTimer>

#include "core/bus_scheduler.h"

namespace massage::app {

class SystemClock : public core::IClock {
 public:
  SystemClock() { timer_.start(); }
  std::int64_t nowMs() const override { return timer_.elapsed(); }

 private:
  QElapsedTimer timer_;
};

}  // namespace massage::app
