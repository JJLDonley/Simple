#pragma once

#include <string>

#include "AST/ast.h"
#include "RAST/rast.h"

namespace Simple::Lang::RAST {

bool ResolveProgram(const Simple::Lang::AST::Program& program,
                    ResolvedProgram* out,
                    std::string* error);

} // namespace Simple::Lang::RAST
