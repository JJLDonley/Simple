#pragma once

#include <cctype>
#include <string>

namespace Simple::Lang {

inline std::string LowerAscii(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

inline bool CanonicalizeReservedImportPath(const std::string& path, std::string* out) {
  if (!out) return false;
  const std::string key = LowerAscii(path);

  if (key == "math" || key == "system.math") {
    *out = "Math";
    return true;
  }
  if (key == "io" || key == "system.io" || key == "system.stream" || key == "core.io") {
    *out = "IO";
    return true;
  }
  if (key == "time" || key == "system.time") {
    *out = "Time";
    return true;
  }
  if (key == "core.dl" || key == "system.dl") {
    *out = "Core.DL";
    return true;
  }
  if (key == "core.os" || key == "system.os") {
    *out = "Core.Os";
    return true;
  }
  if (key == "file" || key == "system.file" ||
      key == "core.fs" || key == "system.fs") {
    *out = "Core.Fs";
    return true;
  }
  if (key == "core.log" || key == "system.log") {
    *out = "Core.Log";
    return true;
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
