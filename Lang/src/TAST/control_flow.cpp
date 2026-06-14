#include "TAST/control_flow.h"

#include "TAST/types.h"

namespace Simple::Lang::TAST {
namespace {

Flow Sequence(Flow before, Flow after) {
  Flow out;
  out.may_break = before.may_break || after.may_break;
  out.may_skip = before.may_skip || after.may_skip;
  if (before.always_returns) {
    out.always_returns = true;
    out.may_fallthrough = false;
    return out;
  }
  out.always_returns = after.always_returns;
  out.may_fallthrough = after.may_fallthrough;
  return out;
}

Flow MergeBranches(const std::vector<Flow>& branches, bool exhaustive) {
  Flow out;
  out.may_fallthrough = !exhaustive;
  out.always_returns = exhaustive && !branches.empty();
  for (const auto& branch : branches) {
    out.may_break = out.may_break || branch.may_break;
    out.may_skip = out.may_skip || branch.may_skip;
    out.may_fallthrough = out.may_fallthrough || branch.may_fallthrough;
    out.always_returns = out.always_returns && branch.always_returns;
  }
  if (out.always_returns) out.may_fallthrough = false;
  return out;
}

} // namespace

Flow AnalyzeBlockFlow(const std::vector<Simple::Lang::AST::Stmt>& stmts) {
  Flow flow;
  for (const auto& stmt : stmts) {
    flow = Sequence(flow, AnalyzeStmtFlow(stmt));
    if (flow.always_returns) break;
  }
  return flow;
}

bool CheckReturnFlow(const std::vector<Simple::Lang::AST::Stmt>& stmts,
                     bool requires_return,
                     std::string* error) {
  const Flow flow = AnalyzeBlockFlow(stmts);
  if (!requires_return || flow.always_returns) return true;
  if (error) *error = "not all paths return a value";
  return false;
}

bool CheckFunctionReturnFlow(const Simple::Lang::AST::FuncDecl& fn,
                             std::string* error) {
  const bool return_is_void = fn.return_type.name == "void";
  const bool is_main = fn.name == "main" && fn.return_type.name == "i32";
  if (return_is_void || is_main || AnalyzeBlockFlow(fn.body).always_returns) return true;
  if (error) *error = "non-void function does not return on all paths";
  return false;
}

bool CheckConditionType(const Simple::Lang::AST::TypeRef& type,
                        std::string* error) {
  if (type.pointer_depth != 0 || !IsBoolTypeName(type.name)) {
    if (error) *error = "condition must be bool";
    return false;
  }
  return true;
}

bool GetSwitchBranchValueExpr(const Simple::Lang::AST::SwitchBranch& branch,
                              bool require_explicit_return,
                              const Simple::Lang::AST::Expr** out_expr,
                              std::string* error) {
  if (!out_expr) return false;
  *out_expr = nullptr;
  if (branch.is_block) {
    if (branch.block.empty() ||
        branch.block.back().kind != StmtKind::Return ||
        !branch.block.back().has_return_expr) {
      if (error) *error = "switch branch block must end with a return value";
      return false;
    }
    *out_expr = &branch.block.back().expr;
    return true;
  }
  if (!branch.has_inline_value) {
    if (error) *error = "switch branch requires a value";
    return false;
  }
  if (require_explicit_return && !branch.is_explicit_return) {
    if (error) *error = "assigning switch branches must use 'return'";
    return false;
  }
  *out_expr = &branch.value;
  return true;
}

Flow AnalyzeStmtFlow(const Simple::Lang::AST::Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::Return:
      return {false, true, false, false};
    case StmtKind::Break:
      return {false, false, true, false};
    case StmtKind::Skip:
      return {false, false, false, true};
    case StmtKind::IfChain: {
      std::vector<Flow> branches;
      branches.reserve(stmt.if_branches.size() + (stmt.else_branch.empty() ? 0 : 1));
      for (const auto& branch : stmt.if_branches) {
        branches.push_back(AnalyzeBlockFlow(branch.second));
      }
      const bool exhaustive = !stmt.else_branch.empty();
      if (exhaustive) branches.push_back(AnalyzeBlockFlow(stmt.else_branch));
      return MergeBranches(branches, exhaustive);
    }
    case StmtKind::IfStmt: {
      std::vector<Flow> branches;
      branches.push_back(AnalyzeBlockFlow(stmt.if_then));
      const bool exhaustive = !stmt.if_else.empty();
      if (exhaustive) branches.push_back(AnalyzeBlockFlow(stmt.if_else));
      return MergeBranches(branches, exhaustive);
    }
    case StmtKind::WhileLoop: {
      Flow body = AnalyzeBlockFlow(stmt.loop_body);
      body.may_fallthrough = true;
      body.always_returns = false;
      return body;
    }
    case StmtKind::ForLoop: {
      Flow body = AnalyzeBlockFlow(stmt.loop_body);
      body.may_fallthrough = true;
      body.always_returns = false;
      return body;
    }
    default:
      return {};
  }
}

} // namespace Simple::Lang::TAST
