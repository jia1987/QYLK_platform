#pragma once
// 审计测试共享工具：手动时钟（时序可控）+ 临时目录（自动清理）。
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>

#include "audit/audit_clock.h"

namespace atest {

class ManualClock : public massage::audit::IClock {
 public:
  std::int64_t mono = 0;
  std::int64_t wall = 1790000000000LL;  // ≈2026-09（合理墙钟）
  std::int64_t monoMs() const override { return mono; }
  std::int64_t wallMs() const override { return wall; }
  void advance(std::int64_t ms) {  // 正常流逝：两钟同步走
    mono += ms;
    wall += ms;
  }
  void jumpWall(std::int64_t ms) { wall += ms; }  // 只动墙钟 = 改钟/跳变
};

// <tmp>/massage_audit_test_<n>_<tag>_<time>/，析构递归删除
class TempDir {
 public:
  explicit TempDir(const std::string& tag) {
    static int counter = 0;
    const auto stamp = static_cast<long long>(std::time(nullptr));
    path_ = std::filesystem::temp_directory_path() /
            ("massage_audit_test_" + std::to_string(++counter) + "_" + tag + "_" +
             std::to_string(stamp));
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    std::filesystem::create_directories(path_, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  std::string file(const std::string& name) const { return (path_ / name).string(); }
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace atest
