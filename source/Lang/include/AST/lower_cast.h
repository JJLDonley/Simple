#pragma once

#include <string>

#include "AST/ast.h"
namespace Simple::Lang::AST {

bool LowerCastProgram(const Simple::Lang::Program& in,
                      Program* out,
                      std::string* error);

bool LowerCastProgramNormalized(const Simple::Lang::Program& in,
                                NormalizedProgram* out,
                                std::string* error);

} // namespace Simple::Lang::AST
