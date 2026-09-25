#pragma once
// MockBus —— 进程内 485 从机仿真（tools/simulator/rs485_sim.py 的 C++ 精简版）。
// 用途：无硬件/无虚拟串口时驱动 UI 开发与冒烟测试。物理行为与 Python 模拟器
// 同源：缓启动爬升、故障闭锁、复位语义、在位模拟。
#include <functional>
#include <map>
#include <queue>
#include <vector>

#include <QObject>
#include <QTimer>

#include "core/bus_scheduler.h"

namespace massage::app {

class MockBus : public QObject, public core::ITransport {
  Q_OBJECT
 public:
  explicit MockBus(std::vector<std::uint8_t> presentAddrs, QObject* parent = nullptr);

  // ITransport
  bool send(const core::Frame& f) override;

  // 应答交付出口（AppCore 接 deliverReply）
  std::function<void(const core::Frame&)> replySink;

  // 调试辅助：外部改变某头在位状态（后续绑定界面/维护模式用）
  void setPresent(std::uint8_t addr, bool present);

 private:
  struct MHead {
    std::uint8_t addr = 0;
    bool present = true;
    int targetRpm = 0;
    double rpm = 0.0;
    int dir = 0;  // 0=A 1=B
    std::uint8_t fault = 0;
    double temp = 32.0;
    std::uint8_t soft = 0x10;
    bool closedLoop = true;
    std::uint8_t polePairs = 2;
    double position = 0.0;
    bool braking = false;
  };

  void handle(const core::Frame& f);
  void applyCtrl(MHead& h, int value, int status);
  void enqueueReply(std::uint8_t header, std::uint8_t addr, std::uint8_t b3,
                    std::uint8_t b4, std::uint8_t b5);

  std::map<std::uint8_t, MHead> heads_;
  std::queue<core::Frame> outQueue_;
  QTimer deliverTimer_;   // 5ms：按序交付应答（模拟 2ms 换向间隔）
  QTimer physicsTimer_;   // 50ms：电机物理仿真
};

}  // namespace massage::app
