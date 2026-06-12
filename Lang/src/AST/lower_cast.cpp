#include "AST/lower_cast.h"

#include <utility>

namespace Simple::Lang::AST {
namespace {

void CollectFnLiteralExpr(const Expr& expr,
                          const std::string& binding_name,
                          const TypeRef* signature,
                          std::vector<NormalizedFnLiteralDecl>* out) {
  if (!out) return;
  if (expr.kind == ExprKind::FnLiteral) {
    NormalizedFnLiteralDecl fn;
    fn.binding_name = binding_name;
    if (signature) fn.signature = *signature;
    fn.params = expr.fn_params;
    fn.body_tokens = expr.fn_body_tokens;
    fn.line = expr.line;
    fn.column = expr.column;
    out->push_back(std::move(fn));
  }
  for (const auto& child : expr.children) CollectFnLiteralExpr(child, {}, nullptr, out);
  for (const auto& arg : expr.args) CollectFnLiteralExpr(arg, {}, nullptr, out);
  for (const auto& value : expr.field_values) CollectFnLiteralExpr(value, {}, nullptr, out);
  for (const auto& branch : expr.switch_branches) {
    if (!branch.is_default) CollectFnLiteralExpr(branch.condition, {}, nullptr, out);
    if (branch.has_inline_value) CollectFnLiteralExpr(branch.value, {}, nullptr, out);
    for (const auto& stmt : branch.block) {
      if (stmt.kind == StmtKind::VarDecl && stmt.var_decl.has_init_expr) {
        CollectFnLiteralExpr(stmt.var_decl.init_expr, stmt.var_decl.name, &stmt.var_decl.type, out);
      } else {
        CollectFnLiteralExpr(stmt.expr, {}, nullptr, out);
        CollectFnLiteralExpr(stmt.target, {}, nullptr, out);
      }
    }
  }
}

void CollectFnLiteralStmt(const Stmt& stmt, std::vector<NormalizedFnLiteralDecl>* out);

void CollectFnLiteralStmts(const std::vector<Stmt>& stmts, std::vector<NormalizedFnLiteralDecl>* out) {
  for (const auto& stmt : stmts) CollectFnLiteralStmt(stmt, out);
}

void CollectFnLiteralStmt(const Stmt& stmt, std::vector<NormalizedFnLiteralDecl>* out) {
  if (stmt.kind == StmtKind::VarDecl) {
    if (stmt.var_decl.has_init_expr) {
      CollectFnLiteralExpr(stmt.var_decl.init_expr, stmt.var_decl.name, &stmt.var_decl.type, out);
    }
    return;
  }
  CollectFnLiteralExpr(stmt.expr, {}, nullptr, out);
  CollectFnLiteralExpr(stmt.target, {}, nullptr, out);
  CollectFnLiteralExpr(stmt.if_cond, {}, nullptr, out);
  CollectFnLiteralExpr(stmt.loop_cond, {}, nullptr, out);
  CollectFnLiteralExpr(stmt.loop_iter, {}, nullptr, out);
  CollectFnLiteralExpr(stmt.loop_step, {}, nullptr, out);
  if (stmt.has_loop_var_decl && stmt.loop_var_decl.has_init_expr) {
    CollectFnLiteralExpr(stmt.loop_var_decl.init_expr, stmt.loop_var_decl.name, &stmt.loop_var_decl.type, out);
  }
  for (const auto& branch : stmt.if_branches) {
    CollectFnLiteralExpr(branch.first, {}, nullptr, out);
    CollectFnLiteralStmts(branch.second, out);
  }
  CollectFnLiteralStmts(stmt.else_branch, out);
  CollectFnLiteralStmts(stmt.if_then, out);
  CollectFnLiteralStmts(stmt.if_else, out);
  CollectFnLiteralStmts(stmt.loop_body, out);
}

void CollectFnLiteralDecls(const std::vector<Decl>& decls, std::vector<NormalizedFnLiteralDecl>* out) {
  for (const auto& decl : decls) {
    if (decl.kind == DeclKind::Function) {
      CollectFnLiteralStmts(decl.func.body, out);
    } else if (decl.kind == DeclKind::Variable && decl.var.has_init_expr) {
      CollectFnLiteralExpr(decl.var.init_expr, decl.var.name, &decl.var.type, out);
    } else if (decl.kind == DeclKind::Artifact) {
      for (const auto& method : decl.artifact.methods) CollectFnLiteralStmts(method.body, out);
      for (const auto& field : decl.artifact.fields) {
        if (field.has_init_expr) CollectFnLiteralExpr(field.init_expr, field.name, &field.type, out);
      }
    } else if (decl.kind == DeclKind::Module) {
      for (const auto& fn : decl.module.functions) CollectFnLiteralStmts(fn.body, out);
      for (const auto& var : decl.module.variables) {
        if (var.has_init_expr) CollectFnLiteralExpr(var.init_expr, var.name, &var.type, out);
      }
    }
  }
}

} // namespace

bool LowerCastProgram(const Simple::Lang::CAST::Program& in,
                      Program* out,
                      std::string* error) {
  if (!out) {
    if (error) *error = "missing AST output program";
    return false;
  }
  *out = in;
  return true;
}

bool LowerCastProgramNormalized(const Simple::Lang::CAST::Program& in,
                                NormalizedProgram* out,
                                std::string* error) {
  if (!out) {
    if (error) *error = "missing normalized AST output program";
    return false;
  }
  out->decls = in.decls;
  out->script_body.statements = in.top_level_stmts;
  out->fn_literals.clear();
  CollectFnLiteralDecls(out->decls, &out->fn_literals);
  CollectFnLiteralStmts(out->script_body.statements, &out->fn_literals);
  return true;
}

} // namespace Simple::Lang::AST
