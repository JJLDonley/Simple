#include "lang_validate.h"

#include <unordered_map>
#include <unordered_set>

#include "lang_parser.h"

namespace Simple::Lang {
namespace {

struct ValidateContext {
  std::unordered_set<std::string> enum_members;
  std::unordered_set<std::string> enum_types;
  std::unordered_set<std::string> top_level;
  std::unordered_map<std::string, const ArtifactDecl*> artifacts;
  std::unordered_map<std::string, size_t> artifact_generics;
  std::unordered_map<std::string, const ModuleDecl*> modules;
  std::unordered_map<std::string, const VarDecl*> globals;
  std::unordered_map<std::string, const FuncDecl*> functions;
};

struct LocalInfo {
  Mutability mutability = Mutability::Mutable;
  const TypeRef* type = nullptr;
};

enum class TypeUse : uint8_t {
  Value,
  Return,
};

const std::unordered_set<std::string> kPrimitiveTypes = {
  "i8", "i16", "i32", "i64", "i128",
  "u8", "u16", "u32", "u64", "u128",
  "f32", "f64",
  "bool", "char", "string",
};

bool CloneTypeRef(const TypeRef& src, TypeRef* out) {
  if (!out) return false;
  out->name = src.name;
  out->type_args.clear();
  out->dims = src.dims;
  out->is_proc = src.is_proc;
  out->proc_return_mutability = src.proc_return_mutability;
  out->proc_params.clear();
  out->proc_return.reset();
  for (const auto& arg : src.type_args) {
    TypeRef copy;
    if (!CloneTypeRef(arg, &copy)) return false;
    out->type_args.push_back(std::move(copy));
  }
  for (const auto& param : src.proc_params) {
    TypeRef copy;
    if (!CloneTypeRef(param, &copy)) return false;
    out->proc_params.push_back(std::move(copy));
  }
  if (src.proc_return) {
    TypeRef copy;
    if (!CloneTypeRef(*src.proc_return, &copy)) return false;
    out->proc_return = std::make_unique<TypeRef>(std::move(copy));
  }
  return true;
}

bool TypeDimsEqual(const std::vector<TypeDim>& a, const std::vector<TypeDim>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].is_list != b[i].is_list) return false;
    if (a[i].has_size != b[i].has_size) return false;
    if (a[i].has_size && a[i].size != b[i].size) return false;
  }
  return true;
}

bool TypeArgsEqual(const std::vector<TypeRef>& a, const std::vector<TypeRef>& b);

bool TypeEquals(const TypeRef& a, const TypeRef& b) {
  if (a.is_proc != b.is_proc) return false;
  if (a.is_proc) {
    if (a.proc_return_mutability != b.proc_return_mutability) return false;
    if (a.proc_params.size() != b.proc_params.size()) return false;
    for (size_t i = 0; i < a.proc_params.size(); ++i) {
      if (!TypeEquals(a.proc_params[i], b.proc_params[i])) return false;
    }
    if (!a.proc_return || !b.proc_return) return false;
    if (!TypeEquals(*a.proc_return, *b.proc_return)) return false;
  } else {
    if (a.name != b.name) return false;
    if (!TypeArgsEqual(a.type_args, b.type_args)) return false;
    if (!TypeDimsEqual(a.dims, b.dims)) return false;
  }
  return true;
}

bool TypeArgsEqual(const std::vector<TypeRef>& a, const std::vector<TypeRef>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!TypeEquals(a[i], b[i])) return false;
  }
  return true;
}

bool CheckTypeRef(const TypeRef& type,
                  const ValidateContext& ctx,
                  const std::unordered_set<std::string>& type_params,
                  TypeUse use,
                  std::string* error) {
  if (type.is_proc) {
    for (const auto& param : type.proc_params) {
      if (!CheckTypeRef(param, ctx, type_params, TypeUse::Value, error)) return false;
    }
    if (!type.proc_return) {
      if (error) *error = "procedure type missing return type";
      return false;
    }
    return CheckTypeRef(*type.proc_return, ctx, type_params, TypeUse::Return, error);
  }

  if (type.name == "void") {
    if (use != TypeUse::Return) {
      if (error) *error = "void is only valid as a return type";
      return false;
    }
    if (!type.type_args.empty()) {
      if (error) *error = "void cannot have type arguments";
      return false;
    }
    return true;
  }

  const bool is_primitive = kPrimitiveTypes.find(type.name) != kPrimitiveTypes.end();
  const bool is_type_param = type_params.find(type.name) != type_params.end();
  const bool is_user_type = ctx.top_level.find(type.name) != ctx.top_level.end();

  if (!is_primitive && !is_type_param && !is_user_type) {
    if (error) *error = "unknown type: " + type.name;
    return false;
  }

  if (is_user_type && !is_type_param) {
    if (ctx.modules.find(type.name) != ctx.modules.end()) {
      if (error) *error = "module is not a type: " + type.name;
      return false;
    }
    if (ctx.functions.find(type.name) != ctx.functions.end()) {
      if (error) *error = "function is not a type: " + type.name;
      return false;
    }
    if (ctx.enum_types.find(type.name) != ctx.enum_types.end()) {
      if (!type.type_args.empty()) {
        if (error) *error = "enum type cannot have type arguments: " + type.name;
        return false;
      }
    }
    auto art_it = ctx.artifact_generics.find(type.name);
    if (art_it != ctx.artifact_generics.end()) {
      const size_t expected = art_it->second;
      if (type.type_args.size() != expected) {
        if (error) {
          *error = "generic type argument count mismatch for " + type.name;
        }
        return false;
      }
    }
  }

  if (!type.type_args.empty()) {
    if (is_primitive) {
      if (error) *error = "primitive type cannot have type arguments: " + type.name;
      return false;
    }
    if (is_type_param) {
      if (error) *error = "type parameter cannot have type arguments: " + type.name;
      return false;
    }
    for (const auto& arg : type.type_args) {
      if (!CheckTypeRef(arg, ctx, type_params, TypeUse::Value, error)) return false;
    }
  }

  return true;
}

const LocalInfo* FindLocal(const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const std::string& name);
const VarDecl* FindModuleVar(const ModuleDecl* module, const std::string& name);
const VarDecl* FindArtifactField(const ArtifactDecl* artifact, const std::string& name);
const FuncDecl* FindModuleFunc(const ModuleDecl* module, const std::string& name);
const FuncDecl* FindArtifactMethod(const ArtifactDecl* artifact, const std::string& name);

bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out) {
  if (!out) return false;
  switch (expr.kind) {
    case ExprKind::Literal:
      out->is_proc = false;
      out->type_args.clear();
      out->dims.clear();
      switch (expr.literal_kind) {
        case LiteralKind::Integer: out->name = "i32"; break;
        case LiteralKind::Float: out->name = "f64"; break;
        case LiteralKind::String: out->name = "string"; break;
        case LiteralKind::Char: out->name = "char"; break;
        case LiteralKind::Bool: out->name = "bool"; break;
      }
      return true;
    case ExprKind::Identifier: {
      if (expr.text == "self") return false;
      if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
        if (!local->type) return false;
        return CloneTypeRef(*local->type, out);
      }
      auto global_it = ctx.globals.find(expr.text);
      if (global_it != ctx.globals.end()) {
        return CloneTypeRef(global_it->second->type, out);
      }
      return false;
    }
    case ExprKind::Member: {
      if (expr.op != "." || expr.children.empty()) return false;
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier) {
        if (base.text == "self") {
          const VarDecl* field = FindArtifactField(current_artifact, expr.text);
          if (field) return CloneTypeRef(field->type, out);
          const FuncDecl* method = FindArtifactMethod(current_artifact, expr.text);
          if (method) return CloneTypeRef(method->return_type, out);
          return false;
        }
        auto module_it = ctx.modules.find(base.text);
        if (module_it != ctx.modules.end()) {
          if (const VarDecl* var = FindModuleVar(module_it->second, expr.text)) {
            return CloneTypeRef(var->type, out);
          }
          if (const FuncDecl* fn = FindModuleFunc(module_it->second, expr.text)) {
            return CloneTypeRef(fn->return_type, out);
          }
          return false;
        }
        if (const LocalInfo* local = FindLocal(scopes, base.text)) {
          if (!local->type) return false;
          auto artifact_it = ctx.artifacts.find(local->type->name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            return CloneTypeRef(field->type, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            return CloneTypeRef(method->return_type, out);
          }
        }
        auto global_it = ctx.globals.find(base.text);
        if (global_it != ctx.globals.end()) {
          auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            return CloneTypeRef(field->type, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            return CloneTypeRef(method->return_type, out);
          }
        }
      }
      return false;
    }
    case ExprKind::Call: {
      if (expr.children.empty()) return false;
      const Expr& callee = expr.children[0];
      if (callee.kind == ExprKind::Identifier) {
        if (callee.text == "len") {
          out->name = "i32";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        auto fn_it = ctx.functions.find(callee.text);
        if (fn_it != ctx.functions.end()) {
          return CloneTypeRef(fn_it->second->return_type, out);
        }
      }
      if (callee.kind == ExprKind::Member) {
        TypeRef member_type;
        if (InferExprType(callee, ctx, scopes, current_artifact, &member_type)) {
          if (member_type.is_proc && member_type.proc_return) {
            return CloneTypeRef(*member_type.proc_return, out);
          }
          return CloneTypeRef(member_type, out);
        }
      }
      return false;
    }
    default:
      return false;
  }
}

bool CollectTypeParams(const std::vector<std::string>& generics,
                        std::unordered_set<std::string>* out,
                        std::string* error) {
  if (!out) return false;
  out->clear();
  for (const auto& name : generics) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  return true;
}

bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               const std::unordered_set<std::string>& type_params,
               const TypeRef* expected_return,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error);

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error);

bool CheckArrayLiteralShape(const Expr& expr,
                            const std::vector<TypeDim>& dims,
                            size_t dim_index,
                            std::string* error);

bool CheckArrayLiteralElementTypes(const Expr& expr,
                                   const ValidateContext& ctx,
                                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                   const ArtifactDecl* current_artifact,
                                   const std::vector<TypeDim>& dims,
                                   size_t dim_index,
                                   const TypeRef& element_type,
                                   std::string* error);

bool CheckListLiteralElementTypes(const Expr& expr,
                                  const ValidateContext& ctx,
                                  const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                  const ArtifactDecl* current_artifact,
                                  const TypeRef& list_type,
                                  std::string* error);

bool CheckBoolCondition(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error);

bool StmtReturns(const Stmt& stmt);
bool StmtsReturn(const std::vector<Stmt>& stmts);

const LocalInfo* FindLocal(const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const std::string& name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
}

bool AddLocal(std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
              const std::string& name,
              const LocalInfo& info,
              std::string* error) {
  if (scopes.empty()) scopes.emplace_back();
  auto& current = scopes.back();
  if (!current.emplace(name, info).second) {
    if (error) *error = "duplicate local declaration: " + name;
    return false;
  }
  return true;
}

bool IsAssignOp(const std::string& op) {
  return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
         op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=";
}

const VarDecl* FindModuleVar(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& var : module->variables) {
    if (var.name == name) return &var;
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
  if (!artifact) return false;
  if (FindArtifactField(artifact, name)) return true;
  if (FindArtifactMethod(artifact, name)) return true;
  return false;
}

const FuncDecl* FindModuleFunc(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& fn : module->functions) {
    if (fn.name == name) return &fn;
  }
  return nullptr;
}

bool CheckCallArgs(const FuncDecl* fn, size_t arg_count, std::string* error) {
  if (!fn) return false;
  if (fn->params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch for " + fn->name + ": expected " +
               std::to_string(fn->params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckProcTypeArgs(const TypeRef* type, size_t arg_count, std::string* error) {
  if (!type || !type->is_proc) return false;
  if (type->proc_params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch: expected " +
               std::to_string(type->proc_params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckCallTarget(const Expr& callee,
                     size_t arg_count,
                     const ValidateContext& ctx,
                     const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                     const ArtifactDecl* current_artifact,
                     std::string* error) {
  if (callee.kind == ExprKind::FnLiteral) {
    if (callee.fn_params.size() != arg_count) {
      if (error) {
        *error = "call argument count mismatch for fn literal: expected " +
                 std::to_string(callee.fn_params.size()) + ", got " + std::to_string(arg_count);
      }
      return false;
    }
    return true;
  }
  if (callee.kind == ExprKind::Identifier) {
    auto fn_it = ctx.functions.find(callee.text);
    if (fn_it != ctx.functions.end()) {
      return CheckCallArgs(fn_it->second, arg_count, error);
    }
    if (const LocalInfo* local = FindLocal(scopes, callee.text)) {
      if (local->type && local->type->is_proc) {
        return CheckProcTypeArgs(local->type, arg_count, error);
      }
      if (error) *error = "attempt to call non-function: " + callee.text;
      return false;
    }
    auto global_it = ctx.globals.find(callee.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->type.is_proc) {
        return CheckProcTypeArgs(&global_it->second->type, arg_count, error);
      }
      if (error) *error = "attempt to call non-function: " + callee.text;
      return false;
    }
    return true;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "self") {
        const FuncDecl* method = FindArtifactMethod(current_artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (FindArtifactField(current_artifact, callee.text)) {
          if (error) *error = "attempt to call non-function: self." + callee.text;
          return false;
        }
        return true;
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const FuncDecl* fn = FindModuleFunc(module_it->second, callee.text);
        if (fn) return CheckCallArgs(fn, arg_count, error);
        if (FindModuleVar(module_it->second, callee.text)) {
          const VarDecl* var = FindModuleVar(module_it->second, callee.text);
          if (var && var->type.is_proc) {
            return CheckProcTypeArgs(&var->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (const VarDecl* field = FindArtifactField(artifact, callee.text)) {
          if (field->type.is_proc) {
            return CheckProcTypeArgs(&field->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
        return true;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (const VarDecl* field = FindArtifactField(artifact, callee.text)) {
          if (field->type.is_proc) {
            return CheckProcTypeArgs(&field->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
      }
    }
  }
  return true;
}

bool CheckAssignmentTarget(const Expr& target,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           std::string* error) {
  if (target.kind == ExprKind::Identifier) {
    if (target.text == "self") {
      if (error) *error = "cannot assign to self";
      return false;
    }
    if (const LocalInfo* local = FindLocal(scopes, target.text)) {
      if (local->mutability == Mutability::Immutable) {
        if (error) *error = "cannot assign to immutable local: " + target.text;
        return false;
      }
      return true;
    }
    auto global_it = ctx.globals.find(target.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->mutability == Mutability::Immutable) {
        if (error) *error = "cannot assign to immutable variable: " + target.text;
        return false;
      }
      return true;
    }
    return true;
  }
  if (target.kind == ExprKind::Member && target.op == "." && !target.children.empty()) {
    const Expr& base = target.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "self") {
        const VarDecl* field = FindArtifactField(current_artifact, target.text);
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: self." + target.text;
          return false;
        }
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const VarDecl* field = FindArtifactField(
          artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second,
          target.text);
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const VarDecl* field = FindModuleVar(module_it->second, target.text);
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable module member: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const VarDecl* field = FindArtifactField(
          artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second,
          target.text);
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
      }
    }
    return true;
  }
  return true;
}

bool ValidateArtifactLiteral(const Expr& expr,
                             const ArtifactDecl* artifact,
                             const ValidateContext& ctx,
                             const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                             const ArtifactDecl* current_artifact,
                             std::string* error) {
  if (!artifact) return true;
  const size_t field_count = artifact->fields.size();
  if (expr.children.size() > field_count) {
    if (error) *error = "too many positional values in artifact literal";
    return false;
  }
  std::unordered_set<std::string> seen;
  for (const auto& name : expr.field_names) {
    if (!seen.insert(name).second) {
      if (error) *error = "duplicate named field in artifact literal: " + name;
      return false;
    }
  }
  for (size_t i = 0; i < expr.children.size(); ++i) {
    if (i >= field_count) break;
    const auto& field = artifact->fields[i];
    if (seen.find(field.name) != seen.end()) {
      if (error) *error = "field specified twice in artifact literal: " + field.name;
      return false;
    }
    seen.insert(field.name);
    TypeRef value_type;
    if (InferExprType(expr.children[i], ctx, scopes, current_artifact, &value_type)) {
      if (!TypeEquals(field.type, value_type)) {
        if (error) *error = "artifact field type mismatch: " + field.name;
        return false;
      }
    }
  }
  if (!expr.field_names.empty()) {
    std::unordered_set<std::string> valid;
    std::unordered_map<std::string, const VarDecl*> field_map;
    for (const auto& field : artifact->fields) {
      valid.insert(field.name);
      field_map[field.name] = &field;
    }
    for (const auto& name : expr.field_names) {
      if (valid.find(name) == valid.end()) {
        if (error) *error = "unknown artifact field: " + name;
        return false;
      }
    }
    for (size_t i = 0; i < expr.field_names.size(); ++i) {
      const auto& name = expr.field_names[i];
      auto it = field_map.find(name);
      if (it == field_map.end()) continue;
      TypeRef value_type;
      if (InferExprType(expr.field_values[i], ctx, scopes, current_artifact, &value_type)) {
        if (!TypeEquals(it->second->type, value_type)) {
          if (error) *error = "artifact field type mismatch: " + name;
          return false;
        }
      }
    }
  }
  return true;
}

bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               const std::unordered_set<std::string>& type_params,
               const TypeRef* expected_return,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error) {
  switch (stmt.kind) {
    case StmtKind::Return:
      if (return_is_void && stmt.has_return_expr) {
        if (error) *error = "void function cannot return a value";
        return false;
      }
      if (!return_is_void && !stmt.has_return_expr) {
        if (error) *error = "non-void function must return a value";
        return false;
      }
      if (stmt.has_return_expr) {
        if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
        if (expected_return) {
          TypeRef actual;
          if (InferExprType(stmt.expr, ctx, scopes, current_artifact, &actual)) {
            if (!TypeEquals(*expected_return, actual)) {
              if (error) *error = "return type mismatch";
              return false;
            }
          }
        }
        return true;
      }
      return true;
    case StmtKind::Expr:
      return CheckExpr(stmt.expr, ctx, scopes, current_artifact, error);
    case StmtKind::Assign:
      if (!CheckExpr(stmt.target, ctx, scopes, current_artifact, error)) return false;
      if (!CheckAssignmentTarget(stmt.target, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
      {
        TypeRef target_type;
        TypeRef value_type;
        bool have_target = InferExprType(stmt.target, ctx, scopes, current_artifact, &target_type);
        bool have_value = InferExprType(stmt.expr, ctx, scopes, current_artifact, &value_type);
        if (have_target && have_value && !TypeEquals(target_type, value_type)) {
          if (error) *error = "assignment type mismatch";
          return false;
        }
        if (have_target &&
            (stmt.expr.kind == ExprKind::ArrayLiteral || stmt.expr.kind == ExprKind::ListLiteral) &&
            !target_type.dims.empty()) {
          if (!CheckArrayLiteralShape(stmt.expr, target_type.dims, 0, error)) return false;
          TypeRef base_type;
          if (!CloneTypeRef(target_type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(stmt.expr,
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             target_type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(stmt.expr,
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            target_type,
                                            error)) {
            return false;
          }
        } else if (have_target &&
                   (stmt.expr.kind == ExprKind::ArrayLiteral || stmt.expr.kind == ExprKind::ListLiteral)) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
      }
      return true;
    case StmtKind::VarDecl:
      if (!CheckTypeRef(stmt.var_decl.type, ctx, type_params, TypeUse::Value, error)) return false;
      {
        LocalInfo info;
        info.mutability = stmt.var_decl.mutability;
        info.type = &stmt.var_decl.type;
        if (!AddLocal(scopes, stmt.var_decl.name, info, error)) return false;
      }
      if (stmt.var_decl.has_init_expr) {
        if (!CheckExpr(stmt.var_decl.init_expr, ctx, scopes, current_artifact, error)) return false;
        if ((stmt.var_decl.init_expr.kind == ExprKind::ArrayLiteral ||
             stmt.var_decl.init_expr.kind == ExprKind::ListLiteral) &&
            !stmt.var_decl.type.dims.empty()) {
          if (!CheckArrayLiteralShape(stmt.var_decl.init_expr, stmt.var_decl.type.dims, 0, error)) {
            return false;
          }
          TypeRef base_type;
          if (!CloneTypeRef(stmt.var_decl.type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(stmt.var_decl.init_expr,
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             stmt.var_decl.type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(stmt.var_decl.init_expr,
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            stmt.var_decl.type,
                                            error)) {
            return false;
          }
        } else if (stmt.var_decl.init_expr.kind == ExprKind::ArrayLiteral ||
                   stmt.var_decl.init_expr.kind == ExprKind::ListLiteral) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
        TypeRef init_type;
        if (InferExprType(stmt.var_decl.init_expr, ctx, scopes, current_artifact, &init_type)) {
          if (!TypeEquals(stmt.var_decl.type, init_type)) {
            if (error) *error = "initializer type mismatch";
            return false;
          }
        }
        if (stmt.var_decl.init_expr.kind == ExprKind::ArtifactLiteral) {
          auto artifact_it = ctx.artifacts.find(stmt.var_decl.type.name);
          if (artifact_it != ctx.artifacts.end()) {
            if (!ValidateArtifactLiteral(stmt.var_decl.init_expr,
                                         artifact_it->second,
                                         ctx,
                                         scopes,
                                         current_artifact,
                                         error)) {
              return false;
            }
          }
        }
        return true;
      }
      return true;
    case StmtKind::IfChain:
      for (const auto& branch : stmt.if_branches) {
        if (!CheckExpr(branch.first, ctx, scopes, current_artifact, error)) return false;
        if (!CheckBoolCondition(branch.first, ctx, scopes, current_artifact, error)) return false;
        scopes.emplace_back();
        for (const auto& child : branch.second) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      if (!stmt.else_branch.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.else_branch) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::IfStmt:
      if (!CheckExpr(stmt.if_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.if_cond, ctx, scopes, current_artifact, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.if_then) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      if (!stmt.if_else.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.if_else) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::WhileLoop:
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth + 1,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      return true;
    case StmtKind::ForLoop: {
      scopes.emplace_back();
      if (!CheckExpr(stmt.loop_iter, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_step, ctx, scopes, current_artifact, error)) return false;
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth + 1,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      return true;
    }
    case StmtKind::Break:
      if (loop_depth == 0) {
        if (error) *error = "break used outside of loop";
        return false;
      }
      return true;
    case StmtKind::Skip:
      if (loop_depth == 0) {
        if (error) *error = "skip used outside of loop";
        return false;
      }
      return true;
  }
  return true;
}

bool IsIntegerTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "i128" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "u128" ||
         name == "char";
}

bool IsFloatTypeName(const std::string& name) {
  return name == "f32" || name == "f64";
}

bool IsBoolTypeName(const std::string& name) {
  return name == "bool";
}

bool IsStringTypeName(const std::string& name) {
  return name == "string";
}

bool IsNumericTypeName(const std::string& name) {
  return IsIntegerTypeName(name) || IsFloatTypeName(name);
}

bool IsScalarType(const TypeRef& type) {
  return !type.is_proc && type.dims.empty() && type.type_args.empty();
}

bool CheckArrayLiteralShape(const Expr& expr,
                            const std::vector<TypeDim>& dims,
                            size_t dim_index,
                            std::string* error) {
  if (dim_index >= dims.size()) return true;
  const TypeDim& dim = dims[dim_index];
  if (!dim.has_size) return true;

  if (expr.kind == ExprKind::ListLiteral) {
    if (dim.size != 0) {
      if (error) *error = "array literal size does not match fixed dimensions";
      return false;
    }
    return true;
  }

  if (expr.kind != ExprKind::ArrayLiteral) {
    if (error) *error = "array literal size does not match fixed dimensions";
    return false;
  }
  if (expr.children.size() != dim.size) {
    if (error) *error = "array literal size does not match fixed dimensions";
    return false;
  }
  if (dim_index + 1 < dims.size()) {
    for (const auto& child : expr.children) {
      if (!CheckArrayLiteralShape(child, dims, dim_index + 1, error)) return false;
    }
  }
  return true;
}

bool CheckArrayLiteralElementTypes(const Expr& expr,
                                   const ValidateContext& ctx,
                                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                   const ArtifactDecl* current_artifact,
                                   const std::vector<TypeDim>& dims,
                                   size_t dim_index,
                                   const TypeRef& element_type,
                                   std::string* error) {
  if (expr.kind == ExprKind::ListLiteral) return true;
  if (expr.kind != ExprKind::ArrayLiteral) return true;
  if (dims.empty()) return true;

  if (dim_index + 1 >= dims.size()) {
    for (const auto& child : expr.children) {
      TypeRef child_type;
      if (!InferExprType(child, ctx, scopes, current_artifact, &child_type)) {
        if (error && error->empty()) *error = "array literal element type mismatch";
        return false;
      }
      if (!TypeEquals(element_type, child_type)) {
        if (error) *error = "array literal element type mismatch";
        return false;
      }
    }
    return true;
  }

  for (const auto& child : expr.children) {
    if (!CheckArrayLiteralElementTypes(child,
                                       ctx,
                                       scopes,
                                       current_artifact,
                                       dims,
                                       dim_index + 1,
                                       element_type,
                                       error)) {
      return false;
    }
  }
  return true;
}

bool CheckListLiteralElementTypes(const Expr& expr,
                                  const ValidateContext& ctx,
                                  const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                  const ArtifactDecl* current_artifact,
                                  const TypeRef& list_type,
                                  std::string* error) {
  if (expr.kind != ExprKind::ListLiteral) return true;
  if (list_type.dims.empty()) return true;
  if (!list_type.dims.front().is_list) return true;

  TypeRef element_type;
  if (!CloneTypeRef(list_type, &element_type)) return false;
  if (!element_type.dims.empty()) {
    element_type.dims.erase(element_type.dims.begin());
  }

  for (const auto& child : expr.children) {
    TypeRef child_type;
    if (!InferExprType(child, ctx, scopes, current_artifact, &child_type)) {
      if (error && error->empty()) *error = "list literal element type mismatch";
      return false;
    }
    if (!TypeEquals(element_type, child_type)) {
      if (error) *error = "list literal element type mismatch";
      return false;
    }
  }
  return true;
}

bool CheckBoolCondition(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error) {
  TypeRef cond_type;
  if (InferExprType(expr, ctx, scopes, current_artifact, &cond_type)) {
    if (!IsBoolTypeName(cond_type.name)) {
      if (error) *error = "condition must be bool";
      return false;
    }
  }
  return true;
}

bool RequireScalar(const TypeRef& type, const std::string& op, std::string* error) {
  if (!IsScalarType(type)) {
    if (error) *error = "operator '" + op + "' requires scalar operands";
    return false;
  }
  return true;
}

bool CheckUnaryOpTypes(const Expr& expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  TypeRef operand;
  if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &operand)) return true;
  if (!RequireScalar(operand, expr.op, error)) return false;

  const std::string op = expr.op.rfind("post", 0) == 0 ? expr.op.substr(4) : expr.op;
  if (op == "!") {
    if (!IsBoolTypeName(operand.name)) {
      if (error) *error = "operator '!' requires bool operand";
      return false;
    }
    return true;
  }
  if (op == "++" || op == "--" || op == "-") {
    if (!IsNumericTypeName(operand.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operand";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckBinaryOpTypes(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error) {
  TypeRef lhs;
  TypeRef rhs;
  if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &lhs)) return true;
  if (!InferExprType(expr.children[1], ctx, scopes, current_artifact, &rhs)) return true;

  if (!RequireScalar(lhs, expr.op, error)) return false;
  if (!RequireScalar(rhs, expr.op, error)) return false;
  if (!TypeEquals(lhs, rhs)) {
    if (error) *error = "operator '" + expr.op + "' requires matching operand types";
    return false;
  }

  const std::string& op = expr.op;
  if (op == "&&" || op == "||") {
    if (!IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires bool operands";
      return false;
    }
    return true;
  }

  if (op == "==" || op == "!=") {
    if (IsStringTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' does not support string operands";
      return false;
    }
    if (!IsNumericTypeName(lhs.name) && !IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric or bool operands";
      return false;
    }
    return true;
  }

  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }

  if (op == "+" || op == "-" || op == "*" || op == "/") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }

  if (op == "%") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '%' requires integer operands";
      return false;
    }
    return true;
  }

  if (op == "<<" || op == ">>" || op == "&" || op == "|" || op == "^") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires integer operands";
      return false;
    }
    return true;
  }

  return true;
}

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error) {
  switch (expr.kind) {
    case ExprKind::Identifier:
      if (expr.text == "self") {
        if (!current_artifact) {
          if (error) *error = "self used outside of artifact method";
          return false;
        }
        return true;
      }
      if (current_artifact && IsArtifactMemberName(current_artifact, expr.text)) {
        if (error) *error = "artifact members must be accessed via self: " + expr.text;
        return false;
      }
      if (expr.text == "len") return true;
      if (FindLocal(scopes, expr.text)) return true;
      if (ctx.top_level.find(expr.text) != ctx.top_level.end()) {
        if (ctx.modules.find(expr.text) != ctx.modules.end()) {
          if (error) *error = "module is not a value: " + expr.text;
          return false;
        }
        if (ctx.artifacts.find(expr.text) != ctx.artifacts.end()) {
          if (error) *error = "type is not a value: " + expr.text;
          return false;
        }
        if (ctx.enum_types.find(expr.text) != ctx.enum_types.end()) {
          if (error) *error = "enum type is not a value: " + expr.text;
          return false;
        }
        return true;
      }
      if (ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
        if (error) *error = "unqualified enum value: " + expr.text;
        return false;
      }
      if (error) *error = "undeclared identifier: " + expr.text;
      return false;
    case ExprKind::Literal:
      return true;
    case ExprKind::Unary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      return CheckUnaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Binary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) {
        if (!CheckAssignmentTarget(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckExpr(expr.children[1], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) return true;
      return CheckBinaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Call:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      for (const auto& arg : expr.args) {
        if (!CheckExpr(arg, ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckCallTarget(expr.children[0], expr.args.size(), ctx, scopes, current_artifact, error)) return false;
      if (expr.children[0].kind == ExprKind::Identifier && expr.children[0].text == "len") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for len: expected 1, got " +
                              std::to_string(expr.args.size());
          return false;
        }
        TypeRef arg_type;
        if (InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (arg_type.dims.empty()) {
            if (error) *error = "len expects array or list argument";
            return false;
          }
        } else {
          if (error && error->empty()) *error = "len expects array or list argument";
          return false;
        }
      }
      return true;
    case ExprKind::Member:
      if (expr.op == "." && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end()) {
          return true;
        }
      }
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (expr.op == "." && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier) {
          auto module_it = ctx.modules.find(base.text);
          if (module_it != ctx.modules.end()) {
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text)) {
              if (error) *error = "unknown module member: " + base.text + "." + expr.text;
              return false;
            }
            return true;
          }
        }
        TypeRef base_type;
        if (InferExprType(base, ctx, scopes, current_artifact, &base_type)) {
          auto artifact_it = ctx.artifacts.find(base_type.name);
          if (artifact_it != ctx.artifacts.end()) {
            const ArtifactDecl* artifact = artifact_it->second;
            if (!FindArtifactField(artifact, expr.text) &&
                !FindArtifactMethod(artifact, expr.text)) {
              if (error) *error = "unknown artifact member: " + base_type.name + "." + expr.text;
              return false;
            }
          }
        }
      }
      if (expr.op == "::" && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end() &&
            ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
          if (error) *error = "enum members must be qualified with '.': " + base.text + "." + expr.text;
          return false;
        }
      }
      return true;
    case ExprKind::Index:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(expr.children[1], ctx, scopes, current_artifact, error)) return false;
      {
        TypeRef base_type;
        if (InferExprType(expr.children[0], ctx, scopes, current_artifact, &base_type)) {
          if (base_type.dims.empty()) {
            if (error) *error = "indexing is only valid on arrays and lists";
            return false;
          }
        } else if (expr.children[0].kind == ExprKind::Literal) {
          if (error) *error = "indexing is only valid on arrays and lists";
          return false;
        }
      }
      if (expr.children[1].kind == ExprKind::Literal) {
        switch (expr.children[1].literal_kind) {
          case LiteralKind::Integer:
          case LiteralKind::Char:
            break;
          default:
            if (error) *error = "index must be an integer";
            return false;
        }
      } else {
        TypeRef index_type;
        if (InferExprType(expr.children[1], ctx, scopes, current_artifact, &index_type)) {
          if (!IsIntegerTypeName(index_type.name) && index_type.name != "char") {
            if (error) *error = "index must be an integer";
            return false;
          }
        }
      }
      return true;
    case ExprKind::ArrayLiteral:
    case ExprKind::ListLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::ArtifactLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, current_artifact, error)) return false;
      }
      for (const auto& field_value : expr.field_values) {
        if (!CheckExpr(field_value, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::FnLiteral:
      return true;
  }
  return true;
}

bool StmtReturns(const Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::Return:
      return true;
    case StmtKind::IfChain:
      if (stmt.if_branches.empty() || stmt.else_branch.empty()) return false;
      for (const auto& branch : stmt.if_branches) {
        if (!StmtsReturn(branch.second)) return false;
      }
      return StmtsReturn(stmt.else_branch);
    case StmtKind::IfStmt:
      if (stmt.if_then.empty() || stmt.if_else.empty()) return false;
      return StmtsReturn(stmt.if_then) && StmtsReturn(stmt.if_else);
    default:
      return false;
  }
}

bool StmtsReturn(const std::vector<Stmt>& stmts) {
  for (const auto& stmt : stmts) {
    if (StmtReturns(stmt)) return true;
  }
  return false;
}

bool CheckFunctionBody(const FuncDecl& fn,
                       const ValidateContext& ctx,
                       const std::unordered_set<std::string>& type_params,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  std::vector<std::unordered_map<std::string, LocalInfo>> scopes;
  scopes.emplace_back();
  std::unordered_set<std::string> param_names;
  const bool return_is_void = fn.return_type.name == "void";
  if (!CheckTypeRef(fn.return_type, ctx, type_params, TypeUse::Return, error)) return false;
  for (const auto& param : fn.params) {
    if (!param_names.insert(param.name).second) {
      if (error) *error = "duplicate parameter name: " + param.name;
      return false;
    }
    if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
    LocalInfo info;
    info.mutability = param.mutability;
    info.type = &param.type;
    if (!AddLocal(scopes, param.name, info, error)) return false;
  }
  for (const auto& stmt : fn.body) {
    if (!CheckStmt(stmt,
                   ctx,
                   type_params,
                   &fn.return_type,
                   return_is_void,
                   0,
                   scopes,
                   current_artifact,
                   error)) {
      return false;
    }
  }
  if (!return_is_void && !StmtsReturn(fn.body)) {
    if (error) *error = "non-void function does not return on all paths";
    return false;
  }
  return true;
}

} // namespace

bool ValidateProgram(const Program& program, std::string* error) {
  ValidateContext ctx;
  for (const auto& decl : program.decls) {
    const std::string* name_ptr = nullptr;
    switch (decl.kind) {
      case DeclKind::Enum:
        name_ptr = &decl.enm.name;
        {
          std::unordered_set<std::string> local_members;
          for (const auto& member : decl.enm.members) {
            if (!member.has_value) {
              if (error) *error = "enum member requires explicit value: " + member.name;
              return false;
            }
            if (!local_members.insert(member.name).second) {
              if (error) *error = "duplicate enum member: " + member.name;
              return false;
            }
            ctx.enum_members.insert(member.name);
          }
        }
        ctx.enum_types.insert(decl.enm.name);
        break;
      case DeclKind::Artifact:
        name_ptr = &decl.artifact.name;
        ctx.artifacts[decl.artifact.name] = &decl.artifact;
        ctx.artifact_generics[decl.artifact.name] = decl.artifact.generics.size();
        break;
      case DeclKind::Module:
        name_ptr = &decl.module.name;
        ctx.modules[decl.module.name] = &decl.module;
        break;
      case DeclKind::Function:
        name_ptr = &decl.func.name;
        ctx.functions[decl.func.name] = &decl.func;
        break;
      case DeclKind::Variable:
        name_ptr = &decl.var.name;
        ctx.globals[decl.var.name] = &decl.var;
        break;
    }
    if (name_ptr && !ctx.top_level.insert(*name_ptr).second) {
      if (error) *error = "duplicate top-level declaration: " + *name_ptr;
      return false;
    }
  }

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case DeclKind::Function:
        {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(decl.func.generics, &type_params, error)) return false;
          if (!CheckFunctionBody(decl.func, ctx, type_params, nullptr, error)) return false;
        }
        break;
      case DeclKind::Artifact:
        {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(decl.artifact.generics, &type_params, error)) return false;
          std::unordered_set<std::string> names;
          for (const auto& field : decl.artifact.fields) {
            if (!names.insert(field.name).second) {
              if (error) *error = "duplicate artifact member: " + field.name;
              return false;
            }
            if (!CheckTypeRef(field.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
          for (const auto& method : decl.artifact.methods) {
            if (!names.insert(method.name).second) {
              if (error) *error = "duplicate artifact member: " + method.name;
              return false;
            }
          }
          for (const auto& method : decl.artifact.methods) {
            if (!CheckFunctionBody(method, ctx, type_params, &decl.artifact, error)) return false;
          }
        }
        break;
      case DeclKind::Module:
        {
          std::unordered_set<std::string> names;
          for (const auto& var : decl.module.variables) {
            if (!names.insert(var.name).second) {
              if (error) *error = "duplicate module member: " + var.name;
              return false;
            }
            std::unordered_set<std::string> type_params;
            if (!CheckTypeRef(var.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
          for (const auto& fn : decl.module.functions) {
            if (!names.insert(fn.name).second) {
              if (error) *error = "duplicate module member: " + fn.name;
              return false;
            }
          }
        }
        for (const auto& fn : decl.module.functions) {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(fn.generics, &type_params, error)) return false;
          if (!CheckFunctionBody(fn, ctx, type_params, nullptr, error)) return false;
        }
        break;
      case DeclKind::Enum:
      case DeclKind::Variable:
        if (decl.kind == DeclKind::Variable) {
          std::unordered_set<std::string> type_params;
          if (!CheckTypeRef(decl.var.type, ctx, type_params, TypeUse::Value, error)) return false;
        }
        break;
    }
  }

  return true;
}

bool ValidateProgramFromString(const std::string& text, std::string* error) {
  Program program;
  if (!ParseProgramFromString(text, &program, error)) return false;
  return ValidateProgram(program, error);
}

} // namespace Simple::Lang
