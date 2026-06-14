#pragma once

#include <string>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

struct Flow {
  bool may_fallthrough = true;
  bool always_returns = false;
  bool may_break = false;
  bool may_skip = false;
};

Flow AnalyzeStmtFlow(const Simple::Lang::AST::Stmt& stmt);
Flow AnalyzeBlockFlow(const std::vector<Simple::Lang::AST::Stmt>& stmts);
bool CheckReturnFlow(const std::vector<Simple::Lang::AST::Stmt>& stmts,
                     bool requires_return,
                     std::string* error);
bool CheckConditionType(const Simple::Lang::AST::TypeRef& type,
                        std::string* error);
bool GetSwitchBranchValueExpr(const Simple::Lang::AST::SwitchBranch& branch,
                              bool require_explicit_return,
                              const Simple::Lang::AST::Expr** out_expr,
                              std::string* error);

} // namespace Simple::Lang::TAST
