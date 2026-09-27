// @srs SRS-035 SRS-036 SRS-037 SRS-038 SRS-039
// ProfileEngine 单测 —— 决策 D4 冻结曲线参数的行为验证。
// 数值向量：扫频 10s 三角波；阶频 20s 梯形波（24/26/24/26%，低保持=max(10Hz, f×50%)）。
#include <cmath>

#include "core/profile.h"
#include "test_harness.h"

using namespace massage::core;

int main() {
  // ---- 恒频 ----
  CHECK_NEAR(profile::targetHz(Mode::Constant, 30, 0.0), 30.0, 1e-9);
  CHECK_NEAR(profile::targetHz(Mode::Constant, 30, 1234.5), 30.0, 1e-9);
  // 防御：设定值越界钳位
  CHECK_NEAR(profile::targetHz(Mode::Constant, 100, 0.0), 50.0, 1e-9);
  CHECK_NEAR(profile::targetHz(Mode::Constant, 5, 0.0), 10.0, 1e-9);

  // ---- 扫频：三角波 0→f→0，周期 10s（设定 50Hz）----
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 50, 0.0), 0.0, 1e-9);
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 50, 1.25), 12.5, 1e-9);  // 1/4 半程
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 50, 2.5), 25.0, 1e-9);  // 半程中点
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 50, 5.0), 50.0, 1e-9);  // 顶点
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 50, 7.5), 25.0, 1e-9);  // 回落
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 50, 10.0), 0.0, 1e-9);  // 周期回到 0
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 50, 12.5), 25.0, 1e-9); // 第二周期
  CHECK_NEAR(profile::targetHz(Mode::Sweep, 30, 1.25), 7.5, 1e-9);  // 设定 30Hz

  // ---- 阶频：梯形波 20s（设定 40Hz，低保持 = max(10, 20) = 20Hz）----
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 0.0), 20.0, 1e-9);   // 爬升起点=低保持
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 2.4), 30.0, 1e-9);   // 爬升中点
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 4.8), 40.0, 1e-9);   // 爬升结束
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 9.9), 40.0, 1e-9);   // 高保持
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 12.4), 30.0, 1e-9);  // 下降中点
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 14.8), 20.0, 1e-9);  // 下降结束
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 19.0), 20.0, 1e-9);  // 低保持
  CHECK_NEAR(profile::targetHz(Mode::Step, 40, 22.4), 30.0, 1e-9);  // 第二周期爬升中点
  // 设定 10Hz：低保持 = max(10, 5) = 10 → 全程恒定 10
  CHECK_NEAR(profile::targetHz(Mode::Step, 10, 0.0), 10.0, 1e-9);
  CHECK_NEAR(profile::targetHz(Mode::Step, 10, 7.0), 10.0, 1e-9);
  CHECK_NEAR(profile::targetHz(Mode::Step, 10, 16.0), 10.0, 1e-9);

  // ---- targetRpm：闭环下限映射（<100RPM → 0 滑行段）与上限 ----
  CHECK(profile::targetRpm(Mode::Constant, 10, 0.0) == 600);   // 10Hz=600RPM
  CHECK(profile::targetRpm(Mode::Constant, 50, 0.0) == 3000);  // 50Hz=3000≤3001
  CHECK(profile::targetRpm(Mode::Sweep, 50, 0.0) == 0);        // 扫频起点=滑行
  CHECK(profile::targetRpm(Mode::Sweep, 50, 0.05) == 0);       // 0.5Hz→30RPM<100→滑行
  CHECK(profile::targetRpm(Mode::Sweep, 50, 0.5) == 300);      // 5Hz→300RPM
  CHECK(profile::targetRpm(Mode::Sweep, 50, 2.5) == 1500);     // 25Hz
  CHECK(profile::targetRpm(Mode::Sweep, 50, 5.0) == 3000);     // 顶点
  CHECK(profile::targetRpm(Mode::Step, 40, 0.0) == 1200);      // 低保持 20Hz
  CHECK(profile::targetRpm(Mode::Step, 40, 5.0) == 2400);      // 高保持 40Hz

  return th::summary();
}
