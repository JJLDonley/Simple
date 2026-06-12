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
void CollectLoopStmt(const Stmt& stmt, std::vector<NormalizedLoop>* out);
void CollectIfChainStmt(const Stmt& stmt, std::vector<NormalizedIfChain>* out);
void CollectSwitchExpr(const Expr& expr, std::vector<NormalizedSwitch>* out);
void CollectSwitchStmt(const Stmt& stmt, std::vector<NormalizedSwitch>* out);

void CollectFnLiteralStmts(const std::vector<Stmt>& stmts, std::vector<NormalizedFnLiteralDecl>* out) {
  for (const auto& stmt : stmts) CollectFnLiteralStmt(stmt, out);
}

void CollectLoopStmts(const std::vector<Stmt>& stmts, std::vector<NormalizedLoop>* out) {
  for (const auto& stmt : stmts) CollectLoopStmt(stmt, out);
}

void CollectIfChainStmts(const std::vector<Stmt>& stmts, std::vector<NormalizedIfChain>* out) {
  for (const auto& stmt : stmts) CollectIfChainStmt(stmt, out);
}

void CollectSwitchStmts(const std::vector<Stmt>& stmts, std::vector<NormalizedSwitch>* out) {
  for (const auto& stmt : stmts) CollectSwitchStmt(stmt, out);
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

void CollectLoopStmt(const Stmt& stmt, std::vector<NormalizedLoop>* out) {
  if (!out) return;
  if (stmt.kind == StmtKind::WhileLoop) {
    NormalizedLoop loop;
    loop.kind = NormalizedLoopKind::While;
    loop.condition = stmt.loop_cond;
    loop.body = stmt.loop_body;
    out->push_back(std::move(loop));
  } else if (stmt.kind == StmtKind::ForLoop) {
    NormalizedLoop loop;
    loop.kind = NormalizedLoopKind::For;
    loop.has_initializer = true;
    loop.has_loop_var_decl = stmt.has_loop_var_decl;
    if (stmt.has_loop_var_decl) loop.loop_var_decl = stmt.loop_var_decl;
    loop.initializer = stmt.loop_iter;
    loop.condition = stmt.loop_cond;
    loop.step = stmt.loop_step;
    loop.body = stmt.loop_body;
    out->push_back(std::move(loop));
  }
  for (const auto& branch : stmt.if_branches) CollectLoopStmts(branch.second, out);
  CollectLoopStmts(stmt.else_branch, out);
  CollectLoopStmts(stmt.if_then, out);
  CollectLoopStmts(stmt.if_else, out);
  CollectLoopStmts(stmt.loop_body, out);
}

void CollectLoopDecls(const std::vector<Decl>& decls, std::vector<NormalizedLoop>* out) {
  for (const auto& decl : decls) {
    if (decl.kind == DeclKind::Function) {
      CollectLoopStmts(decl.func.body, out);
    } else if (decl.kind == DeclKind::Artifact) {
      for (const auto& method : decl.artifact.methods) CollectLoopStmts(method.body, out);
    } else if (decl.kind == DeclKind::Module) {
      for (const auto& fn : decl.module.functions) CollectLoopStmts(fn.body, out);
    }
  }
}

void CollectIfChainStmt(const Stmt& stmt, std::vector<NormalizedIfChain>* out) {
  if (!out) return;
  if (stmt.kind == StmtKind::IfChain) {
    NormalizedIfChain chain;
    for (const auto& branch : stmt.if_branches) {
      NormalizedIfBranch normalized_branch;
      normalized_branch.condition = branch.first;
      normalized_branch.body = branch.second;
      chain.branches.push_back(std::move(normalized_branch));
    }
    chain.else_branch = stmt.else_branch;
    out->push_back(std::move(chain));
  }
  for (const auto& branch : stmt.if_branches) CollectIfChainStmts(branch.second, out);
  CollectIfChainStmts(stmt.else_branch, out);
  CollectIfChainStmts(stmt.if_then, out);
  CollectIfChainStmts(stmt.if_else, out);
  CollectIfChainStmts(stmt.loop_body, out);
}

void CollectIfChainDecls(const std::vector<Decl>& decls, std::vector<NormalizedIfChain>* out) {
  for (const auto& decl : decls) {
    if (decl.kind == DeclKind::Function) {
      CollectIfChainStmts(decl.func.body, out);
    } else if (decl.kind == DeclKind::Artifact) {
      for (const auto& method : decl.artifact.methods) CollectIfChainStmts(method.body, out);
    } else if (decl.kind == DeclKind::Module) {
      for (const auto& fn : decl.module.functions) CollectIfChainStmts(fn.body, out);
    }
  }
}

void CollectSwitchExpr(const Expr& expr, std::vector<NormalizedSwitch>* out) {
  if (!out) return;
  if (expr.kind == ExprKind::Switch) {
    NormalizedSwitch sw;
    if (!expr.children.empty()) sw.scrutinee = expr.children[0];
    for (const auto& branch : expr.switch_branches) {
      NormalizedSwitchBranch normalized;
      normalized.is_default = branch.is_default;
      normalized.is_block = branch.is_block;
      normalized.has_inline_value = branch.has_inline_value;
      normalized.is_explicit_return = branch.is_explicit_return;
      normalized.condition = branch.condition;
      normalized.value = branch.value;
      normalized.block = branch.block;
      sw.branches.push_back(std::move(normalized));
    }
    out->push_back(std::move(sw));
  }
  for (const auto& child : expr.children) CollectSwitchExpr(child, out);
  for (const auto& arg : expr.args) CollectSwitchExpr(arg, out);
  for (const auto& value : expr.field_values) CollectSwitchExpr(value, out);
  for (const auto& branch : expr.switch_branches) {
    if (!branch.is_default) CollectSwitchExpr(branch.condition, out);
    if (branch.has_inline_value) CollectSwitchExpr(branch.value, out);
    CollectSwitchStmts(branch.block, out);
  }
}

void CollectSwitchStmt(const Stmt& stmt, std::vector<NormalizedSwitch>* out) {
  CollectSwitchExpr(stmt.expr, out);
  CollectSwitchExpr(stmt.target, out);
  CollectSwitchExpr(stmt.if_cond, out);
  CollectSwitchExpr(stmt.loop_cond, out);
  CollectSwitchExpr(stmt.loop_iter, out);
  CollectSwitchExpr(stmt.loop_step, out);
  if (stmt.kind == StmtKind::VarDecl && stmt.var_decl.has_init_expr) CollectSwitchExpr(stmt.var_decl.init_expr, out);
  if (stmt.has_loop_var_decl && stmt.loop_var_decl.has_init_expr) CollectSwitchExpr(stmt.loop_var_decl.init_expr, out);
  for (const auto& branch : stmt.if_branches) {
    CollectSwitchExpr(branch.first, out);
    CollectSwitchStmts(branch.second, out);
  }
  CollectSwitchStmts(stmt.else_branch, out);
  CollectSwitchStmts(stmt.if_then, out);
  CollectSwitchStmts(stmt.if_else, out);
  CollectSwitchStmts(stmt.loop_body, out);
}

void CollectSwitchDecls(const std::vector<Decl>& decls, std::vector<NormalizedSwitch>* out) {
  for (const auto& decl : decls) {
    if (decl.kind == DeclKind::Function) {
      CollectSwitchStmts(decl.func.body, out);
    } else if (decl.kind == DeclKind::Variable && decl.var.has_init_expr) {
      CollectSwitchExpr(decl.var.init_expr, out);
    } else if (decl.kind == DeclKind::Artifact) {
      for (const auto& method : decl.artifact.methods) CollectSwitchStmts(method.body, out);
      for (const auto& field : decl.artifact.fields) {
        if (field.has_init_expr) CollectSwitchExpr(field.init_expr, out);
      }
    } else if (decl.kind == DeclKind::Module) {
      for (const auto& fn : decl.module.functions) CollectSwitchStmts(fn.body, out);
      for (const auto& var : decl.module.variables) {
        if (var.has_init_expr) CollectSwitchExpr(var.init_expr, out);
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
  out->loops.clear();
  out->if_chains.clear();
  out->switches.clear();
  CollectFnLiteralDecls(out->decls, &out->fn_literals);
  CollectFnLiteralStmts(out->script_body.statements, &out->fn_literals);
  CollectLoopDecls(out->decls, &out->loops);
  CollectLoopStmts(out->script_body.statements, &out->loops);
  CollectIfChainDecls(out->decls, &out->if_chains);
  CollectIfChainStmts(out->script_body.statements, &out->if_chains);
  CollectSwitchDecls(out->decls, &out->switches);
  CollectSwitchStmts(out->script_body.statements, &out->switches);
  return true;
}

} // namespace Simple::Lang::AST
