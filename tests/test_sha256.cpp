// SHA-256 单测（NIST FIPS 180-4 标准向量）——审计链式哈希的算法基础（D21）。
#include <string>

#include "audit/sha256.h"
#include "test_harness.h"

using namespace massage::audit;

int main() {
  // NIST 标准向量
  CHECK(sha256Hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  // 100 万字符 'a'（多块吞吐路径）
  CHECK(sha256Hex(std::string(1000000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  // 填充边界：55/56/57/63/64/65/119/120 字节（len%64 关键位）
  for (std::size_t n : {55u, 56u, 57u, 63u, 64u, 65u, 119u, 120u, 121u}) {
    const std::string s(n, 'x');
    const std::string h1 = sha256Hex(s);
    CHECK(h1.size() == 64);
    CHECK(h1 == sha256Hex(std::string(s)));  // 稳定性
    // 增量等价：拆两半分别喂不可能（一次性 API），改验证内容敏感
    std::string s2 = s;
    s2[n - 1] = 'y';
    CHECK(sha256Hex(s2) != h1);
  }

  // UTF-8 中文（payload 含中文是常态）
  const std::string cn = "审计追踪·链式哈希";
  CHECK(sha256Hex(cn).size() == 64);
  CHECK(sha256Hex(cn) == toHex(sha256(cn.data(), cn.size())));

  return th::summary();
}
