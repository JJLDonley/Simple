#include "native/path.h"

#include <filesystem>

#include "platform/platform.h"

namespace Simple::VM::Native::Path {

std::string Separator() {
  return std::string(1, std::filesystem::path::preferred_separator);
}

std::string Delimiter() {
  return std::string(1, Simple::Platform::PathListDelimiter());
}

bool IsAbsolute(const std::string& value) {
  return std::filesystem::path(value).is_absolute();
}

std::string Join(const std::string& left, const std::string& right) {
  return (std::filesystem::path(left) / std::filesystem::path(right)).lexically_normal().generic_string();
}

std::string Dirname(const std::string& value) {
  return std::filesystem::path(value).parent_path().generic_string();
}

std::string Basename(const std::string& value) {
  return std::filesystem::path(value).filename().generic_string();
}

std::string Extension(const std::string& value) {
  return std::filesystem::path(value).extension().generic_string();
}

std::string Stem(const std::string& value) {
  return std::filesystem::path(value).stem().generic_string();
}

std::string Normalize(const std::string& value) {
  return std::filesystem::path(value).lexically_normal().generic_string();
}

bool Exists(const std::string& value) {
  std::error_code ec;
  const bool result = std::filesystem::exists(std::filesystem::path(value), ec);
  return !ec && result;
}

bool IsFile(const std::string& value) {
  std::error_code ec;
  const bool result = std::filesystem::is_regular_file(std::filesystem::path(value), ec);
  return !ec && result;
}

bool IsDir(const std::string& value) {
  std::error_code ec;
  const bool result = std::filesystem::is_directory(std::filesystem::path(value), ec);
  return !ec && result;
}

} // namespace Simple::VM::Native::Path
