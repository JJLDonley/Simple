#include "RAST/member_resolution.h"

#include <algorithm>
#include <utility>

#include "RAST/import_graph.h"
#include "RAST/reserved_resolution.h"
#include "RAST/symbol_table.h"

namespace Simple::Lang::RAST {

namespace {

size_t EditDistance(const std::string& a, const std::string& b) {
  std::vector<size_t> prev(b.size() + 1);
  std::vector<size_t> cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(cur);
  }
  return prev[b.size()];
}

} // namespace

std::vector<std::string> ModuleMembers(const ModuleDecl* module) {
  std::vector<std::string> out;
  if (!module) return out;
  out.reserve(module->variables.size() + module->functions.size());
  for (const auto& var : module->variables) out.push_back(var.name);
  for (const auto& fn : module->functions) out.push_back(fn.name);
  return out;
}

std::string UnknownMemberErrorWithSuggestion(const std::string& module_name,
                                             const std::string& member,
                                             const std::vector<std::string>& candidates) {
  std::string out = "unknown module member: " + module_name + "." + member;
  if (candidates.empty()) return out;
  size_t best_dist = static_cast<size_t>(-1);
  std::string best;
  for (const auto& candidate : candidates) {
    const size_t distance = EditDistance(member, candidate);
    if (distance < best_dist) {
      best_dist = distance;
      best = candidate;
    }
  }
  if (!best.empty() && best_dist <= 3) {
    out += " (did you mean '" + best + "'?)";
  }
  return out;
}

const VarDecl* FindModuleVar(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& var : module->variables) {
    if (var.name == name) return &var;
  }
  return nullptr;
}

const FuncDecl* FindModuleFunc(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& fn : module->functions) {
    if (fn.name == name) return &fn;
  }
  return nullptr;
}

const VarDecl* FindArtifactField(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return nullptr;
  for (const auto& field : artifact->fields) {
    if (field.name == name) return &field;
  }
  return nullptr;
}

const FuncDecl* FindArtifactMethod(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return nullptr;
  for (const auto& method : artifact->methods) {
    if (method.name == name) return &method;
  }
  return nullptr;
}

bool IsArtifactMemberName(const ArtifactDecl* artifact, const std::string& name) {
  return FindArtifactField(artifact, name) || FindArtifactMethod(artifact, name);
}

MemberRefKind ClassifyMemberRefKind(MemberRefKind fallback, SymbolKind symbol_kind) {
  switch (symbol_kind) {
    case SymbolKind::ModuleVariable:
    case SymbolKind::ModuleFunction:
      return MemberRefKind::ModuleMember;
    case SymbolKind::ArtifactField:
      return MemberRefKind::ArtifactField;
    case SymbolKind::ArtifactMethod:
      return MemberRefKind::ArtifactMethod;
    case SymbolKind::EnumMember:
      return MemberRefKind::EnumMember;
    case SymbolKind::Extern:
      return MemberRefKind::ExternSymbol;
    default:
      return fallback;
  }
}

SymbolId FindArtifactSymbol(const ResolvedProgram* out, const std::string& name) {
  return FindQualifiedSymbol(out, name, SymbolKind::Artifact);
}

void AddResolvedMemberRef(ResolvedProgram* out,
                          MemberRefKind kind,
                          const std::string& base,
                          const std::string& member,
                          const std::string& qualified_name,
                          SymbolId symbol,
                          const std::string& receiver_type,
                          SymbolId receiver_symbol) {
  MemberRef ref;
  ref.kind = kind;
  ref.base = base;
  ref.member = member;
  ref.qualified_name = qualified_name;
  ref.symbol = symbol;
  ref.receiver_type = receiver_type;
  ref.receiver_symbol = receiver_symbol;
  out->member_refs.push_back(std::move(ref));
}

const MemberRef* LookupResolvedMemberRef(const ResolvedProgram* program,
                                         const std::string& base,
                                         const std::string& member) {
  return ResolveMemberAccess(program, base, member);
}

const MemberRef* ResolveMemberAccess(const ResolvedProgram* program,
                                     const std::string& base,
                                     const std::string& member) {
  if (!program) return nullptr;
  for (const auto& ref : program->member_refs) {
    if (ref.base == base && ref.member == member) return &ref;
  }
  return nullptr;
}

struct TypeEnv {
  std::unordered_map<std::string, std::string> types;
  std::unordered_map<std::string, std::string> dl_modules;
};

void ResolveExprMemberRefs(ResolvedProgram* out,
                           const Expr& expr,
                           const std::string& current_artifact,
                           TypeEnv& types);

void ResolveStmtBlockMemberRefs(ResolvedProgram* out,
                                const std::vector<Stmt>& stmts,
                                const std::string& current_artifact,
                                TypeEnv types);

void ResolveStmtMemberRefs(ResolvedProgram* out,
                           const Stmt& stmt,
                           const std::string& current_artifact,
                           TypeEnv& types) {
  if (stmt.kind == StmtKind::VarDecl) {
    if (stmt.var_decl.has_init_expr) ResolveExprMemberRefs(out, stmt.var_decl.init_expr, current_artifact, types);
    if (!stmt.var_decl.type.name.empty()) types.types[stmt.var_decl.name] = stmt.var_decl.type.name;
    std::string manifest_module;
    if (stmt.var_decl.has_init_expr && GetDlOpenManifestModule(out, stmt.var_decl.init_expr, &manifest_module)) {
      types.dl_modules[stmt.var_decl.name] = manifest_module;
    }
  } else if (stmt.kind == StmtKind::Assign) {
    ResolveExprMemberRefs(out, stmt.target, current_artifact, types);
    ResolveExprMemberRefs(out, stmt.expr, current_artifact, types);
  } else if (stmt.kind == StmtKind::Expr || (stmt.kind == StmtKind::Return && stmt.has_return_expr)) {
    ResolveExprMemberRefs(out, stmt.expr, current_artifact, types);
  } else if (stmt.kind == StmtKind::IfChain) {
    for (const auto& branch : stmt.if_branches) {
      ResolveExprMemberRefs(out, branch.first, current_artifact, types);
      ResolveStmtBlockMemberRefs(out, branch.second, current_artifact, types);
    }
    ResolveStmtBlockMemberRefs(out, stmt.else_branch, current_artifact, types);
  } else if (stmt.kind == StmtKind::IfStmt) {
    ResolveExprMemberRefs(out, stmt.if_cond, current_artifact, types);
    ResolveStmtBlockMemberRefs(out, stmt.if_then, current_artifact, types);
    ResolveStmtBlockMemberRefs(out, stmt.if_else, current_artifact, types);
  } else if (stmt.kind == StmtKind::WhileLoop) {
    ResolveExprMemberRefs(out, stmt.loop_cond, current_artifact, types);
    ResolveStmtBlockMemberRefs(out, stmt.loop_body, current_artifact, types);
  } else if (stmt.kind == StmtKind::ForLoop) {
    TypeEnv for_types = types;
    if (stmt.has_loop_var_decl && !stmt.loop_var_decl.type.name.empty()) {
      for_types.types[stmt.loop_var_decl.name] = stmt.loop_var_decl.type.name;
    }
    ResolveExprMemberRefs(out, stmt.loop_iter, current_artifact, for_types);
    ResolveExprMemberRefs(out, stmt.loop_cond, current_artifact, for_types);
    ResolveExprMemberRefs(out, stmt.loop_step, current_artifact, for_types);
    ResolveStmtBlockMemberRefs(out, stmt.loop_body, current_artifact, for_types);
  }
}

void ResolveExprMemberRefs(ResolvedProgram* out,
                           const Expr& expr,
                           const std::string& current_artifact,
                           TypeEnv& types) {
  if (expr.kind == ExprKind::Member && expr.op == "." && !expr.children.empty()) {
    const Expr& base = expr.children[0];
    std::string qualified;
    std::string receiver_type;
    SymbolId receiver_symbol = kInvalidSymbolId;
    MemberRefKind kind = MemberRefKind::Unknown;
    if (base.kind == ExprKind::Identifier && base.text == "self" && !current_artifact.empty()) {
      qualified = current_artifact + "." + expr.text;
      receiver_type = current_artifact;
      receiver_symbol = FindArtifactSymbol(out, receiver_type);
      kind = MemberRefKind::SelfMember;
    } else if (base.kind == ExprKind::Identifier) {
      auto dl_it = types.dl_modules.find(base.text);
      if (dl_it != types.dl_modules.end()) {
        qualified = dl_it->second + "." + expr.text;
        kind = MemberRefKind::DLManifestCall;
      } else {
        auto type_it = types.types.find(base.text);
        if (type_it != types.types.end()) {
          qualified = type_it->second + "." + expr.text;
          receiver_type = type_it->second;
          receiver_symbol = FindArtifactSymbol(out, receiver_type);
          kind = MemberRefKind::ArtifactMember;
        } else {
          qualified = base.text + "." + expr.text;
          kind = MemberRefKind::StaticMember;
        }
      }
    }
    std::string reserved_module_name;
    if (GetModuleNameFromExpr(base, &reserved_module_name)) {
      std::string reserved_module;
      if (!ResolveReservedImportAlias(out->program, reserved_module_name, &reserved_module)) {
        for (const auto& import : ResolveReservedImports(out->program)) {
          if (import.canonical_module == reserved_module_name) {
            reserved_module = import.canonical_module;
            break;
          }
        }
      }
      if (!reserved_module.empty() && IsReservedModuleFunction(reserved_module, expr.text)) {
        AddResolvedMemberRef(out,
                             MemberRefKind::ReservedModuleFunction,
                             reserved_module_name,
                             expr.text,
                             reserved_module + "." + expr.text,
                             kInvalidSymbolId);
      }
    }
    auto it = out->by_qualified_name.find(qualified);
    if (it != out->by_qualified_name.end()) {
      if (kind != MemberRefKind::DLManifestCall) {
        kind = ClassifyMemberRefKind(kind, out->symbols[it->second].kind);
      }
      AddResolvedMemberRef(out,
                           kind,
                           base.text,
                           expr.text,
                           qualified,
                           it->second,
                           receiver_type,
                           receiver_symbol);
    } else if (base.kind == ExprKind::Identifier) {
      std::string reserved_module;
      if (ResolveReservedImportAlias(out->program, base.text, &reserved_module) &&
          IsReservedModuleFunction(reserved_module, expr.text)) {
        AddResolvedMemberRef(out,
                             MemberRefKind::ReservedModuleFunction,
                             base.text,
                             expr.text,
                             reserved_module + "." + expr.text,
                             kInvalidSymbolId);
      }
    }
  }
  for (const auto& child : expr.children) ResolveExprMemberRefs(out, child, current_artifact, types);
  for (const auto& arg : expr.args) ResolveExprMemberRefs(out, arg, current_artifact, types);
  for (const auto& value : expr.field_values) ResolveExprMemberRefs(out, value, current_artifact, types);
  if (expr.kind == ExprKind::Switch) {
    for (const auto& branch : expr.switch_branches) {
      if (!branch.is_default && branch.pattern_kind == SwitchPatternKind::None) {
        ResolveExprMemberRefs(out, branch.condition, current_artifact, types);
      }
      if (branch.has_inline_value) ResolveExprMemberRefs(out, branch.value, current_artifact, types);
      ResolveStmtBlockMemberRefs(out, branch.block, current_artifact, types);
    }
  }
}

void ResolveStmtBlockMemberRefs(ResolvedProgram* out,
                                const std::vector<Stmt>& stmts,
                                const std::string& current_artifact,
                                TypeEnv types) {
  for (const auto& stmt : stmts) ResolveStmtMemberRefs(out, stmt, current_artifact, types);
}

void ResolveFunctionMemberRefs(ResolvedProgram* out,
                               const FuncDecl& fn,
                               const std::string& current_artifact,
                               const TypeEnv& globals) {
  TypeEnv types = globals;
  for (const auto& param : fn.params) {
    if (!param.type.name.empty()) types.types[param.name] = param.type.name;
  }
  ResolveStmtBlockMemberRefs(out, fn.body, current_artifact, std::move(types));
}

TypeEnv CollectGlobalMemberEnv(ResolvedProgram* out) {
  TypeEnv globals;
  if (!out || !out->program) return globals;
  for (const auto& decl : out->program->decls) {
    if (decl.kind == DeclKind::Variable) {
      if (!decl.var.type.name.empty()) globals.types[decl.var.name] = decl.var.type.name;
      std::string manifest_module;
      if (decl.var.has_init_expr && GetDlOpenManifestModule(out, decl.var.init_expr, &manifest_module)) {
        globals.dl_modules[decl.var.name] = manifest_module;
      }
    } else if (decl.kind == DeclKind::Module) {
      for (const auto& var : decl.module.variables) {
        if (!var.type.name.empty()) globals.types[decl.module.name + "." + var.name] = var.type.name;
      }
    }
  }
  return globals;
}

void ResolveProgramMemberRefs(ResolvedProgram* out) {
  if (!out || !out->program) return;
  TypeEnv globals = CollectGlobalMemberEnv(out);
  for (const auto& decl : out->program->decls) {
    if (decl.kind == DeclKind::Function) {
      ResolveFunctionMemberRefs(out, decl.func, {}, globals);
    } else if (decl.kind == DeclKind::Artifact) {
      for (const auto& method : decl.artifact.methods) {
        ResolveFunctionMemberRefs(out, method, decl.artifact.name, globals);
      }
    } else if (decl.kind == DeclKind::Module) {
      for (const auto& fn : decl.module.functions) ResolveFunctionMemberRefs(out, fn, {}, globals);
      for (const auto& var : decl.module.variables) {
        TypeEnv types = globals;
        if (var.has_init_expr) ResolveExprMemberRefs(out, var.init_expr, {}, types);
      }
    } else if (decl.kind == DeclKind::Variable && decl.var.has_init_expr) {
      TypeEnv types = globals;
      ResolveExprMemberRefs(out, decl.var.init_expr, {}, types);
    }
  }
  TypeEnv script_types = globals;
  for (const auto& stmt : out->program->top_level_stmts) ResolveStmtMemberRefs(out, stmt, {}, script_types);
}


} // namespace Simple::Lang::RAST
