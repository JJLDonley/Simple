#include "RAST/resolver.h"

#include "RAST/member_resolution.h"
#include "RAST/symbol_table.h"

namespace Simple::Lang::RAST {
namespace {

bool AddCallableSymbols(ResolvedProgram* out,
                        const FuncDecl& fn,
                        const std::string& owner,
                        SymbolId parent,
                        bool has_self,
                        std::string* error);

bool AddExprBlockSymbols(ResolvedProgram* out,
                         const Expr& expr,
                         const std::string& owner,
                         SymbolId parent,
                         const std::string& path,
                         std::string* error);

bool AddStmtBlockSymbols(ResolvedProgram* out,
                         const std::vector<Stmt>& stmts,
                         const std::string& owner,
                         SymbolId parent,
                         const std::string& path,
                         std::string* error);

bool AddStmtSymbols(ResolvedProgram* out,
                    const Stmt& stmt,
                    const std::string& owner,
                    SymbolId parent,
                    const std::string& path,
                    std::string* error) {
  if (stmt.kind == StmtKind::VarDecl) {
    if (!AddSymbol(out,
                   SymbolKind::Local,
                   stmt.var_decl.name,
                   owner + "::" + path + ":" + stmt.var_decl.name,
                   parent,
                   error)) {
      return false;
    }
    if (stmt.var_decl.has_init_expr &&
        !AddExprBlockSymbols(out, stmt.var_decl.init_expr, owner, parent, path + ".init", error)) {
      return false;
    }
    return true;
  }
  if (stmt.kind == StmtKind::Assign) {
    return AddExprBlockSymbols(out, stmt.expr, owner, parent, path + ".assign", error);
  }
  if (stmt.kind == StmtKind::Expr || stmt.kind == StmtKind::Return) {
    if (stmt.kind == StmtKind::Return && !stmt.has_return_expr) return true;
    return AddExprBlockSymbols(out, stmt.expr, owner, parent, path + ".expr", error);
  }
  if (stmt.kind == StmtKind::IfChain) {
    for (size_t i = 0; i < stmt.if_branches.size(); ++i) {
      if (!AddExprBlockSymbols(out, stmt.if_branches[i].first, owner, parent, path + ".ifchain" + std::to_string(i) + ".cond", error)) {
        return false;
      }
      if (!AddStmtBlockSymbols(out, stmt.if_branches[i].second, owner, parent, path + ".ifchain" + std::to_string(i), error)) {
        return false;
      }
    }
    return AddStmtBlockSymbols(out, stmt.else_branch, owner, parent, path + ".ifchain_else", error);
  }
  if (stmt.kind == StmtKind::IfStmt) {
    if (!AddExprBlockSymbols(out, stmt.if_cond, owner, parent, path + ".if.cond", error)) return false;
    if (!AddStmtBlockSymbols(out, stmt.if_then, owner, parent, path + ".if_then", error)) return false;
    return AddStmtBlockSymbols(out, stmt.if_else, owner, parent, path + ".if_else", error);
  }
  if (stmt.kind == StmtKind::WhileLoop) {
    if (!AddExprBlockSymbols(out, stmt.loop_cond, owner, parent, path + ".while.cond", error)) return false;
    return AddStmtBlockSymbols(out, stmt.loop_body, owner, parent, path + ".while", error);
  }
  if (stmt.kind == StmtKind::ForLoop) {
    if (stmt.has_loop_var_decl) {
      if (!AddSymbol(out,
                     SymbolKind::Local,
                     stmt.loop_var_decl.name,
                     owner + "::" + path + ".for_init:" + stmt.loop_var_decl.name,
                     parent,
                     error)) {
        return false;
      }
    }
    return AddStmtBlockSymbols(out, stmt.loop_body, owner, parent, path + ".for", error);
  }
  return true;
}

bool AddExprBlockSymbols(ResolvedProgram* out,
                         const Expr& expr,
                         const std::string& owner,
                         SymbolId parent,
                         const std::string& path,
                         std::string* error) {
  for (size_t i = 0; i < expr.children.size(); ++i) {
    if (!AddExprBlockSymbols(out, expr.children[i], owner, parent, path + ".child" + std::to_string(i), error)) return false;
  }
  for (size_t i = 0; i < expr.args.size(); ++i) {
    if (!AddExprBlockSymbols(out, expr.args[i], owner, parent, path + ".arg" + std::to_string(i), error)) return false;
  }
  for (size_t i = 0; i < expr.field_values.size(); ++i) {
    if (!AddExprBlockSymbols(out, expr.field_values[i], owner, parent, path + ".field" + std::to_string(i), error)) return false;
  }
  if (expr.kind == ExprKind::Switch) {
    for (size_t i = 0; i < expr.switch_branches.size(); ++i) {
      const auto& branch = expr.switch_branches[i];
      if (!branch.is_default &&
          !AddExprBlockSymbols(out, branch.condition, owner, parent, path + ".switch" + std::to_string(i) + ".cond", error)) {
        return false;
      }
      if (branch.is_block &&
          !AddStmtBlockSymbols(out, branch.block, owner, parent, path + ".switch" + std::to_string(i), error)) {
        return false;
      }
      if (branch.has_inline_value &&
          !AddExprBlockSymbols(out, branch.value, owner, parent, path + ".switch" + std::to_string(i) + ".value", error)) {
        return false;
      }
    }
  }
  return true;
}

bool AddStmtBlockSymbols(ResolvedProgram* out,
                         const std::vector<Stmt>& stmts,
                         const std::string& owner,
                         SymbolId parent,
                         const std::string& path,
                         std::string* error) {
  for (size_t i = 0; i < stmts.size(); ++i) {
    if (!AddStmtSymbols(out, stmts[i], owner, parent, path + ".s" + std::to_string(i), error)) {
      return false;
    }
  }
  return true;
}

bool AddCallableSymbols(ResolvedProgram* out,
                        const FuncDecl& fn,
                        const std::string& owner,
                        SymbolId parent,
                        bool has_self,
                        std::string* error) {
  if (has_self && !AddSymbol(out, SymbolKind::Self, "self", owner + "::self", parent, error)) {
    return false;
  }
  for (const auto& param : fn.params) {
    if (!AddSymbol(out,
                   SymbolKind::Parameter,
                   param.name,
                   owner + "::param:" + param.name,
                   parent,
                   error)) {
      return false;
    }
  }
  return AddStmtBlockSymbols(out, fn.body, owner, parent, "body", error);
}


} // namespace

bool ResolveAstProgram(const Simple::Lang::AST::Program& program,
                       ResolvedProgram* out,
                       std::string* error) {
  if (!out) {
    if (error) *error = "missing RAST output program";
    return false;
  }
  out->program = &program;
  out->symbols.clear();
  out->by_qualified_name.clear();

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case DeclKind::ModuleHeader: {
        if (!AddSymbol(out, SymbolKind::Module, decl.module_header.name, decl.module_header.name, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      }
      case DeclKind::Import: {
        const std::string name = decl.import_decl.has_alias ? decl.import_decl.alias
                                                            : decl.import_decl.path;
        if (!AddSymbol(out, SymbolKind::Import, name, "import:" + name, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      }
      case DeclKind::Extern: {
        const std::string qualified = decl.ext.has_module ? decl.ext.module + "." + decl.ext.name
                                                          : decl.ext.name;
        if (!AddSymbol(out, SymbolKind::Extern, decl.ext.name, qualified, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      }
      case DeclKind::Function: {
        const SymbolId parent = static_cast<SymbolId>(out->symbols.size());
        if (!AddSymbol(out, SymbolKind::Function, decl.func.name, decl.func.name, kInvalidSymbolId, error)) {
          return false;
        }
        if (!AddCallableSymbols(out, decl.func, decl.func.name, parent, false, error)) return false;
        break;
      }
      case DeclKind::Variable:
        if (!AddSymbol(out, SymbolKind::Global, decl.var.name, decl.var.name, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      case DeclKind::Artifact: {
        const SymbolId parent = static_cast<SymbolId>(out->symbols.size());
        if (!AddSymbol(out, SymbolKind::Artifact, decl.artifact.name, decl.artifact.name, kInvalidSymbolId, error)) {
          return false;
        }
        for (const auto& field : decl.artifact.fields) {
          if (!AddSymbol(out,
                         SymbolKind::ArtifactField,
                         field.name,
                         decl.artifact.name + "." + field.name,
                         parent,
                         error)) {
            return false;
          }
        }
        for (const auto& method : decl.artifact.methods) {
          const SymbolId method_parent = static_cast<SymbolId>(out->symbols.size());
          const std::string method_qualified = decl.artifact.name + "." + method.name;
          if (!AddSymbol(out,
                         SymbolKind::ArtifactMethod,
                         method.name,
                         method_qualified,
                         parent,
                         error)) {
            return false;
          }
          if (!AddCallableSymbols(out, method, method_qualified, method_parent, true, error)) return false;
        }
        break;
      }
      case DeclKind::Module: {
        const SymbolId parent = static_cast<SymbolId>(out->symbols.size());
        if (!AddSymbol(out, SymbolKind::Module, decl.module.name, decl.module.name, kInvalidSymbolId, error)) {
          return false;
        }
        for (const auto& var : decl.module.variables) {
          if (!AddSymbol(out,
                         SymbolKind::ModuleVariable,
                         var.name,
                         decl.module.name + "." + var.name,
                         parent,
                         error)) {
            return false;
          }
        }
        for (const auto& fn : decl.module.functions) {
          const SymbolId fn_parent = static_cast<SymbolId>(out->symbols.size());
          const std::string fn_qualified = decl.module.name + "." + fn.name;
          if (!AddSymbol(out,
                         SymbolKind::ModuleFunction,
                         fn.name,
                         fn_qualified,
                         parent,
                         error)) {
            return false;
          }
          if (!AddCallableSymbols(out, fn, fn_qualified, fn_parent, false, error)) return false;
        }
        break;
      }
      case DeclKind::Enum: {
        const SymbolId parent = static_cast<SymbolId>(out->symbols.size());
        if (!AddSymbol(out, SymbolKind::Enum, decl.enm.name, decl.enm.name, kInvalidSymbolId, error)) {
          return false;
        }
        for (const auto& member : decl.enm.members) {
          if (!AddSymbol(out,
                         SymbolKind::EnumMember,
                         member.name,
                         decl.enm.name + "." + member.name,
                         parent,
                         error)) {
            return false;
          }
        }
        break;
      }
    }
  }
  ResolveProgramMemberRefs(out);
  return true;
}

} // namespace Simple::Lang::RAST
