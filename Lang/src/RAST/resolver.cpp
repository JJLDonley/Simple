#include "RAST/resolver.h"

#include "lang_reserved.h"

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

bool IsReservedModuleFunction(const std::string& canonical_module, const std::string& member) {
  if (canonical_module == "IO") {
    return member == "print" || member == "println" || member == "buffer_new" ||
           member == "buffer_len" || member == "buffer_fill" || member == "buffer_copy";
  }
  if (canonical_module == "Math") {
    return member == "abs" || member == "min" || member == "max" || member == "sqrt";
  }
  if (canonical_module == "Time") return member == "mono_ns" || member == "wall_ns" || member == "formatWallNs";
  if (canonical_module == "DL") {
    return member == "open" || member == "sym" || member == "close" ||
           member == "last_error" || member == "call_i32" || member == "call_i64" ||
           member == "call_f32" || member == "call_f64" || member == "call_str0";
  }
  if (canonical_module == "OS") {
    return member == "args_count" || member == "args_get" || member == "env_get" ||
           member == "cwd_get" || member == "time_mono_ns" || member == "time_wall_ns" ||
           member == "formatWallNs" ||
           member == "sleep_ms";
  }
  if (canonical_module == "Thread") {
    return member == "sleep" || member == "yield" || member == "hardwareConcurrency";
  }
  if (canonical_module == "Random") {
    return member == "seed" || member == "i32" || member == "range" || member == "f64";
  }
  if (canonical_module == "Env") {
    return member == "argsCount" || member == "arg" || member == "get" || member == "set" ||
           member == "platform" || member == "arch" || member == "exePath";
  }
  if (canonical_module == "Path") {
    return member == "join" || member == "dirname" || member == "basename" || member == "ext" ||
           member == "normalize" || member == "exists" || member == "isFile" || member == "isDir";
  }
  if (canonical_module == "FS") {
    return member == "readText" || member == "writeText" || member == "readBytes" || member == "writeBytes" ||
           member == "copy" || member == "remove" || member == "mkdir" || member == "mkdirAll" ||
           member == "listDir" || member == "cwd" || member == "setCwd";
  }
  if (canonical_module == "Channel") {
    return member == "newI32" || member == "sendI32" || member == "trySendI32" || member == "recvI32" || member == "tryRecvI32" ||
           member == "newI64" || member == "sendI64" || member == "trySendI64" || member == "recvI64" || member == "tryRecvI64" ||
           member == "newF32" || member == "sendF32" || member == "trySendF32" || member == "recvF32" || member == "tryRecvF32" ||
           member == "newF64" || member == "sendF64" || member == "trySendF64" || member == "recvF64" || member == "tryRecvF64" ||
           member == "newBool" || member == "sendBool" || member == "trySendBool" || member == "recvBool" || member == "tryRecvBool" ||
           member == "newString" || member == "sendString" || member == "trySendString" || member == "recvString" || member == "tryRecvString" ||
           member == "newBytes" || member == "sendBytes" || member == "trySendBytes" || member == "recvBytes" || member == "tryRecvBytes" ||
           member == "close";
  }
  if (canonical_module == "File") return member == "open" || member == "close" || member == "read" || member == "write";
  if (canonical_module == "Log") return member == "log" || member == "info" || member == "warn" || member == "error" || member == "setLevel";
  return false;
}

bool ResolveReservedImportAlias(const Program* program, const std::string& alias, std::string* canonical) {
  if (!program) return false;
  for (const auto& decl : program->decls) {
    if (decl.kind != DeclKind::Import) continue;
    std::string resolved;
    if (!Simple::Lang::CanonicalizeReservedImportPath(decl.import_decl.path, &resolved)) continue;
    const std::string import_alias = decl.import_decl.has_alias
        ? decl.import_decl.alias
        : Simple::Lang::DefaultImportAlias(resolved);
    if (import_alias == alias) {
      if (canonical) *canonical = resolved;
      return true;
    }
  }
  return false;
}

std::string NormalizeCoreDlMember(const std::string& name) {
  if (name == "Open") return "open";
  if (name == "Sym") return "sym";
  if (name == "Close") return "close";
  if (name == "LastError") return "last_error";
  if (name == "CallI32") return "call_i32";
  if (name == "CallI64") return "call_i64";
  if (name == "CallF32") return "call_f32";
  if (name == "CallF64") return "call_f64";
  if (name == "CallStr0") return "call_str0";
  return name;
}

bool GetModuleNameFromExpr(const Expr& base, std::string* out) {
  if (!out) return false;
  if (base.kind == ExprKind::Identifier) {
    *out = base.text;
    return true;
  }
  if (base.kind == ExprKind::Member && base.op == "." && !base.children.empty()) {
    const Expr& root = base.children[0];
    if (root.kind == ExprKind::Identifier && (root.text == "Core" || root.text == "System")) {
      *out = root.text + "." + base.text;
      return true;
    }
  }
  return false;
}

bool GetDlOpenManifestModule(const ResolvedProgram* out, const Expr& expr, std::string* out_module) {
  if (!out || !out_module || expr.kind != ExprKind::Call || expr.children.empty()) return false;
  const Expr& callee = expr.children[0];
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  if (NormalizeCoreDlMember(callee.text) != "open") return false;
  std::string module_alias;
  if (!GetModuleNameFromExpr(callee.children[0], &module_alias)) return false;
  std::string canonical;
  if (!ResolveReservedImportAlias(out->program, module_alias, &canonical) || canonical != "DL") return false;
  if (expr.args.size() != 2 || expr.args[1].kind != ExprKind::Identifier) return false;
  const std::string& manifest_module = expr.args[1].text;
  bool has_extern = false;
  const std::string prefix = manifest_module + ".";
  for (const auto& entry : out->by_qualified_name) {
    if (entry.first.rfind(prefix, 0) == 0 && out->symbols[entry.second].kind == SymbolKind::Extern) {
      has_extern = true;
      break;
    }
  }
  if (!has_extern) return false;
  *out_module = manifest_module;
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
  if (!out || name.empty()) return kInvalidSymbolId;
  auto it = out->by_qualified_name.find(name);
  if (it == out->by_qualified_name.end()) return kInvalidSymbolId;
  if (out->symbols[it->second].kind != SymbolKind::Artifact) return kInvalidSymbolId;
  return it->second;
}

void AddResolvedMemberRef(ResolvedProgram* out,
                          MemberRefKind kind,
                          const std::string& base,
                          const std::string& member,
                          const std::string& qualified_name,
                          SymbolId symbol,
                          const std::string& receiver_type = {},
                          SymbolId receiver_symbol = kInvalidSymbolId) {
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
      if (!branch.is_default) ResolveExprMemberRefs(out, branch.condition, current_artifact, types);
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
