#pragma once

#include <cctype>
#include <array>
#include <string>

namespace Simple::Lang {

inline std::string LowerAscii(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

inline bool CanonicalizeReservedImportPath(const std::string& path, std::string* out) {
  if (!out) return false;
  struct ReservedImportEntry {
    const char* name;
    const char* canonical;
  };
  static constexpr std::array<ReservedImportEntry, 48> kReserved = {{
      {"Math", "Math"},
      {"IO", "IO"},
      {"Time", "Time"},
      {"DL", "DL"},
      {"OS", "OS"},
      {"File", "File"},
      {"Buffer", "Buffer"},
      {"Http", "Http"},
      {"Socket", "Socket"},
      {"Log", "Log"},
      {"Json", "Json"},
      {"Thread", "Thread"},
      {"Channel", "Channel"},
      {"Random", "Random"},
      {"Env", "Env"},
      {"Path", "Path"},
      {"FS", "FS"},
      {"System.Math", "Math"},
      {"System.IO", "IO"},
      {"System.Time", "Time"},
      {"System.FFI", "DL"},
      {"System.OS", "OS"},
      {"System.FS", "FS"},
      {"System.Bytes", "Buffer"},
      {"System.Buffer", "Buffer"},
      {"System.Json", "Json"},
      {"System.Thread", "Thread"},
      {"System.Channel", "Channel"},
      {"System.Random", "Random"},
      {"System.Env", "Env"},
      {"System.Path", "Path"},
      {"System.Log", "Log"},
      {"Standard.Math", "Math"},
      {"Standard.IO", "IO"},
      {"Standard.Time", "Time"},
      {"Standard.FS", "FS"},
      {"Standard.Bytes", "Buffer"},
      {"Standard.Json", "Json"},
      {"Standard.Random", "Random"},
      {"Standard.Env", "Env"},
      {"Standard.Path", "Path"},
      {"Standard.Log", "Log"},
      {"Standard.Console", "IO"},
      {"Standard.Process", "OS"},
      {"Standard.Net", "Socket"},
      {"Standard.HTTP", "Http"},
      {"Standard.HTTPS", "Http"},
      {"Standard.Terminal", "IO"},
  }};
  for (const auto& entry : kReserved) {
    if (path == entry.name) {
      *out = entry.canonical;
      return true;
    }
  }
  return false;
}

inline bool IsReservedImportPath(const std::string& path) {
  std::string canonical;
  return CanonicalizeReservedImportPath(path, &canonical);
}

inline std::string DefaultImportAlias(const std::string& import_path) {
  if (import_path.empty()) return {};
  size_t start = 0;
  const size_t slash = import_path.find_last_of('/');
  if (slash != std::string::npos) start = slash + 1;
  size_t end = import_path.size();
  if (end >= 7 && import_path.compare(end - 7, 7, ".simple") == 0) end -= 7;
  if (end <= start) return {};
  const std::string base = import_path.substr(start, end - start);
  const size_t dot = base.find_last_of('.');
  return (dot == std::string::npos || dot + 1 >= base.size()) ? base : base.substr(dot + 1);
}

} // namespace Simple::Lang
