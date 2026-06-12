#pragma once

// Phase-0 RAST facade.
//
// RAST is the resolved AST target: imports, scopes, symbols, and member
// bindings. The current validator resolves directly over the legacy AST, so
// this wrapper records the intended boundary without changing behavior yet.

#include "AST/ast.h"

namespace Simple::Lang::RAST {

using Program = Simple::Lang::AST::Program;
using Decl = Simple::Lang::AST::Decl;
using Stmt = Simple::Lang::AST::Stmt;
using Expr = Simple::Lang::AST::Expr;
using TypeRef = Simple::Lang::AST::TypeRef;

struct ResolvedProgramView {
  const Program* program = nullptr;
};

} // namespace Simple::Lang::RAST
