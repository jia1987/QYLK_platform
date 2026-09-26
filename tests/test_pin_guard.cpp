// PinGuard 单测（决策 D20）：5 次失败锁 5 分钟、单调钟计时、惰性解除、成功清零。
#include "audit/pin_guard.h"

#include "audit_test_util.h"
#include "test_harness.h"

using namespace massage::audit;

int main() {
  atest::ManualClock clk;
  PinGuard g(clk, /*maxFails=*/5, /*lockMs=*/300000);

  CHECK(!g.locked());
  CHECK(g.failCount() == 0);

  // 前 4 次失败：不锁
  for (int i = 0; i < 4; ++i) CHECK(!g.onFailure());
  CHECK(!g.locked());
  CHECK(g.failCount() == 4);

  // 第 5 次：锁定生效
  CHECK(g.onFailure());
  std::int64_t remain = 0;
  CHECK(g.locked(&remain));
  CHECK(remain > 299000 && remain <= 300000);

  // 锁定期间失败不累计
  CHECK(!g.onFailure());
  CHECK(g.locked());

  // 改墙钟无效（单调钟计时，D17/D20 交叉验证）
  clk.jumpWall(10LL * 60 * 1000);
  CHECK(g.locked());

  // 时间未到 / 到点
  clk.advance(299999);
  CHECK(g.locked());
  clk.advance(2);
  CHECK(!g.locked());  // 惰性解除

  // 解除后重新计数；成功清零
  CHECK(!g.onFailure());
  CHECK(!g.onFailure());
  CHECK(g.failCount() == 2);
  g.onSuccess();
  CHECK(g.failCount() == 0);
  CHECK(!g.locked());

  // 新一轮仍按 5 次阈值
  for (int i = 0; i < 4; ++i) CHECK(!g.onFailure());
  CHECK(!g.locked());
  CHECK(g.onFailure());
  CHECK(g.locked());

  return th::summary();
}
