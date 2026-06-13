#pragma once

#include <string>

namespace Simple::Lang::RAST {

struct ModuleMapEntry {
  std::string name;
  std::string path;
};

std::string ImportPathWithSimpleExtension(const std::string& import_path);
bool IsExplicitRelativeImportPath(const std::string& import_path);
bool ParseModuleMapLine(const std::string& line, ModuleMapEntry* out);

} // namespace Simple::Lang::RAST
