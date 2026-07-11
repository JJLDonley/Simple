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
  static constexpr std::array<ReservedImportEntry, 44> kReserved = {{
      {"System.IO", "SystemIO"},
      {"System.FS", "FS"},
      {"System.Path", "Path"},
      {"System.Env", "Env"},
      {"System.OS", "OS"},
      {"System.Time", "Time"},
      {"System.FFI", "DL"},
      {"System.ASM", "SystemASM"},
      {"System.Bytes", "Buffer"},
      {"System.Buffer", "Buffer"},
      {"System.Json", "Json"},
      {"System.Log", "Log"},
      {"System.Random", "SystemRandom"},
      {"System.Thread", "Thread"},
      {"System.Job", "SystemJob"},
      {"System.Channel", "Channel"},
      {"System.Process", "SystemProcess"},
      {"System.Net", "SystemNet"},
      {"System.HTTP", "SystemHTTP"},
      {"System.Terminal", "SystemTerminal"},
      {"System.Capability", "SystemCapability"},
      {"System.Runtime", "SystemRuntime"},
      {"System.Debug", "SystemDebug"},
      {"Standard.IO", "StandardIO"},
      {"Standard.Console", "StandardConsole"},
      {"Standard.FS", "StandardFS"},
      {"Standard.Path", "StandardPath"},
      {"Standard.Bytes", "Buffer"},
      {"Standard.Text", "StandardText"},
      {"Standard.Json", "StandardJson"},
      {"Standard.Math", "Math"},
      {"Standard.Random", "StandardRandom"},
      {"Standard.Time", "StandardTime"},
      {"Standard.Log", "Log"},
      {"Standard.Process", "StandardProcess"},
      {"Standard.Net", "StandardNet"},
      {"Standard.HTTP", "StandardHTTP"},
      {"Standard.HTTPS", "StandardHTTPS"},
      {"Standard.Terminal", "StandardTerminal"},
      {"Standard.Promise", "StandardPromise"},
      {"Standard.Channel", "StandardChannel"},
      {"Standard.Collections", "StandardCollections"},
      {"Standard.Result", "StandardResult"},
      {"Standard.Option", "StandardOption"},
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
