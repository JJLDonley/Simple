#include "AST/capture_analysis.h"

namespace Simple::Lang::ASTAnalysis {
namespace {

using CaptureScopes = std::vector<std::unordered_set<std::string>>;

bool IsBound(const CaptureScopes& scopes, const std::string& name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (it->find(name) != it->end()) return true;
  }
  return false;
}

void CollectFreeExpr(const Expr& expr,
                     CaptureScopes* scopes,
                     std::unordered_set<std::string>* free_names);

void CollectFreeStatements(const std::vector<Stmt>& body,
                           CaptureScopes* scopes,
                           std::unordered_set<std::string>* free_names) {
  scopes->emplace_back();
  for (const auto& stmt : body) {
    switch (stmt.kind) {
      case StmtKind::Return:
        if (stmt.has_return_expr) CollectFreeExpr(stmt.expr, scopes, free_names);
        break;
      case StmtKind::Expr:
        CollectFreeExpr(stmt.expr, scopes, free_names);
        break;
      case StmtKind::Assign:
        CollectFreeExpr(stmt.target, scopes, free_names);
        CollectFreeExpr(stmt.expr, scopes, free_names);
        break;
      case StmtKind::VarDecl:
        if (stmt.var_decl.has_init_expr) {
          CollectFreeExpr(stmt.var_decl.init_expr, scopes, free_names);
        }
        scopes->back().insert(stmt.var_decl.name);
        break;
      case StmtKind::IfChain:
        for (const auto& branch : stmt.if_branches) {
          CollectFreeExpr(branch.first, scopes, free_names);
          CollectFreeStatements(branch.second, scopes, free_names);
        }
        CollectFreeStatements(stmt.else_branch, scopes, free_names);
        break;
      case StmtKind::IfStmt:
        CollectFreeExpr(stmt.if_cond, scopes, free_names);
        CollectFreeStatements(stmt.if_then, scopes, free_names);
        CollectFreeStatements(stmt.if_else, scopes, free_names);
        break;
      case StmtKind::WhileLoop:
        CollectFreeExpr(stmt.loop_cond, scopes, free_names);
        CollectFreeStatements(stmt.loop_body, scopes, free_names);
        break;
      case StmtKind::ForLoop:
        scopes->emplace_back();
        if (stmt.has_loop_var_decl) {
          if (stmt.loop_var_decl.has_init_expr) {
            CollectFreeExpr(stmt.loop_var_decl.init_expr, scopes, free_names);
          }
          scopes->back().insert(stmt.loop_var_decl.name);
        }
        CollectFreeExpr(stmt.loop_iter, scopes, free_names);
        CollectFreeExpr(stmt.loop_cond, scopes, free_names);
        CollectFreeExpr(stmt.loop_step, scopes, free_names);
        CollectFreeStatements(stmt.loop_body, scopes, free_names);
        scopes->pop_back();
        break;
      case StmtKind::Break:
      case StmtKind::Skip:
        break;
    }
  }
  scopes->pop_back();
}

void CollectFreeExpr(const Expr& expr,
                     CaptureScopes* scopes,
                     std::unordered_set<std::string>* free_names) {
  if (expr.kind == ExprKind::Identifier && !IsBound(*scopes, expr.text)) {
    free_names->insert(expr.text);
  }
  if (expr.kind == ExprKind::FnLiteral) {
    scopes->emplace_back();
    for (const auto& param : expr.fn_params) scopes->back().insert(param.name);
    CollectFreeStatements(expr.fn_body, scopes, free_names);
    scopes->pop_back();
    return;
  }
  for (const auto& child : expr.children) CollectFreeExpr(child, scopes, free_names);
  for (const auto& arg : expr.args) CollectFreeExpr(arg, scopes, free_names);
  for (const auto& value : expr.field_values) CollectFreeExpr(value, scopes, free_names);
  for (const auto& branch : expr.switch_branches) {
    CaptureScopes branch_scopes = *scopes;
    branch_scopes.emplace_back();
    if (!branch.pattern_binding.empty()) branch_scopes.back().insert(branch.pattern_binding);
    if (!branch.is_default) CollectFreeExpr(branch.condition, &branch_scopes, free_names);
    if (branch.has_inline_value) CollectFreeExpr(branch.value, &branch_scopes, free_names);
    CollectFreeStatements(branch.block, &branch_scopes, free_names);
  }
}

void CollectCapturedExpr(const Expr& expr,
                         const std::unordered_set<std::string>& available,
                         std::unordered_set<std::string>* captures) {
  if (expr.kind == ExprKind::FnLiteral) {
    const auto free_names = FindFnLiteralFreeNames(expr);
    for (const auto& name : free_names) {
      if (available.find(name) != available.end()) captures->insert(name);
    }
    return;
  }
  for (const auto& child : expr.children) CollectCapturedExpr(child, available, captures);
  for (const auto& arg : expr.args) CollectCapturedExpr(arg, available, captures);
  for (const auto& value : expr.field_values) CollectCapturedExpr(value, available, captures);
  for (const auto& branch : expr.switch_branches) {
    CollectCapturedExpr(branch.condition, available, captures);
    CollectCapturedExpr(branch.value, available, captures);
    CollectCapturedLocalsFromStatements(branch.block, available, captures);
  }
}

} // namespace

std::unordered_set<std::string> FindFnLiteralFreeNames(const Expr& literal) {
  std::unordered_set<std::string> free_names;
  CaptureScopes scopes(1);
  for (const auto& param : literal.fn_params) scopes.back().insert(param.name);
  CollectFreeStatements(literal.fn_body, &scopes, &free_names);
  return free_names;
}

void CollectAllLocalNames(const std::vector<Stmt>& body,
                          std::unordered_set<std::string>* names) {
  for (const auto& stmt : body) {
    if (stmt.kind == StmtKind::VarDecl) names->insert(stmt.var_decl.name);
    if (stmt.kind == StmtKind::ForLoop && stmt.has_loop_var_decl) {
      names->insert(stmt.loop_var_decl.name);
    }
    for (const auto& branch : stmt.if_branches) CollectAllLocalNames(branch.second, names);
    CollectAllLocalNames(stmt.else_branch, names);
    CollectAllLocalNames(stmt.if_then, names);
    CollectAllLocalNames(stmt.if_else, names);
    CollectAllLocalNames(stmt.loop_body, names);
  }
}

void CollectCapturedLocalsFromStatements(
    const std::vector<Stmt>& body,
    const std::unordered_set<std::string>& available,
    std::unordered_set<std::string>* captures) {
  for (const auto& stmt : body) {
    CollectCapturedExpr(stmt.expr, available, captures);
    CollectCapturedExpr(stmt.target, available, captures);
    if (stmt.var_decl.has_init_expr) {
      CollectCapturedExpr(stmt.var_decl.init_expr, available, captures);
    }
    if (stmt.loop_var_decl.has_init_expr) {
      CollectCapturedExpr(stmt.loop_var_decl.init_expr, available, captures);
    }
    CollectCapturedExpr(stmt.if_cond, available, captures);
    CollectCapturedExpr(stmt.loop_cond, available, captures);
    CollectCapturedExpr(stmt.loop_iter, available, captures);
    CollectCapturedExpr(stmt.loop_step, available, captures);
    for (const auto& branch : stmt.if_branches) {
      CollectCapturedExpr(branch.first, available, captures);
      CollectCapturedLocalsFromStatements(branch.second, available, captures);
    }
    CollectCapturedLocalsFromStatements(stmt.else_branch, available, captures);
    CollectCapturedLocalsFromStatements(stmt.if_then, available, captures);
    CollectCapturedLocalsFromStatements(stmt.if_else, available, captures);
    CollectCapturedLocalsFromStatements(stmt.loop_body, available, captures);
  }
}

} // namespace Simple::Lang::ASTAnalysis
