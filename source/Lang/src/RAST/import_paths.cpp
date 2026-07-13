#include "RAST/import_paths.h"

#include <filesystem>
#include <utility>

namespace Simple::Lang::RAST {

std::string ImportPathWithSimpleExtension(const std::string& import_path) {
  std::filesystem::path path(import_path);
  if (path.has_extension()) return import_path;
  return import_path + ".simple";
}

bool IsExplicitRelativeImportPath(const std::string& import_path) {
  if (import_path.empty()) return false;
  std::filesystem::path path(import_path);
  return path.is_relative() && (import_path[0] == '.' || path.has_parent_path());
}

bool ParseModuleMapLine(const std::string& line, ModuleMapEntry* out) {
  if (!out) return false;
  std::string body = line;
  const size_t comment = body.find("//");
  if (comment != std::string::npos) body = body.substr(0, comment);
  const size_t eq = body.find('=');
  if (eq == std::string::npos) return false;
  std::string name = body.substr(0, eq);
  std::string path = body.substr(eq + 1);
  auto trim = [](std::string value) {
    const size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string{};
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
  };
  name = trim(std::move(name));
  path = trim(std::move(path));
  if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
    path = path.substr(1, path.size() - 2);
  }
  if (name.empty() || path.empty()) return false;
  out->name = std::move(name);
  out->path = std::move(path);
  return true;
}

} // namespace Simple::Lang::RAST
