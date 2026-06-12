#pragma once

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

} // namespace Simple::Lang::TAST
