#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "RAST/rast.h"

namespace Simple::Lang::TAST {

using Program = Simple::Lang::RAST::Program;
using Decl = Simple::Lang::RAST::Decl;
using Stmt = Simple::Lang::RAST::Stmt;
using Expr = Simple::Lang::RAST::Expr;
using TypeRef = Simple::Lang::RAST::TypeRef;

struct TypedExpr {
  const Expr* expr = nullptr;
  TypeRef type;
};

struct TypedStmt {
  const Stmt* stmt = nullptr;
  bool always_returns = false;
};

using ExprTypeMap = std::unordered_map<const Expr*, TypeRef>;
using MutabilityFacts = std::unordered_map<std::string, Simple::Lang::Mutability>;

struct AbiFacts {
  std::vector<TypeRef> extern_param_types;
  std::vector<TypeRef> extern_return_types;
};

struct TypedProgram {
  const Simple::Lang::RAST::ResolvedProgram* resolved = nullptr;
  std::vector<TypedExpr> typed_exprs;
  std::vector<TypedStmt> typed_stmts;
  ExprTypeMap expr_types;
  MutabilityFacts mutability_facts;
  AbiFacts abi_facts;
};

struct TypedProgramView {
  const Program* program = nullptr;
};

} // namespace Simple::Lang::TAST
