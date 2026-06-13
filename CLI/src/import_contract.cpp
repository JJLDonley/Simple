#include "import_contract.h"

#include <filesystem>

namespace Simple::CLI {

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

} // namespace Simple::CLI
