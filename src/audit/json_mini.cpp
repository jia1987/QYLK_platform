#include "audit/json_mini.h"

#include <cstdio>

namespace massage::audit {

std::string jsonQuote(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(c);  // UTF-8 字节原样透传
        }
    }
  }
  out.push_back('"');
  return out;
}

namespace {

// 在 obj 中定位 "key" 后的值起点（跳过冒号与空白）；失败返回 npos
std::size_t valueStart(std::string_view obj, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t pos = 0;
  while ((pos = obj.find(needle, pos)) != std::string_view::npos) {
    std::size_t p = pos + needle.size();
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) ++p;
    if (p < obj.size() && obj[p] == ':') {
      ++p;
      while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) ++p;
      return p;
    }
    pos += needle.size();  // 不是键位置（可能是值里的字符串），继续找
  }
  return std::string_view::npos;
}

// 从值起点扫出完整值子串
std::string_view scanValue(std::string_view obj, std::size_t start) {
  if (start >= obj.size()) return {};
  const char c0 = obj[start];
  if (c0 == '{' || c0 == '[') {
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (std::size_t i = start; i < obj.size(); ++i) {
      const char c = obj[i];
      if (inStr) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"') inStr = false;
        continue;
      }
      if (c == '"') inStr = true;
      else if (c == '{' || c == '[') ++depth;
      else if (c == '}' || c == ']') {
        --depth;
        if (depth == 0) return obj.substr(start, i - start + 1);
      }
    }
    return {};  // 不配对：损坏输入
  }
  if (c0 == '"') {
    bool esc = false;
    for (std::size_t i = start + 1; i < obj.size(); ++i) {
      const char c = obj[i];
      if (esc) { esc = false; continue; }
      if (c == '\\') { esc = true; continue; }
      if (c == '"') return obj.substr(start, i - start + 1);
    }
    return {};
  }
  // 标量：到 , } ] 或结尾
  std::size_t i = start;
  while (i < obj.size() && obj[i] != ',' && obj[i] != '}' && obj[i] != ']') ++i;
  std::string_view v = obj.substr(start, i - start);
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
  return v;
}

bool unescape(std::string_view quoted, std::string& out) {
  if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"')
    return false;
  out.clear();
  out.reserve(quoted.size());
  for (std::size_t i = 1; i + 1 < quoted.size(); ++i) {
    const char c = quoted[i];
    if (c != '\\') { out.push_back(c); continue; }
    if (i + 2 >= quoted.size()) return false;
    const char e = quoted[++i];
    switch (e) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'u': {
        // \uXXXX → 仅处理 BMP：编成 UTF-8（代理对不做——本项目 payload 不产生）
        if (i + 4 >= quoted.size()) return false;
        unsigned cp = 0;
        for (int k = 1; k <= 4; ++k) {
          const char hx = quoted[i + k];
          cp <<= 4;
          if (hx >= '0' && hx <= '9') cp |= static_cast<unsigned>(hx - '0');
          else if (hx >= 'a' && hx <= 'f') cp |= static_cast<unsigned>(hx - 'a' + 10);
          else if (hx >= 'A' && hx <= 'F') cp |= static_cast<unsigned>(hx - 'A' + 10);
          else return false;
        }
        i += 4;
        if (cp < 0x80) {
          out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
          out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
          out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        break;
      }
      default: return false;
    }
  }
  return true;
}

}  // namespace

bool jsonFindRaw(std::string_view obj, std::string_view key, std::string_view& out) {
  const std::size_t start = valueStart(obj, key);
  if (start == std::string_view::npos) return false;
  out = scanValue(obj, start);
  return !out.empty();
}

bool jsonGetInt(std::string_view obj, std::string_view key, long long& out) {
  std::string_view raw;
  if (!jsonFindRaw(obj, key, raw)) return false;
  if (raw == "null") return false;
  try {
    std::size_t consumed = 0;
    const long long v = std::stoll(std::string(raw), &consumed);
    if (consumed != raw.size()) return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool jsonGetStr(std::string_view obj, std::string_view key, std::string& out) {
  std::string_view raw;
  if (!jsonFindRaw(obj, key, raw)) return false;
  if (raw == "null") { out.clear(); return true; }  // 显式 null → 空串
  return unescape(raw, out);
}

}  // namespace massage::audit
