#pragma once
// 极简 JSON 工具（零依赖）——审计 payload 构造 + NDJSON 兜底行回放解析（D13）。
// 只处理本项目自产的扁平 JSON（键 → 标量或单层对象），不是通用解析器：
// 输入若被人为破坏，解析失败即跳过该行（回放计入 skipped），绝不崩溃。
#include <string>
#include <string_view>

namespace massage::audit {

// 字符串转 JSON 字符串字面量内容（含引号），UTF-8 原样透传，控制字符转义
std::string jsonQuote(std::string_view s);

// 从扁平 JSON 对象取键对应的原始值子串（对象值含花括号整体；字符串值含引号）
bool jsonFindRaw(std::string_view obj, std::string_view key, std::string_view& out);
// 取整数值（键不存在或不是数字 → false）
bool jsonGetInt(std::string_view obj, std::string_view key, long long& out);
// 取字符串值（去引号 + 反转义）
bool jsonGetStr(std::string_view obj, std::string_view key, std::string& out);

// payload 构造助手：jobj({"k", v}, ...) 风格由调用方拼接，这里只提供分片
inline std::string jnum(std::string_view key, long long v) {
  return std::string("\"").append(key).append("\":").append(std::to_string(v));
}
inline std::string jstr(std::string_view key, std::string_view v) {
  return std::string("\"").append(key).append("\":").append(jsonQuote(v));
}
inline std::string jraw(std::string_view key, std::string_view rawJson) {
  return std::string("\"").append(key).append("\":").append(rawJson);
}
inline std::string jbool(std::string_view key, bool v) {
  return std::string("\"").append(key).append("\":").append(v ? "true" : "false");
}

}  // namespace massage::audit
