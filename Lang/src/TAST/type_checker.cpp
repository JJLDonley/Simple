#include "TAST/type_checker.h"

#include "TAST/control_flow.h"
#include "lang_validate.h"

namespace Simple::Lang::TAST {

bool CheckResolvedProgram(const Simple::Lang::RAST::ResolvedProgram& resolved,
                          TypedProgram* out,
                          std::string* error) {
  if (!resolved.program) {
    if (error) *error = "missing resolved program input";
    return false;
  }
  if (!ValidateProgram(*resolved.program, error)) return false;
  if (out) {
    out->resolved = &resolved;
    out->typed_exprs.clear();
    out->typed_stmts.clear();
    out->expr_types.clear();
    out->mutability_facts.clear();
    out->abi_facts.extern_param_types.clear();
    out->abi_facts.extern_return_types.clear();
    for (const auto& decl : resolved.program->decls) {
      if (decl.kind == Simple::Lang::AST::DeclKind::Variable) {
        out->mutability_facts[decl.var.name] = decl.var.mutability;
        if (decl.var.has_init_expr) {
          TypedExpr typed_expr;
          typed_expr.expr = &decl.var.init_expr;
          typed_expr.type = decl.var.type;
          out->typed_exprs.push_back(typed_expr);
          out->expr_types.emplace(typed_expr.expr, typed_expr.type);
        }
      } else if (decl.kind == Simple::Lang::AST::DeclKind::Extern) {
        out->abi_facts.extern_return_types.push_back(decl.ext.return_type);
        for (const auto& param : decl.ext.params) {
          out->abi_facts.extern_param_types.push_back(param.type);
        }
      } else if (decl.kind == Simple::Lang::AST::DeclKind::Function) {
        for (const auto& stmt : decl.func.body) {
          TypedStmt typed_stmt;
          typed_stmt.stmt = &stmt;
          typed_stmt.always_returns = AnalyzeStmtFlow(stmt).always_returns;
          out->typed_stmts.push_back(typed_stmt);
        }
      }
    }
  }
  return true;
}

} // namespace Simple::Lang::TAST
