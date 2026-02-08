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
  if (key == "io" || key == "system.io" || key == "system.stream") {
    *out = "IO";
    return true;
  }
  if (key == "time" || key == "system.time") {
    *out = "Time";
    return true;
  }
  if (key == "file" || key == "system.file") {
    *out = "File";
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
  if (key == "core.fs" || key == "system.fs") {
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

} // namespace Simple::Lang
