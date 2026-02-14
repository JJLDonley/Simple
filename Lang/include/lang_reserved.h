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
    const char* canonical;
    std::array<const char*, 8> aliases;
    size_t alias_count;
  };

  static constexpr std::array<ReservedImportEntry, 7> kReserved = {{
      {"Core.Math", {"Math", "math", "System.math", "system.math"}, 4},
      {"Core.IO", {"IO", "io", "System.io", "system.io"}, 4},
      {"Core.Time", {"Time", "time", "System.time", "system.time"}, 4},
      {"Core.DL", {"DL", "dl", "System.dl", "system.dl"}, 4},
      {"Core.OS", {"OS", "os", "System.os", "system.os"}, 4},
      {"Core.FS", {"FS", "fs", "File", "file", "System.file", "system.file", "System.fs", "system.fs"}, 8},
      {"Core.Log", {"Log", "log", "System.log", "system.log"}, 4},
  }};

  for (const auto& entry : kReserved) {
    for (size_t i = 0; i < entry.alias_count; ++i) {
      if (path == entry.aliases[i]) {
        *out = entry.canonical;
        return true;
      }
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
  const std::string leaf = (dot == std::string::npos || dot + 1 >= base.size())
                               ? base
                               : base.substr(dot + 1);
  return LowerAscii(leaf);
}

} // namespace Simple::Lang
