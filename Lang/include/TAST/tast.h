#pragma once

// Phase-0 TAST facade.
//
// TAST is the typed AST target: resolved nodes plus stable expression/type,
// mutability, and ABI facts. The current validator computes these facts on the
// fly; this module boundary lets later patches persist them explicitly.

#include "RAST/rast.h"

namespace Simple::Lang::TAST {

using Program = Simple::Lang::RAST::Program;
using Decl = Simple::Lang::RAST::Decl;
using Stmt = Simple::Lang::RAST::Stmt;
using Expr = Simple::Lang::RAST::Expr;
using TypeRef = Simple::Lang::RAST::TypeRef;

struct TypedProgramView {
  const Program* program = nullptr;
};

} // namespace Simple::Lang::TAST
