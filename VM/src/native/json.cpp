#include "native/json.h"

#include <atomic>
#include <cctype>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace Simple::VM::Native::Json {
namespace {

std::atomic<int64_t> g_next_handle{1};
std::mutex g_mutex;
std::unordered_map<int64_t, std::string> g_documents;

} // namespace

bool IsValidText(const std::string& text) {
  size_t pos = 0;
  auto skip_ws = [&]() {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
  };
  std::function<bool()> parse_value;
  auto parse_literal = [&](const char* lit) -> bool {
    const size_t len = std::strlen(lit);
    if (text.compare(pos, len, lit) != 0) return false;
    pos += len;
    return true;
  };
  auto parse_string = [&]() -> bool {
    if (pos >= text.size() || text[pos++] != '"') return false;
    while (pos < text.size()) {
      const unsigned char c = static_cast<unsigned char>(text[pos++]);
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c != '\\') continue;
      if (pos >= text.size()) return false;
      const char e = text[pos++];
      if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' || e == 'n' ||
          e == 'r' || e == 't') {
        continue;
      }
      if (e != 'u' || pos + 4 > text.size()) return false;
      for (int i = 0; i < 4; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(text[pos++]))) return false;
      }
    }
    return false;
  };
  auto parse_number = [&]() -> bool {
    const size_t start = pos;
    if (pos < text.size() && text[pos] == '-') ++pos;
    if (pos >= text.size()) return false;
    if (text[pos] == '0') {
      ++pos;
    } else if (std::isdigit(static_cast<unsigned char>(text[pos]))) {
      while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    } else {
      return false;
    }
    if (pos < text.size() && text[pos] == '.') {
      ++pos;
      if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
      while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
      ++pos;
      if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
      if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
      while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    }
    return pos > start;
  };
  auto parse_array = [&]() -> bool {
    if (pos >= text.size() || text[pos++] != '[') return false;
    skip_ws();
    if (pos < text.size() && text[pos] == ']') {
      ++pos;
      return true;
    }
    while (true) {
      if (!parse_value()) return false;
      skip_ws();
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return true;
      }
      if (pos >= text.size() || text[pos++] != ',') return false;
    }
  };
  auto parse_object = [&]() -> bool {
    if (pos >= text.size() || text[pos++] != '{') return false;
    skip_ws();
    if (pos < text.size() && text[pos] == '}') {
      ++pos;
      return true;
    }
    while (true) {
      if (!parse_string()) return false;
      skip_ws();
      if (pos >= text.size() || text[pos++] != ':') return false;
      if (!parse_value()) return false;
      skip_ws();
      if (pos < text.size() && text[pos] == '}') {
        ++pos;
        return true;
      }
      if (pos >= text.size() || text[pos++] != ',') return false;
    }
  };
  parse_value = [&]() -> bool {
    skip_ws();
    if (pos >= text.size()) return false;
    const char c = text[pos];
    if (c == '"') return parse_string();
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
    return parse_literal("true") || parse_literal("false") || parse_literal("null");
  };
  if (!parse_value()) return false;
  skip_ws();
  return pos == text.size();
}

int64_t Parse(const std::string& text) {
  if (!IsValidText(text)) return 0;
  const int64_t handle = g_next_handle.fetch_add(1);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_documents[handle] = text;
  return handle;
}

bool Stringify(int64_t handle, std::string* out) {
  if (!out) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_documents.find(handle);
  if (it == g_documents.end()) return false;
  *out = it->second;
  return true;
}

bool Free(int64_t handle) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_documents.erase(handle) != 0;
}

} // namespace Simple::VM::Native::Json
