#pragma once

#include <string>
#include <unordered_map>
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

bool CheckUsingImportHasPriorAlias(const std::string& path,
                                   const std::unordered_map<std::string, std::string>& aliases,
                                   std::string* error);

} // namespace Simple::Lang::RAST
