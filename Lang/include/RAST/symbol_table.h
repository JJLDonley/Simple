#pragma once

#include <string>

#include "RAST/rast.h"

namespace Simple::Lang::RAST {

bool AddSymbol(ResolvedProgram* out,
               SymbolKind kind,
               const std::string& name,
               const std::string& qualified_name,
               SymbolId parent,
               std::string* error);

SymbolId FindQualifiedSymbol(const ResolvedProgram* program,
                             const std::string& qualified_name,
                             SymbolKind expected_kind);

} // namespace Simple::Lang::RAST
