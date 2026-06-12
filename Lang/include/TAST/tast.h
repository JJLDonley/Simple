#pragma once

#include "RAST/rast.h"

namespace Simple::Lang::TAST {

using Program = Simple::Lang::RAST::Program;
using Decl = Simple::Lang::RAST::Decl;
using Stmt = Simple::Lang::RAST::Stmt;
using Expr = Simple::Lang::RAST::Expr;
using TypeRef = Simple::Lang::RAST::TypeRef;

struct TypedProgram {
  const Simple::Lang::RAST::ResolvedProgram* resolved = nullptr;
};

struct TypedProgramView {
  const Program* program = nullptr;
};

} // namespace Simple::Lang::TAST
