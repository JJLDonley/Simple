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
  static constexpr std::array<ReservedImportEntry, 31> kReserved = {{
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

inline bool LegacyReservedImportReplacement(const std::string& path, std::string* out) {
  if (!out) return false;
  if (path == "IO") *out = "Standard.IO";
  else if (path == "Math") *out = "Standard.Math";
  else if (path == "Time") *out = "System.Time or Standard.Time";
  else if (path == "DL") *out = "System.FFI";
  else if (path == "OS") *out = "System.OS";
  else if (path == "File") *out = "System.FS";
  else if (path == "FS") *out = "Standard.FS or System.FS";
  else if (path == "Path") *out = "Standard.Path or System.Path";
  else if (path == "Env") *out = "System.Env";
  else if (path == "Random") *out = "Standard.Random or System.Random";
  else if (path == "Buffer") *out = "Standard.Bytes or System.Bytes";
  else if (path == "Json") *out = "System.Json or Standard.Json";
  else if (path == "Log") *out = "Standard.Log or System.Log";
  else if (path == "Thread") *out = "System.Thread";
  else if (path == "Channel") *out = "System.Channel";
  else if (path == "Http") *out = "Standard.HTTP or System.HTTP";
  else if (path == "Socket") *out = "Standard.Net or System.Net";
  else return false;
  return true;
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
