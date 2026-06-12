#include "RAST/resolver.h"

namespace Simple::Lang::RAST {
namespace {

bool AddSymbol(ResolvedProgram* out,
               SymbolKind kind,
               const std::string& name,
               const std::string& qualified_name,
               SymbolId parent,
               std::string* error) {
  if (!out) return false;
  if (out->by_qualified_name.find(qualified_name) != out->by_qualified_name.end()) {
    if (error) *error = "duplicate symbol: " + qualified_name;
    return false;
  }
  Symbol symbol;
  symbol.id = static_cast<SymbolId>(out->symbols.size());
  symbol.kind = kind;
  symbol.name = name;
  symbol.qualified_name = qualified_name;
  symbol.parent = parent;
  out->by_qualified_name.emplace(qualified_name, symbol.id);
  out->symbols.push_back(std::move(symbol));
  return true;
}

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

void AddResolvedMemberRef(ResolvedProgram* out,
                          MemberRefKind kind,
                          const std::string& base,
                          const std::string& member,
                          const std::string& qualified_name,
                          SymbolId symbol) {
  MemberRef ref;
  ref.kind = kind;
  ref.base = base;
  ref.member = member;
  ref.qualified_name = qualified_name;
  ref.symbol = symbol;
  out->member_refs.push_back(std::move(ref));
}

void ResolveExprMemberRefs(ResolvedProgram* out,
                           const Expr& expr,
                           const std::string& current_artifact);

void ResolveStmtMemberRefs(ResolvedProgram* out,
                           const Stmt& stmt,
                           const std::string& current_artifact) {
  if (stmt.kind == StmtKind::VarDecl && stmt.var_decl.has_init_expr) {
    ResolveExprMemberRefs(out, stmt.var_decl.init_expr, current_artifact);
  } else if (stmt.kind == StmtKind::Assign) {
    ResolveExprMemberRefs(out, stmt.target, current_artifact);
    ResolveExprMemberRefs(out, stmt.expr, current_artifact);
  } else if (stmt.kind == StmtKind::Expr || (stmt.kind == StmtKind::Return && stmt.has_return_expr)) {
    ResolveExprMemberRefs(out, stmt.expr, current_artifact);
  } else if (stmt.kind == StmtKind::IfChain) {
    for (const auto& branch : stmt.if_branches) {
      ResolveExprMemberRefs(out, branch.first, current_artifact);
      for (const auto& child : branch.second) ResolveStmtMemberRefs(out, child, current_artifact);
    }
    for (const auto& child : stmt.else_branch) ResolveStmtMemberRefs(out, child, current_artifact);
  } else if (stmt.kind == StmtKind::IfStmt) {
    ResolveExprMemberRefs(out, stmt.if_cond, current_artifact);
    for (const auto& child : stmt.if_then) ResolveStmtMemberRefs(out, child, current_artifact);
    for (const auto& child : stmt.if_else) ResolveStmtMemberRefs(out, child, current_artifact);
  } else if (stmt.kind == StmtKind::WhileLoop) {
    ResolveExprMemberRefs(out, stmt.loop_cond, current_artifact);
    for (const auto& child : stmt.loop_body) ResolveStmtMemberRefs(out, child, current_artifact);
  } else if (stmt.kind == StmtKind::ForLoop) {
    ResolveExprMemberRefs(out, stmt.loop_iter, current_artifact);
    ResolveExprMemberRefs(out, stmt.loop_cond, current_artifact);
    ResolveExprMemberRefs(out, stmt.loop_step, current_artifact);
    for (const auto& child : stmt.loop_body) ResolveStmtMemberRefs(out, child, current_artifact);
  }
}

void ResolveExprMemberRefs(ResolvedProgram* out,
                           const Expr& expr,
                           const std::string& current_artifact) {
  if (expr.kind == ExprKind::Member && expr.op == "." && !expr.children.empty()) {
    const Expr& base = expr.children[0];
    std::string qualified;
    MemberRefKind kind = MemberRefKind::Unknown;
    if (base.kind == ExprKind::Identifier && base.text == "self" && !current_artifact.empty()) {
      qualified = current_artifact + "." + expr.text;
      kind = MemberRefKind::SelfMember;
    } else if (base.kind == ExprKind::Identifier) {
      qualified = base.text + "." + expr.text;
      kind = MemberRefKind::StaticMember;
    }
    auto it = out->by_qualified_name.find(qualified);
    if (it != out->by_qualified_name.end()) {
      AddResolvedMemberRef(out, kind, base.text, expr.text, qualified, it->second);
    }
  }
  for (const auto& child : expr.children) ResolveExprMemberRefs(out, child, current_artifact);
  for (const auto& arg : expr.args) ResolveExprMemberRefs(out, arg, current_artifact);
  for (const auto& value : expr.field_values) ResolveExprMemberRefs(out, value, current_artifact);
  if (expr.kind == ExprKind::Switch) {
    for (const auto& branch : expr.switch_branches) {
      if (!branch.is_default) ResolveExprMemberRefs(out, branch.condition, current_artifact);
      if (branch.has_inline_value) ResolveExprMemberRefs(out, branch.value, current_artifact);
      for (const auto& stmt : branch.block) ResolveStmtMemberRefs(out, stmt, current_artifact);
    }
  }
}

void ResolveFunctionMemberRefs(ResolvedProgram* out,
                               const FuncDecl& fn,
                               const std::string& current_artifact) {
  for (const auto& stmt : fn.body) ResolveStmtMemberRefs(out, stmt, current_artifact);
}

void ResolveProgramMemberRefs(ResolvedProgram* out) {
  if (!out || !out->program) return;
  for (const auto& decl : out->program->decls) {
    if (decl.kind == DeclKind::Function) {
      ResolveFunctionMemberRefs(out, decl.func, {});
    } else if (decl.kind == DeclKind::Artifact) {
      for (const auto& method : decl.artifact.methods) {
        ResolveFunctionMemberRefs(out, method, decl.artifact.name);
      }
    } else if (decl.kind == DeclKind::Module) {
      for (const auto& fn : decl.module.functions) ResolveFunctionMemberRefs(out, fn, {});
      for (const auto& var : decl.module.variables) {
        if (var.has_init_expr) ResolveExprMemberRefs(out, var.init_expr, {});
      }
    } else if (decl.kind == DeclKind::Variable && decl.var.has_init_expr) {
      ResolveExprMemberRefs(out, decl.var.init_expr, {});
    }
  }
  for (const auto& stmt : out->program->top_level_stmts) ResolveStmtMemberRefs(out, stmt, {});
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
