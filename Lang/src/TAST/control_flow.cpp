#include "TAST/control_flow.h"

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
