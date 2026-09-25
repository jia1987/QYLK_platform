#pragma once
// 最小测试断言（零第三方依赖）。测试输出即注册验证记录的一部分。
#include <cstdio>

namespace th {
inline int g_checks = 0;
inline int g_fails = 0;

inline int summary() {
  std::printf("%d checks, %d failures\n", g_checks, g_fails);
  return g_fails == 0 ? 0 : 1;
}
}  // namespace th

#define CHECK(cond)                                                      \
  do {                                                                   \
    ++th::g_checks;                                                      \
    if (!(cond)) {                                                       \
      ++th::g_fails;                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                    \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                            \
  do {                                                                   \
    ++th::g_checks;                                                      \
    const double da_ = (a), db_ = (b);                                   \
    if (da_ - db_ > (eps) || db_ - da_ > (eps)) {                        \
      ++th::g_fails;                                                     \
      std::printf("FAIL %s:%d: %s ~= %s (%g vs %g)\n", __FILE__,         \
                  __LINE__, #a, #b, da_, db_);                           \
    }                                                                    \
  } while (0)
