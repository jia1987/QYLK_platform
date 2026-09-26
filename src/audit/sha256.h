#pragma once
// SHA-256（FIPS 180-4）——审计链式哈希（决策 D21）与导出清单（D22）用。
// 零第三方依赖，NIST 标准向量单测覆盖（test_sha256）。
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace massage::audit {

using Sha256Digest = std::array<std::uint8_t, 32>;

Sha256Digest sha256(const void* data, std::size_t len);
inline Sha256Digest sha256(std::string_view s) { return sha256(s.data(), s.size()); }

std::string toHex(const Sha256Digest& d);           // 64 位小写十六进制
inline std::string sha256Hex(std::string_view s) { return toHex(sha256(s)); }

// 流式文件哈希（导出清单 D22 用，不把整个文件读进内存）；失败返回空串
std::string sha256FileHex(const std::string& path);

}  // namespace massage::audit
