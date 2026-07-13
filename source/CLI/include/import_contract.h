#pragma once

#include <string>

namespace Simple::CLI {

std::string ImportPathWithSimpleExtension(const std::string& import_path);
bool IsExplicitRelativeImportPath(const std::string& import_path);

} // namespace Simple::CLI
