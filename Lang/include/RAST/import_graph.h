#pragma once

#include <string>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::RAST {

struct ResolvedImport {
  std::string alias;
  std::string canonical_module;
};

std::vector<ResolvedImport> ResolveReservedImports(const Simple::Lang::AST::Program* program);

bool ResolveReservedImportAlias(const Simple::Lang::AST::Program* program,
                                const std::string& alias,
                                std::string* canonical);

} // namespace Simple::Lang::RAST
