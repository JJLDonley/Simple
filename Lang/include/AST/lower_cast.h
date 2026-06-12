#pragma once

#include <string>

#include "AST/ast.h"
#include "CAST/cast.h"

namespace Simple::Lang::AST {

bool LowerCastProgram(const Simple::Lang::CAST::Program& in,
                      Program* out,
                      std::string* error);

} // namespace Simple::Lang::AST
