#include "import_contract.h"

#include "RAST/import_paths.h"

namespace Simple::CLI {

std::string ImportPathWithSimpleExtension(const std::string& import_path) {
  return Simple::Lang::RAST::ImportPathWithSimpleExtension(import_path);
}

bool IsExplicitRelativeImportPath(const std::string& import_path) {
  return Simple::Lang::RAST::IsExplicitRelativeImportPath(import_path);
}

} // namespace Simple::CLI
