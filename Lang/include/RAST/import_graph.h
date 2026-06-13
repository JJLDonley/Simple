#pragma once

#include <string>

#include "AST/ast.h"

namespace Simple::Lang::RAST {

bool ResolveReservedImportAlias(const Simple::Lang::AST::Program* program,
                                const std::string& alias,
                                std::string* canonical);

} // namespace Simple::Lang::RAST
