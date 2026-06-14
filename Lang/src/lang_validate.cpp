#include "TAST/type_checker.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CAST/parser.h"
#include "lang_reserved.h"
#include "native/registry.h"
#include "RAST/member_resolution.h"
#include "RAST/reserved_resolution.h"
#include "TAST/calls.h"
#include "TAST/control_flow.h"
#include "TAST/expressions.h"
#include "TAST/generics.h"
#include "TAST/literals.h"
#include "TAST/statements.h"
#include "TAST/types.h"

namespace Simple::Lang {
namespace {

struct ValidateContext {
  std::unordered_set<std::string> enum_members;
  std::unordered_set<std::string> enum_types;
  std::unordered_map<std::string, std::unordered_set<std::string>> enum_members_by_type;
  std::unordered_set<std::string> top_level;
  std::unordered_map<std::string, const ArtifactDecl*> artifacts;
  std::unordered_map<std::string, size_t> artifact_generics;
  std::unordered_map<std::string, const ModuleDecl*> modules;
  std::unordered_map<std::string, const VarDecl*> globals;
  std::unordered_map<std::string, bool> global_points_to_immutable;
  std::unordered_map<std::string, const FuncDecl*> functions;
  std::unordered_map<std::string, const ExternDecl*> externs;
  std::unordered_map<std::string, std::unordered_map<std::string, const ExternDecl*>> externs_by_module;
  std::unordered_set<std::string> reserved_imports;
  std::unordered_map<std::string, std::string> reserved_import_aliases;
  std::unordered_set<std::string> using_reserved_modules;
};

struct LocalInfo {
  Mutability mutability = Mutability::Mutable;
  const TypeRef* type = nullptr;
  std::string dl_module;
  bool points_to_immutable = false;
};

struct CallTargetInfo {
  std::vector<TypeRef> params;
  TypeRef return_type;
  Mutability return_mutability = Mutability::Mutable;
  std::vector<std::string> type_params;
  bool is_proc = false;
};

void PrefixErrorLocation(uint32_t line, uint32_t column, std::string* error) {
  if (!error || error->empty() || line == 0) return;
  *error = std::to_string(line) + ":" + std::to_string(column) + ": " + *error;
}

bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out);
bool AnalyzeSwitchExpr(const Expr& expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       bool require_explicit_return,
                       const TypeRef* expected_type,
                       TypeRef* out_type,
                       std::string* error,
                       const std::unordered_set<std::string>* type_params = nullptr,
                       const TypeRef* expected_return = nullptr,
                       bool return_is_void = false,
                       int loop_depth = 0);
bool ValidateVarInitExpr(const VarDecl& var,
                         const ValidateContext& ctx,
                         const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                         const ArtifactDecl* current_artifact,
                         bool require_switch_returns,
                         std::string* error,
                         const std::unordered_set<std::string>* type_params = nullptr,
                         const TypeRef* expected_return = nullptr,
                         bool return_is_void = false,
                         int loop_depth = 0);
bool GetCallTargetInfo(const Expr& callee,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       CallTargetInfo* out,
                       std::string* error);
const LocalInfo* FindLocal(
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const std::string& name);

enum class TypeUse : uint8_t {
  Value,
  Return,
};

using RAST::FindArtifactField;
using RAST::FindArtifactMethod;
using RAST::FindModuleFunc;
using RAST::FindModuleVar;
using RAST::GetModuleNameFromExpr;
using RAST::IsArtifactMemberName;
using RAST::IsIoPrintName;
using RAST::ModuleMembers;
using RAST::NativeModuleNameForReserved;
using RAST::NormalizeDlMemberName;
using RAST::ReservedModuleMembers;
using RAST::UnknownMemberErrorWithSuggestion;
using TAST::ApplyTypeSubstitution;
using TAST::BuildArtifactTypeParamMap;
using TAST::CheckCompoundAssignOp;
using TAST::CheckFunctionCallArgs;
using TAST::CheckBinaryOpTypeRules;
using TAST::CheckProcTypeArgs;
using TAST::CheckUnaryOpTypeRules;
using TAST::CollectTypeParams;
using TAST::CollectTypeParamsMerged;
using TAST::CountFormatPlaceholders;
using TAST::CloneElementType;
using TAST::CloneTypeRef;
using TAST::CloneTypeVector;
using TAST::GetAtCastTargetName;
using TAST::IsBoolTypeName;
using TAST::IsAddressableExpr;
using TAST::IsAssignOp;
using TAST::IsFloatTypeName;
using TAST::IsIntegerTypeName;
using TAST::IsLiteralCompatibleWithScalarType;
using TAST::IsListLiteralExpr;
using TAST::IsListMethodName;
using TAST::IsNumericTypeName;
using TAST::IsPositionalBraceLiteralExpr;
using TAST::IsPrimitiveCastName;
using TAST::IsPrimitiveTypeName;
using TAST::IsScalarType;
using TAST::IsStringTypeName;
using TAST::MakeListType;
using TAST::MakeSimpleType;
using TAST::RequireScalar;
using TAST::SubstituteTypeParams;
using TAST::TypeDimsEqual;
using TAST::TypeEquals;
using TAST::TypesCompatibleForExpr;
using TAST::UnifyTypeParams;

bool ResolveReservedModuleName(const ValidateContext& ctx,
                               const std::string& name,
                               std::string* out);

bool IsIoPrintCallExpr(const Expr& callee, const ValidateContext& ctx) {
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  if (!IsIoPrintName(callee.text)) return false;
  if (callee.children[0].kind == ExprKind::Identifier && callee.children[0].text == "IO") return true;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  std::string resolved;
  return ResolveReservedModuleName(ctx, module_name, &resolved) && resolved == "IO";
}

bool IsReservedModuleEnabled(const ValidateContext& ctx, const std::string& name) {
  if (ctx.reserved_import_aliases.find(name) != ctx.reserved_import_aliases.end()) return true;
  if (ctx.reserved_imports.find(name) != ctx.reserved_imports.end()) return true;
  std::string canonical;
  if (!CanonicalizeReservedImportPath(name, &canonical)) return false;
  return ctx.reserved_imports.find(canonical) != ctx.reserved_imports.end();
}

bool ResolveReservedModuleName(const ValidateContext& ctx,
                               const std::string& name,
                               std::string* out) {
  if (!out) return false;
  std::string canonical;
  if (CanonicalizeReservedImportPath(name, &canonical) &&
      ctx.reserved_imports.find(canonical) != ctx.reserved_imports.end()) {
    *out = canonical;
    return true;
  }
  auto it = ctx.reserved_import_aliases.find(name);
  if (it != ctx.reserved_import_aliases.end()) {
    *out = it->second;
    return true;
  }
  return false;
}

bool IsCoreDlOpenCallExpr(const Expr& expr, const ValidateContext& ctx) {
  if (expr.kind != ExprKind::Call || expr.children.empty()) return false;
  const Expr& callee = expr.children[0];
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  if (!IsReservedModuleEnabled(ctx, module_name)) return false;
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module_name, &resolved)) return false;
  return resolved == "DL" && NormalizeDlMemberName(callee.text) == "open";
}

bool GetDlOpenManifestModule(const Expr& expr,
                             const ValidateContext& ctx,
                             std::string* out_module) {
  if (!out_module) return false;
  if (!IsCoreDlOpenCallExpr(expr, ctx)) return false;
  if (expr.args.size() != 2) return false;
  if (expr.args[1].kind != ExprKind::Identifier) return false;
  const std::string& module = expr.args[1].text;
  auto mod_it = ctx.externs_by_module.find(module);
  if (mod_it == ctx.externs_by_module.end() || mod_it->second.empty()) return false;
  *out_module = module;
  return true;
}

bool ResolveDlModuleForIdentifier(
    const std::string& ident,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    std::string* out_module) {
  if (!out_module) return false;
  if (const LocalInfo* local = FindLocal(scopes, ident)) {
    if (!local->dl_module.empty()) {
      *out_module = local->dl_module;
      return true;
    }
  }
  auto global_it = ctx.globals.find(ident);
  if (global_it != ctx.globals.end() &&
      global_it->second &&
      global_it->second->has_init_expr) {
    if (GetDlOpenManifestModule(global_it->second->init_expr, ctx, out_module)) {
      return true;
    }
  }
  return false;
}

bool IsSupportedDlAbiType(const TypeRef& type,
                          const ValidateContext& ctx,
                          bool allow_void) {
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) return false;
  if (type.pointer_depth > 0) return true;
  if (allow_void && type.name == "void") return true;
  if (type.name == "i8" || type.name == "i16" || type.name == "i32" || type.name == "i64" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64" ||
      type.name == "f32" || type.name == "f64" || type.name == "bool" || type.name == "char" ||
      type.name == "string") {
    return true;
  }
  if (ctx.enum_types.find(type.name) != ctx.enum_types.end()) return true;
  if (ctx.artifacts.find(type.name) != ctx.artifacts.end()) {
    std::unordered_set<std::string> visiting;
    std::function<bool(const std::string&)> check_struct = [&](const std::string& name) -> bool {
      if (!visiting.insert(name).second) return false;
      auto it = ctx.artifacts.find(name);
      if (it == ctx.artifacts.end()) {
        visiting.erase(name);
        return false;
      }
      const ArtifactDecl* art = it->second;
      for (const auto& field : art->fields) {
        if (field.type.is_proc || !field.type.type_args.empty() || !field.type.dims.empty()) {
          visiting.erase(name);
          return false;
        }
        if (field.type.pointer_depth > 0) continue;
        if (field.type.name == "i8" || field.type.name == "i16" || field.type.name == "i32" ||
            field.type.name == "i64" || field.type.name == "u8" || field.type.name == "u16" ||
            field.type.name == "u32" || field.type.name == "u64" || field.type.name == "f32" ||
            field.type.name == "f64" || field.type.name == "bool" || field.type.name == "char" ||
            field.type.name == "string") {
          continue;
        }
        if (ctx.enum_types.find(field.type.name) != ctx.enum_types.end()) continue;
        if (ctx.artifacts.find(field.type.name) != ctx.artifacts.end()) {
          if (!check_struct(field.type.name)) {
            visiting.erase(name);
            return false;
          }
          continue;
        }
        visiting.erase(name);
        return false;
      }
      visiting.erase(name);
      return true;
    };
    return check_struct(type.name);
  }
  return false;
}

bool IsSupportedDlDynamicSignature(const ExternDecl& ext,
                                   const ValidateContext& ctx,
                                   std::string* error) {
  if (!IsSupportedDlAbiType(ext.return_type, ctx, true)) {
    if (error) {
      *error = "dynamic DL return type for '" + ext.module + "." + ext.name +
               "' is not ABI-supported";
    }
    return false;
  }
  for (const auto& p : ext.params) {
    if (!IsSupportedDlAbiType(p.type, ctx, false)) {
      if (error) {
        *error = "dynamic DL parameter type for '" + ext.module + "." + ext.name +
                 "' is not ABI-supported";
      }
      return false;
    }
  }
  if (ext.params.size() > 254) {
    if (error) {
      *error = "dynamic DL symbol '" + ext.module + "." + ext.name +
               "' currently supports up to 254 ABI parameters";
    }
    return false;
  }
  return true;
}

bool GetReservedModuleVarType(const ValidateContext& ctx,
                              const std::string& module,
                              const std::string& member,
                              TypeRef* out) {
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module, &resolved)) return false;
  if (resolved == "Math" && member == "PI") {
    if (out) *out = MakeSimpleType("f64");
    return true;
  }
  if (resolved == "DL" && member == "supported") {
    if (out) *out = MakeSimpleType("bool");
    return true;
  }
  if (resolved == "OS" &&
      (member == "is_linux" || member == "is_macos" || member == "is_windows" || member == "has_dl")) {
    if (out) *out = MakeSimpleType("bool");
    return true;
  }
  return false;
}

bool GetReservedModuleCallTarget(const ValidateContext& ctx,
                                 const std::string& module,
                                 const std::string& member,
                                 CallTargetInfo* out);

bool ResolveUsingReservedCallTarget(const ValidateContext& ctx,
                                    const std::string& member,
                                    std::string* out_module,
                                    CallTargetInfo* out) {
  bool found = false;
  std::string found_module;
  CallTargetInfo found_info;
  for (const auto& module : ctx.using_reserved_modules) {
    CallTargetInfo candidate;
    if (module == "IO" && IsIoPrintName(member)) {
      candidate.params.push_back(MakeSimpleType("T"));
      candidate.return_type = MakeSimpleType("void");
      candidate.type_params = {"T"};
    } else if (!GetReservedModuleCallTarget(ctx, module, member, &candidate)) continue;
    if (found) return false;
    found = true;
    found_module = module;
    found_info = std::move(candidate);
  }
  if (!found) return false;
  if (out_module) *out_module = std::move(found_module);
  if (out) *out = std::move(found_info);
  return true;
}

bool NativeTypeToLangType(Simple::Byte::TypeKind kind, TypeRef* out) {
  if (!out) return false;
  switch (kind) {
    case Simple::Byte::TypeKind::Bool:
      *out = MakeSimpleType("bool");
      return true;
    case Simple::Byte::TypeKind::I32:
      *out = MakeSimpleType("i32");
      return true;
    case Simple::Byte::TypeKind::I64:
      *out = MakeSimpleType("i64");
      return true;
    case Simple::Byte::TypeKind::F32:
      *out = MakeSimpleType("f32");
      return true;
    case Simple::Byte::TypeKind::F64:
      *out = MakeSimpleType("f64");
      return true;
    case Simple::Byte::TypeKind::String:
      *out = MakeSimpleType("string");
      return true;
    case Simple::Byte::TypeKind::Ref:
      *out = MakeListType("i32");
      return true;
    case Simple::Byte::TypeKind::Unspecified:
      *out = MakeSimpleType("void");
      return true;
    default:
      return false;
  }
}

bool TryGetNativeReservedModuleCallTarget(const std::string& resolved,
                                          const std::string& member,
                                          CallTargetInfo* out) {
  if (!out) return false;
  std::string native_module;
  if (!NativeModuleNameForReserved(resolved, &native_module)) return false;
  static const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  const Simple::VM::Native::NativeFunctionSpec* spec = registry.Find(native_module, member);
  if (!spec) return false;
  out->params.clear();
  out->type_params.clear();
  out->is_proc = false;
  for (Simple::Byte::TypeKind param_kind : spec->parameter_types) {
    TypeRef param;
    if (!NativeTypeToLangType(param_kind, &param)) return false;
    out->params.push_back(std::move(param));
  }
  if (!NativeTypeToLangType(spec->result_type, &out->return_type)) return false;
  out->return_mutability = Mutability::Mutable;
  return true;
}

bool GetReservedModuleCallTarget(const ValidateContext& ctx,
                                 const std::string& module,
                                 const std::string& member,
                                 CallTargetInfo* out) {
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module, &resolved)) return false;
  if (!out) return false;
  out->params.clear();
  out->type_params.clear();
  out->is_proc = false;
  if (resolved == "Math") {
    if (member == "abs") {
      out->params.push_back(MakeSimpleType("T"));
      out->return_type = MakeSimpleType("T");
      out->return_mutability = Mutability::Mutable;
      out->type_params = {"T"};
      return true;
    }
    if (member == "min" || member == "max") {
      out->params.push_back(MakeSimpleType("T"));
      out->params.push_back(MakeSimpleType("T"));
      out->return_type = MakeSimpleType("T");
      out->return_mutability = Mutability::Mutable;
      out->type_params = {"T"};
      return true;
    }
    if (member == "sqrt") {
      out->params.push_back(MakeSimpleType("T"));
      out->return_type = MakeSimpleType("T");
      out->return_mutability = Mutability::Mutable;
      out->type_params = {"T"};
      return true;
    }
  }
  if (resolved == "Time") {
    if (member == "mono_ns" || member == "wall_ns") {
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "formatWallNs") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "IO") {
    if (member == "buffer_new") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeListType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "buffer_len") {
      out->params.push_back(MakeListType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "buffer_fill") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "buffer_copy") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "DL") {
    std::string dl_member = member;
    if (dl_member == "Open") dl_member = "open";
    else if (dl_member == "Sym") dl_member = "sym";
    else if (dl_member == "Close") dl_member = "close";
    else if (dl_member == "LastError") dl_member = "last_error";
    else if (dl_member == "CallI32") dl_member = "call_i32";
    else if (dl_member == "CallI64") dl_member = "call_i64";
    else if (dl_member == "CallF32") dl_member = "call_f32";
    else if (dl_member == "CallF64") dl_member = "call_f64";
    else if (dl_member == "CallStr0") dl_member = "call_str0";
    if (dl_member == "open") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "sym") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "close") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "last_error") {
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_i32") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_i64") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_f32") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("f32"));
      out->params.push_back(MakeSimpleType("f32"));
      out->return_type = MakeSimpleType("f32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_f64") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("f64"));
      out->params.push_back(MakeSimpleType("f64"));
      out->return_type = MakeSimpleType("f64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_str0") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "FS") {
    if (member == "readText") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "writeText") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "readBytes") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeListType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "listDir") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeListType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "writeBytes") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeListType("i32"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "copy") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "remove" || member == "mkdir" || member == "mkdirAll" || member == "setCwd") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "cwd") {
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Path") {
    if (member == "join") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "dirname" || member == "basename" || member == "ext" || member == "normalize") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "exists" || member == "isFile" || member == "isDir") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Env") {
    if (member == "argsCount") {
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "arg") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "get") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "set") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "platform" || member == "arch" || member == "exePath") {
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Random") {
    if (member == "seed") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "i32") {
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "range") {
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "f64") {
      out->return_type = MakeSimpleType("f64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Channel") {
    auto channel_value_type = [](const std::string& suffix) -> TypeRef {
      if (suffix == "I64") return MakeSimpleType("i64");
      if (suffix == "F32") return MakeSimpleType("f32");
      if (suffix == "F64") return MakeSimpleType("f64");
      if (suffix == "Bool") return MakeSimpleType("bool");
      if (suffix == "String") return MakeSimpleType("string");
      if (suffix == "Bytes") return MakeListType("i32");
      return MakeSimpleType("i32");
    };
    static constexpr const char* suffixes[] = {"I32", "I64", "F32", "F64", "Bool", "String", "Bytes"};
    for (const char* suffix_c : suffixes) {
      const std::string suffix = suffix_c;
      if (member == "new" + suffix) {
        out->return_type = MakeSimpleType("i64");
        out->return_mutability = Mutability::Mutable;
        return true;
      }
      if (member == "send" + suffix || member == "trySend" + suffix) {
        out->params.push_back(MakeSimpleType("i64"));
        out->params.push_back(channel_value_type(suffix));
        out->return_type = MakeSimpleType("bool");
        out->return_mutability = Mutability::Mutable;
        return true;
      }
      if (member == "recv" + suffix || member == "tryRecv" + suffix) {
        out->params.push_back(MakeSimpleType("i64"));
        out->return_type = channel_value_type(suffix);
        out->return_mutability = Mutability::Mutable;
        return true;
      }
      if (member == "pending" + suffix) {
        out->params.push_back(MakeSimpleType("i64"));
        out->return_type = MakeSimpleType("i32");
        out->return_mutability = Mutability::Mutable;
        return true;
      }
    }
    if (member == "close") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Thread") {
    if (member == "sleep") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "yield") {
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "hardwareConcurrency") {
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "OS") {
    if (member == "args_count") {
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "args_get") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "env_get") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "cwd_get") {
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "time_mono_ns" || member == "time_wall_ns") {
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "formatWallNs") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "sleep_ms") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "File") {
    if (member == "open") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "close") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "read" || member == "write") {
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Json") {
    if (member == "parse") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "stringify") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "free") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Buffer") {
    if (member == "new") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeListType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "len") {
      out->params.push_back(MakeListType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "readU16LE" || member == "readU32LE") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "writeU16LE" || member == "writeU32LE") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "slice") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeListType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "copy") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Log") {
    if (member == "log") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "info" || member == "warn" || member == "error") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "setLevel") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "setFile") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("bool");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  return TryGetNativeReservedModuleCallTarget(resolved, member, out);
}

bool InferTypeArgsFromCall(const std::vector<TypeRef>& param_types,
                           const std::vector<Expr>& call_args,
                           const std::unordered_set<std::string>& type_params,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           std::unordered_map<std::string, TypeRef>* out_mapping) {
  if (!out_mapping) return false;
  out_mapping->clear();
  if (param_types.size() != call_args.size()) return false;
  for (size_t i = 0; i < param_types.size(); ++i) {
    TypeRef arg_type;
    if (!InferExprType(call_args[i], ctx, scopes, current_artifact, &arg_type)) return false;
    if (!UnifyTypeParams(param_types[i], arg_type, type_params, out_mapping)) return false;
  }
  for (const auto& name : type_params) {
    if (out_mapping->find(name) == out_mapping->end()) return false;
  }
  return true;
}

bool CheckTypeRef(const TypeRef& type,
                  const ValidateContext& ctx,
                  const std::unordered_set<std::string>& type_params,
                  TypeUse use,
                  std::string* error) {
  if (type.pointer_depth > 0) {
    TypeRef pointee;
    if (!CloneTypeRef(type, &pointee)) return false;
    pointee.pointer_depth -= 1;
    if (pointee.pointer_depth == 0 && pointee.name == "void") {
      if (!pointee.type_args.empty()) {
        if (error) *error = "void cannot have type arguments";
        PrefixErrorLocation(type.line, type.column, error);
        return false;
      }
      return true;
    }
    return CheckTypeRef(pointee, ctx, type_params, TypeUse::Value, error);
  }
  if (type.is_proc) {
    for (const auto& param : type.proc_params) {
      if (!CheckTypeRef(param, ctx, type_params, TypeUse::Value, error)) return false;
    }
    if (!type.proc_return) {
      if (error) *error = "procedure type missing return type";
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    return CheckTypeRef(*type.proc_return, ctx, type_params, TypeUse::Return, error);
  }

  if (type.name == "void") {
    if (use != TypeUse::Return) {
      if (error) *error = "void is only valid as a return type";
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (!type.type_args.empty()) {
      if (error) *error = "void cannot have type arguments";
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    return true;
  }

  const bool is_primitive = IsPrimitiveTypeName(type.name);
  const bool is_type_param = type_params.find(type.name) != type_params.end();
  const bool is_user_type = ctx.top_level.find(type.name) != ctx.top_level.end();

  if (IsReservedModuleEnabled(ctx, type.name)) {
    if (error) *error = "module is not a type: " + type.name;
    PrefixErrorLocation(type.line, type.column, error);
    return false;
  }

  if (!is_primitive && !is_type_param && !is_user_type) {
    if (error) *error = "unknown type: " + type.name;
    PrefixErrorLocation(type.line, type.column, error);
    return false;
  }

  if (is_user_type && !is_type_param) {
    if (ctx.modules.find(type.name) != ctx.modules.end()) {
      if (error) *error = "module is not a type: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (ctx.functions.find(type.name) != ctx.functions.end()) {
      if (error) *error = "function is not a type: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (ctx.enum_types.find(type.name) != ctx.enum_types.end()) {
      if (!type.type_args.empty()) {
        if (error) *error = "enum type cannot have type arguments: " + type.name;
        PrefixErrorLocation(type.line, type.column, error);
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
        PrefixErrorLocation(type.line, type.column, error);
        return false;
      }
    }
  }

  if (!type.type_args.empty()) {
    if (is_primitive) {
      if (error) *error = "primitive type cannot have type arguments: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (is_type_param) {
      if (error) *error = "type parameter cannot have type arguments: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
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
bool CheckFnLiteralAgainstType(const Expr& fn_expr,
                               const TypeRef& target_type,
                               std::string* error);

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
    case ExprKind::FormatString:
      out->name = "string";
      out->type_args.clear();
      out->dims.clear();
      out->is_proc = false;
      out->proc_params.clear();
      out->proc_return.reset();
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
      if (base.kind == ExprKind::Identifier && base.text == "System") {
        return true;
      }
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
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          if (GetReservedModuleVarType(ctx, module_name, expr.text, out)) {
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(expr.text);
          if (ext_it != ext_mod_it->second.end()) {
            return CloneTypeRef(ext_it->second->return_type, out);
          }
          return false;
        }
      }
        if (const LocalInfo* local = FindLocal(scopes, base.text)) {
          if (!local->type) return false;
          auto artifact_it = ctx.artifacts.find(local->type->name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, nullptr)) return false;
          }
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(field->type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(method->return_type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
        }
        auto global_it = ctx.globals.find(base.text);
        if (global_it != ctx.globals.end()) {
          auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, nullptr)) return false;
          }
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(field->type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(method->return_type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
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
        std::string cast_target;
        if (GetAtCastTargetName(callee.text, &cast_target)) {
          out->name = cast_target;
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
      }
      CallTargetInfo info;
      if (!GetCallTargetInfo(callee, ctx, scopes, current_artifact, &info, nullptr)) return false;
      if (info.type_params.empty()) {
        return CloneTypeRef(info.return_type, out);
      }
      std::unordered_map<std::string, TypeRef> mapping;
      if (!expr.type_args.empty()) {
        if (expr.type_args.size() != info.type_params.size()) return false;
        for (size_t i = 0; i < info.type_params.size(); ++i) {
          TypeRef copy;
          if (!CloneTypeRef(expr.type_args[i], &copy)) return false;
          mapping[info.type_params[i]] = std::move(copy);
        }
      } else {
        std::unordered_set<std::string> type_param_set(info.type_params.begin(),
                                                       info.type_params.end());
        if (!InferTypeArgsFromCall(info.params, expr.args, type_param_set,
                                   ctx, scopes, current_artifact, &mapping)) {
          return false;
        }
      }
      TypeRef resolved;
      if (!SubstituteTypeParams(info.return_type, mapping, &resolved)) return false;
      return CloneTypeRef(resolved, out);
    }
    case ExprKind::Index: {
      TypeRef base_type;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &base_type)) return false;
      if (base_type.dims.empty()) return false;
      TypeRef result;
      if (!CloneTypeRef(base_type, &result)) return false;
      result.dims.erase(result.dims.begin());
      result.is_proc = false;
      result.proc_params.clear();
      result.proc_return.reset();
      return CloneTypeRef(result, out);
    }
    case ExprKind::Unary: {
      if (expr.children.empty()) return false;
      TypeRef operand;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &operand)) return false;
      const std::string op = expr.op.rfind("post", 0) == 0 ? expr.op.substr(4) : expr.op;
      if (op == "&") {
        TypeRef result = operand;
        result.pointer_depth += 1;
        return CloneTypeRef(result, out);
      }
      if (!IsScalarType(operand)) return false;
      if (op == "!") {
        if (!IsBoolTypeName(operand.name)) return false;
        out->name = "bool";
        out->pointer_depth = 0;
        out->type_args.clear();
        out->dims.clear();
        out->is_proc = false;
        out->proc_params.clear();
        out->proc_return.reset();
        return true;
      }
      if (op == "++" || op == "--" || op == "-") {
        if (!IsNumericTypeName(operand.name)) return false;
        return CloneTypeRef(operand, out);
      }
      return false;
    }
    case ExprKind::Binary: {
      if (expr.children.size() < 2) return false;
      TypeRef lhs;
      TypeRef rhs;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &lhs)) return false;
      if (!InferExprType(expr.children[1], ctx, scopes, current_artifact, &rhs)) return false;
      if (!IsScalarType(lhs) || !IsScalarType(rhs)) return false;

      TypeRef common;
      if (TypeEquals(lhs, rhs)) {
        if (!CloneTypeRef(lhs, &common)) return false;
      } else {
        if (IsLiteralCompatibleWithScalarType(expr.children[0], rhs)) {
          if (!CloneTypeRef(rhs, &common)) return false;
        } else if (IsLiteralCompatibleWithScalarType(expr.children[1], lhs)) {
          if (!CloneTypeRef(lhs, &common)) return false;
        } else {
          return false;
        }
      }

      const std::string& op = expr.op;
      if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
          op == "&&" || op == "||") {
        out->name = "bool";
        out->pointer_depth = 0;
        out->type_args.clear();
        out->dims.clear();
        out->is_proc = false;
        out->proc_params.clear();
        out->proc_return.reset();
        return true;
      }

      if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
          op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" ||
          op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
          op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        return CloneTypeRef(common, out);
      }
      return false;
    }
    case ExprKind::Switch: {
      TypeRef result;
      if (!AnalyzeSwitchExpr(expr, ctx, scopes, current_artifact, false, nullptr, &result, nullptr)) {
        return false;
      }
      return CloneTypeRef(result, out);
    }
    default:
      return false;
  }
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

bool IsMutableStorageExpr(const Expr& expr,
                          const ValidateContext& ctx,
                          const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                          const ArtifactDecl* current_artifact,
                          bool* out_known) {
  if (out_known) *out_known = true;
  if (expr.kind == ExprKind::Identifier) {
    if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
      return local->mutability == Mutability::Mutable;
    }
    auto global_it = ctx.globals.find(expr.text);
    if (global_it != ctx.globals.end()) {
      return global_it->second->mutability == Mutability::Mutable;
    }
    if (out_known) *out_known = false;
    return true;
  }
  if (expr.kind == ExprKind::Member &&
      (expr.op == "." || expr.op == "->") &&
      !expr.children.empty()) {
    const Expr& base = expr.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "self") {
        const VarDecl* field = FindArtifactField(current_artifact, expr.text);
        if (field) return field->mutability == Mutability::Mutable;
        if (out_known) *out_known = false;
        return true;
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const VarDecl* var = FindModuleVar(module_it->second, expr.text);
        if (var) return var->mutability == Mutability::Mutable;
        if (out_known) *out_known = false;
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        auto artifact_it = ctx.artifacts.find(local->type ? local->type->name : "");
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, expr.text);
        if (field) return field->mutability == Mutability::Mutable;
        if (out_known) *out_known = false;
        return true;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, expr.text);
        if (field) return field->mutability == Mutability::Mutable;
        if (out_known) *out_known = false;
        return true;
      }
    }
    if (out_known) *out_known = false;
    return true;
  }
  if (expr.kind == ExprKind::Index && !expr.children.empty()) {
    return IsMutableStorageExpr(expr.children[0], ctx, scopes, current_artifact, out_known);
  }
  if (out_known) *out_known = false;
  return true;
}

bool GetPointerImmutabilityFromExpr(const Expr& expr,
                                    const ValidateContext& ctx,
                                    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                    const ArtifactDecl* current_artifact,
                                    bool* out_known,
                                    bool* out_points_to_immutable) {
  if (out_known) *out_known = false;
  if (out_points_to_immutable) *out_points_to_immutable = false;
  if (expr.kind == ExprKind::Unary && expr.op == "&" && !expr.children.empty()) {
    bool known = false;
    const bool is_mutable = IsMutableStorageExpr(expr.children[0], ctx, scopes, current_artifact, &known);
    if (out_known) *out_known = known;
    if (out_points_to_immutable) *out_points_to_immutable = known && !is_mutable;
    return true;
  }
  if (expr.kind == ExprKind::Identifier) {
    if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
      if (out_known) *out_known = true;
      if (out_points_to_immutable) *out_points_to_immutable = local->points_to_immutable;
      return true;
    }
    auto global_it = ctx.global_points_to_immutable.find(expr.text);
    if (global_it != ctx.global_points_to_immutable.end()) {
      if (out_known) *out_known = true;
      if (out_points_to_immutable) *out_points_to_immutable = global_it->second;
      return true;
    }
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
    if (IsPrimitiveCastName(callee.text)) {
      if (error) *error = "primitive cast syntax requires '@': use @" + callee.text + "(value)";
      return false;
    }
    auto fn_it = ctx.functions.find(callee.text);
    if (fn_it != ctx.functions.end()) {
      return CheckFunctionCallArgs(fn_it->second, arg_count, error);
    }
    auto ext_it = ctx.externs.find(callee.text);
    if (ext_it != ctx.externs.end()) {
      if (ext_it->second->params.size() != arg_count) {
        if (error) {
          *error = "call argument count mismatch for extern " + callee.text +
                   ": expected " + std::to_string(ext_it->second->params.size()) +
                   ", got " + std::to_string(arg_count);
        }
        return false;
      }
      return true;
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
    CallTargetInfo using_info;
    std::string using_module;
    if (ResolveUsingReservedCallTarget(ctx, callee.text, &using_module, &using_info)) {
      if (using_module == "IO" && IsIoPrintName(callee.text)) {
        if (arg_count == 0) {
          if (error) *error = "call argument count mismatch for " + callee.text;
          return false;
        }
        return true;
      }
      if (using_info.params.size() != arg_count) {
        if (error) {
          *error = "call argument count mismatch for " + callee.text +
                   ": expected " + std::to_string(using_info.params.size()) +
                   ", got " + std::to_string(arg_count);
        }
        return false;
      }
      return true;
    }
    return true;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (IsIoPrintCallExpr(callee, ctx)) {
        if (arg_count == 0) {
          if (error) *error = "call argument count mismatch for IO." + callee.text;
          return false;
        }
        return true;
      }
      if (base.text == "self") {
        const FuncDecl* method = FindArtifactMethod(current_artifact, callee.text);
        if (method) return CheckFunctionCallArgs(method, arg_count, error);
        if (FindArtifactField(current_artifact, callee.text)) {
          if (error) *error = "attempt to call non-function: self." + callee.text;
          return false;
        }
        return true;
      }
      std::string dl_module;
      if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
        auto mod_it = ctx.externs_by_module.find(dl_module);
        if (mod_it != ctx.externs_by_module.end()) {
          auto ext_it = mod_it->second.find(callee.text);
          if (ext_it != mod_it->second.end()) {
            if (!IsSupportedDlDynamicSignature(*ext_it->second, ctx, error)) return false;
            if (ext_it->second->params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for dynamic symbol " +
                         base.text + "." + callee.text + ": expected " +
                         std::to_string(ext_it->second->params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
          if (error) {
            *error = "unknown dynamic symbol: " + base.text + "." + callee.text;
          }
          return false;
        }
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const FuncDecl* fn = FindModuleFunc(module_it->second, callee.text);
        if (fn) return CheckFunctionCallArgs(fn, arg_count, error);
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
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          CallTargetInfo info;
          if (GetReservedModuleCallTarget(ctx, module_name, callee.text, &info)) {
            std::string resolved_module;
            const bool is_System_dl_open =
                ResolveReservedModuleName(ctx, module_name, &resolved_module) &&
                resolved_module == "DL" &&
                NormalizeDlMemberName(callee.text) == "open";
            if (!is_System_dl_open && info.params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for " + module_name + "." + callee.text +
                         ": expected " + std::to_string(info.params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            if (is_System_dl_open && arg_count != 1 && arg_count != 2) {
              if (error) {
                *error = "call argument count mismatch for " + module_name + "." + callee.text +
                         ": expected 1 or 2, got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(callee.text);
          if (ext_it != ext_mod_it->second.end()) {
            if (ext_it->second->params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for extern " + module_name + "." + callee.text +
                         ": expected " + std::to_string(ext_it->second->params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
          if (error) *error = "unknown extern member: " + module_name + "." + callee.text;
          return false;
        }
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) return CheckFunctionCallArgs(method, arg_count, error);
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
        if (method) return CheckFunctionCallArgs(method, arg_count, error);
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

bool GetCallTargetInfo(const Expr& callee,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       CallTargetInfo* out,
                       std::string* error) {
  if (!out) return false;
  if (callee.kind == ExprKind::FnLiteral) {
    out->params.clear();
    out->return_type = TypeRef{};
    out->return_mutability = Mutability::Mutable;
    out->type_params.clear();
    out->is_proc = true;
    for (const auto& param : callee.fn_params) {
      TypeRef copy;
      if (!CloneTypeRef(param.type, &copy)) return false;
      out->params.push_back(std::move(copy));
    }
    return true;
  }
  if (callee.kind == ExprKind::Identifier) {
    auto fn_it = ctx.functions.find(callee.text);
    if (fn_it != ctx.functions.end()) {
      out->params.clear();
      if (!CloneTypeRef(fn_it->second->return_type, &out->return_type)) return false;
      out->return_mutability = fn_it->second->return_mutability;
      out->type_params = fn_it->second->generics;
      out->is_proc = false;
      for (const auto& param : fn_it->second->params) {
        TypeRef copy;
        if (!CloneTypeRef(param.type, &copy)) return false;
        out->params.push_back(std::move(copy));
      }
      return true;
    }
    auto ext_it = ctx.externs.find(callee.text);
    if (ext_it != ctx.externs.end()) {
      out->params.clear();
      if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
      out->return_mutability = ext_it->second->return_mutability;
      out->type_params.clear();
      out->is_proc = false;
      for (const auto& param : ext_it->second->params) {
        TypeRef copy;
        if (!CloneTypeRef(param.type, &copy)) return false;
        out->params.push_back(std::move(copy));
      }
      return true;
    }
    if (const LocalInfo* local = FindLocal(scopes, callee.text)) {
      if (local->type && local->type->is_proc) {
        if (!CloneTypeVector(local->type->proc_params, &out->params)) return false;
        if (local->type->proc_return) {
          if (!CloneTypeRef(*local->type->proc_return, &out->return_type)) return false;
        }
        out->return_mutability = local->type->proc_return_mutability;
        out->type_params.clear();
        out->is_proc = true;
        return true;
      }
      return false;
    }
    auto global_it = ctx.globals.find(callee.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->type.is_proc) {
        if (!CloneTypeVector(global_it->second->type.proc_params, &out->params)) return false;
        if (global_it->second->type.proc_return) {
          if (!CloneTypeRef(*global_it->second->type.proc_return, &out->return_type)) return false;
        }
        out->return_mutability = global_it->second->type.proc_return_mutability;
        out->type_params.clear();
        out->is_proc = true;
        return true;
      }
      return false;
    }
    if (ResolveUsingReservedCallTarget(ctx, callee.text, nullptr, out)) return true;
    return false;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (IsIoPrintCallExpr(callee, ctx)) {
        out->params.clear();
        TypeRef param;
        param.name = "T";
        param.type_args.clear();
        param.dims.clear();
        param.is_proc = false;
        param.proc_params.clear();
        param.proc_return.reset();
        out->params.push_back(std::move(param));
        out->return_type = TypeRef{};
        out->return_type.name = "void";
        out->return_type.type_args.clear();
        out->return_type.dims.clear();
        out->return_type.is_proc = false;
        out->return_type.proc_params.clear();
        out->return_type.proc_return.reset();
        out->return_mutability = Mutability::Mutable;
        out->type_params = {"T"};
        out->is_proc = false;
        return true;
      }
      if (base.text == "self") {
        const FuncDecl* method = FindArtifactMethod(current_artifact, callee.text);
        if (!method) return false;
        out->params.clear();
        if (!CloneTypeRef(method->return_type, &out->return_type)) return false;
        out->return_mutability = method->return_mutability;
        out->type_params = method->generics;
        out->is_proc = false;
        for (const auto& param : method->params) {
          TypeRef copy;
          if (!CloneTypeRef(param.type, &copy)) return false;
          out->params.push_back(std::move(copy));
        }
        return true;
      }
      std::string dl_module;
      if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
        auto mod_it = ctx.externs_by_module.find(dl_module);
        if (mod_it != ctx.externs_by_module.end()) {
          auto ext_it = mod_it->second.find(callee.text);
          if (ext_it != mod_it->second.end()) {
            if (!IsSupportedDlDynamicSignature(*ext_it->second, ctx, error)) return false;
            out->params.clear();
            if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
            out->return_mutability = ext_it->second->return_mutability;
            out->type_params.clear();
            out->is_proc = false;
            for (const auto& param : ext_it->second->params) {
              TypeRef copy;
              if (!CloneTypeRef(param.type, &copy)) return false;
              out->params.push_back(std::move(copy));
            }
            return true;
          }
        }
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const FuncDecl* fn = FindModuleFunc(module_it->second, callee.text);
        if (fn) {
          out->params.clear();
          if (!CloneTypeRef(fn->return_type, &out->return_type)) return false;
          out->return_mutability = fn->return_mutability;
          out->type_params = fn->generics;
          out->is_proc = false;
          for (const auto& param : fn->params) {
            TypeRef copy;
            if (!CloneTypeRef(param.type, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* var = FindModuleVar(module_it->second, callee.text);
        if (var && var->type.is_proc) {
          if (!CloneTypeVector(var->type.proc_params, &out->params)) return false;
          if (var->type.proc_return) {
            if (!CloneTypeRef(*var->type.proc_return, &out->return_type)) return false;
          }
          out->return_mutability = var->type.proc_return_mutability;
          out->type_params.clear();
          out->is_proc = true;
          return true;
        }
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          if (GetReservedModuleCallTarget(ctx, module_name, callee.text, out)) {
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(callee.text);
          if (ext_it != ext_mod_it->second.end()) {
            out->params.clear();
            if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
            out->return_mutability = ext_it->second->return_mutability;
            out->type_params.clear();
            out->is_proc = false;
            for (const auto& param : ext_it->second->params) {
              TypeRef copy;
              if (!CloneTypeRef(param.type, &copy)) return false;
              out->params.push_back(std::move(copy));
            }
            return true;
          }
        }
      }
      TypeRef base_type;
      if (InferExprType(base, ctx, scopes, current_artifact, &base_type) &&
          !base_type.dims.empty() && base_type.dims.front().is_list) {
        TypeRef element_type;
        if (!CloneElementType(base_type, &element_type)) return false;
        out->params.clear();
        out->type_params.clear();
        out->is_proc = false;
        out->return_mutability = Mutability::Mutable;
        if (callee.text == "len") {
          out->return_type = MakeSimpleType("i32");
          return true;
        }
        if (callee.text == "push") {
          out->params.push_back(element_type);
          out->return_type = MakeSimpleType("void");
          return true;
        }
        if (callee.text == "pop") {
          out->return_type = element_type;
          return true;
        }
        if (callee.text == "insert") {
          out->params.push_back(MakeSimpleType("i32"));
          out->params.push_back(element_type);
          out->return_type = MakeSimpleType("void");
          return true;
        }
        if (callee.text == "remove") {
          out->params.push_back(MakeSimpleType("i32"));
          out->return_type = element_type;
          return true;
        }
        if (callee.text == "clear") {
          out->return_type = MakeSimpleType("void");
          return true;
        }
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return false;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, error)) return false;
          }
          out->params.clear();
          TypeRef resolved_return;
          if (!SubstituteTypeParams(method->return_type, mapping, &resolved_return)) return false;
          if (!CloneTypeRef(resolved_return, &out->return_type)) return false;
          out->return_mutability = method->return_mutability;
          out->type_params = method->generics;
          out->is_proc = false;
          for (const auto& param : method->params) {
            TypeRef copy;
            if (!SubstituteTypeParams(param.type, mapping, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* field = FindArtifactField(artifact, callee.text);
        if (field && field->type.is_proc) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, error)) return false;
          }
          TypeRef resolved_field;
          if (!SubstituteTypeParams(field->type, mapping, &resolved_field)) return false;
          out->params.clear();
          out->type_params.clear();
          out->is_proc = true;
          out->return_mutability = resolved_field.proc_return_mutability;
          if (!CloneTypeVector(resolved_field.proc_params, &out->params)) return false;
          if (resolved_field.proc_return) {
            if (!CloneTypeRef(*resolved_field.proc_return, &out->return_type)) return false;
          }
          return true;
        }
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, error)) return false;
          }
          out->params.clear();
          TypeRef resolved_return;
          if (!SubstituteTypeParams(method->return_type, mapping, &resolved_return)) return false;
          if (!CloneTypeRef(resolved_return, &out->return_type)) return false;
          out->return_mutability = method->return_mutability;
          out->type_params = method->generics;
          out->is_proc = false;
          for (const auto& param : method->params) {
            TypeRef copy;
            if (!SubstituteTypeParams(param.type, mapping, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* field = FindArtifactField(artifact, callee.text);
        if (field && field->type.is_proc) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, error)) return false;
          }
          TypeRef resolved_field;
          if (!SubstituteTypeParams(field->type, mapping, &resolved_field)) return false;
          out->params.clear();
          out->type_params.clear();
          out->is_proc = true;
          out->return_mutability = resolved_field.proc_return_mutability;
          if (!CloneTypeVector(resolved_field.proc_params, &out->params)) return false;
          if (resolved_field.proc_return) {
            if (!CloneTypeRef(*resolved_field.proc_return, &out->return_type)) return false;
          }
          return true;
        }
      }
    }
  }
  if (error) *error = "attempt to call non-function";
  return false;
}

bool CheckCallArgTypes(const Expr& call_expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  if (call_expr.kind != ExprKind::Call || call_expr.children.empty()) return true;
  const Expr& callee = call_expr.children[0];
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    std::string module_name;
    if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
      std::string mod = module_name;
      std::string resolved_mod;
      if (ResolveReservedModuleName(ctx, module_name, &resolved_mod)) mod = resolved_mod;
      const std::string& name = callee.text;
      auto infer_arg = [&](size_t index, TypeRef* out_type) -> bool {
        if (!out_type) return false;
        if (index >= call_expr.args.size()) return false;
        return InferExprType(call_expr.args[index], ctx, scopes, current_artifact, out_type);
      };
      auto is_i32_buffer = [&](const TypeRef& t) -> bool {
        return t.name == "i32" && !t.is_proc && t.type_args.empty() &&
               t.dims.size() == 1;
      };
      if (mod == "Math") {
        if (name == "abs") {
          if (call_expr.args.size() != 1) return true;
          TypeRef arg;
          if (!infer_arg(0, &arg)) return true;
          if ((arg.name != "i32" && arg.name != "i64") || !arg.dims.empty() || arg.is_proc) {
            if (error) *error = "Math.abs expects i32 or i64 argument";
            return false;
          }
          return true;
        }
        if (name == "sqrt") {
          if (call_expr.args.size() != 1) return true;
          TypeRef arg;
          if (!infer_arg(0, &arg)) return true;
          if ((arg.name != "f32" && arg.name != "f64") || !arg.dims.empty() || arg.is_proc) {
            if (error) *error = "Math.sqrt expects f32 or f64 argument";
            return false;
          }
          return true;
        }
        if (name == "min" || name == "max") {
          if (call_expr.args.size() != 2) return true;
          TypeRef a;
          TypeRef b;
          if (!infer_arg(0, &a) || !infer_arg(1, &b)) return true;
          auto allowed = [&](const TypeRef& t) {
            return t.name == "i32" || t.name == "i64" || t.name == "f32" || t.name == "f64";
          };
          if (!allowed(a) || !allowed(b) || !TypeEquals(a, b) || !a.dims.empty() || !b.dims.empty()) {
            if (error) *error = "Math." + name + " expects two numeric arguments of the same type";
            return false;
          }
          return true;
        }
      }
      if (mod == "IO") {
        if (name == "buffer_new") {
          if (call_expr.args.size() != 1) return true;
          TypeRef len;
          if (!infer_arg(0, &len)) return true;
          if (len.name != "i32" || !len.dims.empty()) {
            if (error) *error = "IO.buffer_new expects (i32)";
            return false;
          }
          return true;
        }
        if (name == "buffer_len") {
          if (call_expr.args.size() != 1) return true;
          TypeRef buf;
          if (!infer_arg(0, &buf)) return true;
          if (!is_i32_buffer(buf)) {
            if (error) *error = "IO.buffer_len expects (i32[])";
            return false;
          }
          return true;
        }
        if (name == "buffer_fill") {
          if (call_expr.args.size() != 3) return true;
          TypeRef buf;
          TypeRef value;
          TypeRef count;
          if (!infer_arg(0, &buf) || !infer_arg(1, &value) || !infer_arg(2, &count)) return true;
          if (!is_i32_buffer(buf) || value.name != "i32" || !value.dims.empty() ||
              count.name != "i32" || !count.dims.empty()) {
            if (error) *error = "IO.buffer_fill expects (i32[], i32, i32)";
            return false;
          }
          return true;
        }
        if (name == "buffer_copy") {
          if (call_expr.args.size() != 3) return true;
          TypeRef dst;
          TypeRef src;
          TypeRef count;
          if (!infer_arg(0, &dst) || !infer_arg(1, &src) || !infer_arg(2, &count)) return true;
          if (!is_i32_buffer(dst) || !is_i32_buffer(src) || count.name != "i32" || !count.dims.empty()) {
            if (error) *error = "IO.buffer_copy expects (i32[], i32[], i32)";
            return false;
          }
          return true;
        }
      }
      if (mod == "Time") {
        if (name == "mono_ns" || name == "wall_ns") {
          if (!call_expr.args.empty()) {
            if (error) *error = "Time." + name + " expects no arguments";
            return false;
          }
          return true;
        }
        if (name == "formatWallNs") {
          if (call_expr.args.size() != 1) {
            if (error) *error = "Time.formatWallNs expects (i64)";
            return false;
          }
          TypeRef ns;
          if (!infer_arg(0, &ns)) return true;
          if (ns.name != "i64" || !ns.dims.empty()) {
            if (error) *error = "Time.formatWallNs expects (i64)";
            return false;
          }
          return true;
        }
      }
      if (mod == "DL" && NormalizeDlMemberName(name) == "open") {
        if (call_expr.args.size() != 1 && call_expr.args.size() != 2) {
          if (error) *error = "DL.open expects (string) or (string, manifest)";
          return false;
        }
        TypeRef path;
        if (!infer_arg(0, &path)) return true;
        if (path.name != "string" || !path.dims.empty()) {
          if (error) *error = "DL.open expects first argument string path";
          return false;
        }
        if (call_expr.args.size() == 2) {
          if (call_expr.args[1].kind != ExprKind::Identifier) {
            if (error) *error = "DL.open manifest must be an extern module identifier";
            return false;
          }
          const std::string manifest = call_expr.args[1].text;
          auto mod_it = ctx.externs_by_module.find(manifest);
          if (mod_it == ctx.externs_by_module.end() || mod_it->second.empty()) {
            if (error) *error = "DL.open manifest has no extern symbols: " + manifest;
            return false;
          }
          for (const auto& entry : mod_it->second) {
            if (!IsSupportedDlDynamicSignature(*entry.second, ctx, error)) return false;
          }
        }
        return true;
      }
      if (mod == "File") {
        if (name == "open") {
          if (call_expr.args.size() != 2) return true;
          TypeRef path;
          TypeRef flags;
          if (!infer_arg(0, &path) || !infer_arg(1, &flags)) return true;
          if (path.name != "string" || !path.dims.empty() || flags.name != "i32" || !flags.dims.empty()) {
            if (error) *error = "File.open expects (string, i32)";
            return false;
          }
          return true;
        }
        if (name == "close") {
          if (call_expr.args.size() != 1) return true;
          TypeRef fd;
          if (!infer_arg(0, &fd)) return true;
          if (fd.name != "i32" || !fd.dims.empty()) {
            if (error) *error = "File.close expects (i32)";
            return false;
          }
          return true;
        }
        if (name == "read" || name == "write") {
          if (call_expr.args.size() != 3) return true;
          TypeRef fd;
          TypeRef buf;
          TypeRef len;
          if (!infer_arg(0, &fd) || !infer_arg(1, &buf) || !infer_arg(2, &len)) return true;
          if (fd.name != "i32" || !fd.dims.empty() || len.name != "i32" || !len.dims.empty() ||
              !is_i32_buffer(buf)) {
            if (error) *error = "File." + name + " expects (i32, i32[], i32)";
            return false;
          }
          return true;
        }
      }
    }
  }
  CallTargetInfo info;
  if (!GetCallTargetInfo(callee, ctx, scopes, current_artifact, &info, error)) return true;
  if (!info.type_params.empty() && !call_expr.type_args.empty()) {
    if (call_expr.type_args.size() != info.type_params.size()) {
      if (error) {
        *error = "generic type argument count mismatch: expected " +
                 std::to_string(info.type_params.size()) + ", got " +
                 std::to_string(call_expr.type_args.size());
      }
      return false;
    }
  } else if (info.type_params.empty() && !call_expr.type_args.empty()) {
    if (error) *error = "non-generic call cannot take type arguments";
    return false;
  }

  std::unordered_map<std::string, TypeRef> mapping;
  if (!info.type_params.empty()) {
    std::unordered_set<std::string> type_param_set(info.type_params.begin(), info.type_params.end());
    if (!call_expr.type_args.empty()) {
      for (size_t i = 0; i < info.type_params.size(); ++i) {
        TypeRef copy;
        if (!CloneTypeRef(call_expr.type_args[i], &copy)) return false;
        mapping[info.type_params[i]] = std::move(copy);
      }
    } else {
      if (!InferTypeArgsFromCall(info.params, call_expr.args, type_param_set,
                                 ctx, scopes, current_artifact, &mapping)) {
        if (error) *error = "cannot infer type arguments for call";
        return false;
      }
    }
  }

  for (size_t i = 0; i < info.params.size() && i < call_expr.args.size(); ++i) {
    TypeRef expected;
    if (!SubstituteTypeParams(info.params[i], mapping, &expected)) return false;
    TypeRef actual;
    if (!InferExprType(call_expr.args[i], ctx, scopes, current_artifact, &actual)) continue;
    if (!TypesCompatibleForExpr(expected, actual, call_expr.args[i])) {
      if (error) *error = "call argument type mismatch";
      return false;
    }
  }
  return true;
}

bool CheckAssignmentTarget(const Expr& target,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           std::string* error) {
  std::function<bool(const Expr&)> is_mutable_expr = [&](const Expr& expr) -> bool {
    if (expr.kind == ExprKind::Identifier) {
      if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
        if (local->type && local->type->pointer_depth > 0) {
          return !local->points_to_immutable;
        }
        return local->mutability == Mutability::Mutable;
      }
      auto global_it = ctx.globals.find(expr.text);
      if (global_it != ctx.globals.end()) {
        if (global_it->second->type.pointer_depth > 0) {
          auto imm_it = ctx.global_points_to_immutable.find(expr.text);
          if (imm_it != ctx.global_points_to_immutable.end()) {
            return !imm_it->second;
          }
          return true;
        }
        return global_it->second->mutability == Mutability::Mutable;
      }
      return true;
    }
    if (expr.kind == ExprKind::Member &&
        (expr.op == "." || expr.op == "->") &&
        !expr.children.empty()) {
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier) {
        if (base.text == "self") {
          const VarDecl* field = FindArtifactField(current_artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
          return true;
        }
        auto module_it = ctx.modules.find(base.text);
        if (module_it != ctx.modules.end()) {
          const VarDecl* var = FindModuleVar(module_it->second, expr.text);
          if (var) return var->mutability == Mutability::Mutable;
          return true;
        }
        if (const LocalInfo* local = FindLocal(scopes, base.text)) {
          auto artifact_it = ctx.artifacts.find(local->type ? local->type->name : "");
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          const VarDecl* field = FindArtifactField(artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
          return true;
        }
        auto global_it = ctx.globals.find(base.text);
        if (global_it != ctx.globals.end()) {
          auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          const VarDecl* field = FindArtifactField(artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
        }
      }
      return true;
    }
    if (expr.kind == ExprKind::Call) {
      CallTargetInfo info;
      if (!GetCallTargetInfo(expr.children[0], ctx, scopes, current_artifact, &info, nullptr)) return true;
      return info.return_mutability == Mutability::Mutable;
    }
    if (expr.kind == ExprKind::Index) {
      if (expr.children.empty()) return true;
      return is_mutable_expr(expr.children[0]);
    }
    return true;
  };
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
    if (ctx.functions.find(target.text) != ctx.functions.end()) {
      if (error) *error = "cannot assign to function: " + target.text;
      return false;
    }
    return true;
  }
  if (target.kind == ExprKind::Member &&
      (target.op == "." || target.op == "->") &&
      !target.children.empty()) {
    const Expr& base = target.children[0];
    if (!is_mutable_expr(base)) {
      if (error) *error = "cannot assign through immutable value";
      return false;
    }
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "self") {
        const VarDecl* field = FindArtifactField(current_artifact, target.text);
        if (!field && FindArtifactMethod(current_artifact, target.text)) {
          if (error) *error = "cannot assign to method: self." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: self." + target.text;
          return false;
        }
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, target.text);
        if (!field && FindArtifactMethod(artifact, target.text)) {
          if (error) *error = "cannot assign to method: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const VarDecl* field = FindModuleVar(module_it->second, target.text);
        if (!field && FindModuleFunc(module_it->second, target.text)) {
          if (error) *error = "cannot assign to function: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable module member: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
        if (error) *error = "cannot assign to immutable module member: " + module_name + "." + target.text;
        return false;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, target.text);
        if (!field && FindArtifactMethod(artifact, target.text)) {
          if (error) *error = "cannot assign to method: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
      }
    }
    return true;
  }
  if (target.kind == ExprKind::Index) {
    if (!target.children.empty() && !is_mutable_expr(target.children[0])) {
      if (error) *error = "cannot assign through immutable value";
      return false;
    }
    return true;
  }
  if (error) *error = "invalid assignment target";
  return false;
}

bool ValidateArtifactLiteral(const Expr& expr,
                             const ArtifactDecl* artifact,
                             const std::unordered_map<std::string, TypeRef>& type_mapping,
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
      TypeRef expected;
      if (!SubstituteTypeParams(field.type, type_mapping, &expected)) return false;
      if (!TypesCompatibleForExpr(expected, value_type, expr.children[i])) {
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
        TypeRef expected;
        if (!SubstituteTypeParams(it->second->type, type_mapping, &expected)) return false;
        if (!TypesCompatibleForExpr(expected, value_type, expr.field_values[i])) {
          if (error) *error = "artifact field type mismatch: " + name;
          return false;
        }
      }
    }
  }
  for (const auto& field : artifact->fields) {
    if (seen.find(field.name) == seen.end()) {
      if (!field.has_init_expr) {
        if (error) *error = "missing artifact field: " + field.name;
        return false;
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
            if (!TypesCompatibleForExpr(*expected_return, actual, stmt.expr)) {
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
      if (stmt.expr.kind != ExprKind::Switch &&
          !CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) {
        return false;
      }
      {
        TypeRef target_type;
        TypeRef value_type;
        bool have_target = InferExprType(stmt.target, ctx, scopes, current_artifact, &target_type);
        bool have_value = false;
        if (stmt.expr.kind == ExprKind::Switch) {
          have_value = AnalyzeSwitchExpr(stmt.expr,
                                         ctx,
                                         scopes,
                                         current_artifact,
                                         true,
                                         have_target ? &target_type : nullptr,
                                         &value_type,
                                         error,
                                         &type_params,
                                         expected_return,
                                         return_is_void,
                                         loop_depth);
        } else {
          have_value = InferExprType(stmt.expr, ctx, scopes, current_artifact, &value_type);
        }
        if (have_target && stmt.expr.kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(stmt.expr, target_type, error)) return false;
        }
        if (have_target && have_value && !TypesCompatibleForExpr(target_type, value_type, stmt.expr)) {
          if (error) *error = "assignment type mismatch";
          return false;
        }
        if (have_target && have_value && stmt.assign_op != "=") {
          std::string op = stmt.assign_op;
          if (!op.empty() && op.back() == '=') op.pop_back();
          TypeRef rhs_for_op;
          if (!CloneTypeRef(value_type, &rhs_for_op)) return false;
          if (!TypeEquals(target_type, rhs_for_op) &&
              IsLiteralCompatibleWithScalarType(stmt.expr, target_type)) {
            if (!CloneTypeRef(target_type, &rhs_for_op)) return false;
          }
          if (!CheckCompoundAssignOp(op, target_type, rhs_for_op, error)) return false;
        }
        if (have_target &&
            !target_type.dims.empty() &&
            target_type.dims.front().is_list &&
            IsListLiteralExpr(stmt.expr)) {
          if (!CheckListLiteralElementTypes(stmt.expr,
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            target_type,
                                            error)) {
            return false;
          }
        } else if (have_target &&
                   !target_type.dims.empty() &&
                   !target_type.dims.front().is_list &&
                   IsPositionalBraceLiteralExpr(stmt.expr)) {
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
        } else if (have_target &&
                   ((!target_type.dims.empty() &&
                     target_type.dims.front().is_list &&
                     IsPositionalBraceLiteralExpr(stmt.expr)) ||
                    (!target_type.dims.empty() &&
                     !target_type.dims.front().is_list &&
                     IsListLiteralExpr(stmt.expr)) ||
                    (target_type.dims.empty() &&
                     IsListLiteralExpr(stmt.expr)))) {
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
        if (!ValidateVarInitExpr(stmt.var_decl,
                                 ctx,
                                 scopes,
                                 current_artifact,
                                 true,
                                 error,
                                 &type_params,
                                 expected_return,
                                 return_is_void,
                                 loop_depth)) {
          return false;
        }
        if (stmt.var_decl.type.pointer_depth > 0) {
          bool known = false;
          bool points_to_immutable = false;
          GetPointerImmutabilityFromExpr(stmt.var_decl.init_expr,
                                         ctx,
                                         scopes,
                                         current_artifact,
                                         &known,
                                         &points_to_immutable);
          auto local_it = scopes.back().find(stmt.var_decl.name);
          if (local_it != scopes.back().end() && known) {
            local_it->second.points_to_immutable = points_to_immutable;
          }
        }
        std::string manifest_module;
        if (GetDlOpenManifestModule(stmt.var_decl.init_expr, ctx, &manifest_module)) {
          auto local_it = scopes.back().find(stmt.var_decl.name);
          if (local_it != scopes.back().end()) {
            local_it->second.dl_module = manifest_module;
          }
        }
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
      if (stmt.has_loop_var_decl) {
        Stmt var_stmt;
        var_stmt.kind = StmtKind::VarDecl;
        var_stmt.var_decl = stmt.loop_var_decl;
        if (!CheckStmt(var_stmt,
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
      if (!CheckExpr(stmt.loop_iter, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_step, ctx, scopes, current_artifact, error)) return false;
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

bool CheckArrayLiteralShape(const Expr& expr,
                            const std::vector<TypeDim>& dims,
                            size_t dim_index,
                            std::string* error) {
  if (dim_index >= dims.size()) return true;
  const TypeDim& dim = dims[dim_index];
  if (!dim.has_size) return true;

  if (!IsPositionalBraceLiteralExpr(expr)) {
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
  if (!IsPositionalBraceLiteralExpr(expr)) return true;
  if (dims.empty()) return true;

  if (dim_index + 1 >= dims.size()) {
    for (const auto& child : expr.children) {
      VarDecl temp;
      temp.name = "__array_element";
      temp.type = element_type;
      temp.has_init_expr = true;
      temp.init_expr = child;
      if (!ValidateVarInitExpr(temp, ctx, scopes, current_artifact, false, error)) {
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
  if (!IsListLiteralExpr(expr)) return true;
  if (list_type.dims.empty()) return true;
  if (!list_type.dims.front().is_list) return true;

  TypeRef element_type;
  if (!CloneTypeRef(list_type, &element_type)) return false;
  if (!element_type.dims.empty()) {
    element_type.dims.erase(element_type.dims.begin());
  }

  for (const auto& child : expr.children) {
    VarDecl temp;
    temp.name = "__list_element";
    temp.type = element_type;
    temp.has_init_expr = true;
    temp.init_expr = child;
    if (!ValidateVarInitExpr(temp, ctx, scopes, current_artifact, false, error)) {
      if (error) *error = "list literal element type mismatch";
      return false;
    }
  }
  return true;
}

bool ValidateVarInitExpr(const VarDecl& var,
                         const ValidateContext& ctx,
                         const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                         const ArtifactDecl* current_artifact,
                         bool require_switch_returns,
                         std::string* error,
                         const std::unordered_set<std::string>* type_params,
                         const TypeRef* expected_return,
                         bool return_is_void,
                         int loop_depth) {
  if (!var.has_init_expr) return true;
  if (var.init_expr.kind != ExprKind::Switch &&
      !CheckExpr(var.init_expr, ctx, scopes, current_artifact, error)) {
    return false;
  }
  if (var.init_expr.kind == ExprKind::FnLiteral) {
    if (!CheckFnLiteralAgainstType(var.init_expr, var.type, error)) {
      return false;
    }
  }
  if (!var.type.dims.empty() &&
      var.type.dims.front().is_list &&
      IsListLiteralExpr(var.init_expr)) {
    if (!CheckListLiteralElementTypes(var.init_expr,
                                      ctx,
                                      scopes,
                                      current_artifact,
                                      var.type,
                                      error)) {
      return false;
    }
  } else if (!var.type.dims.empty() &&
             !var.type.dims.front().is_list &&
             IsPositionalBraceLiteralExpr(var.init_expr)) {
    if (!CheckArrayLiteralShape(var.init_expr, var.type.dims, 0, error)) {
      return false;
    }
    TypeRef base_type;
    if (!CloneTypeRef(var.type, &base_type)) return false;
    base_type.dims.clear();
    if (!CheckArrayLiteralElementTypes(var.init_expr,
                                       ctx,
                                       scopes,
                                       current_artifact,
                                       var.type.dims,
                                       0,
                                       base_type,
                                       error)) {
      return false;
    }
  } else if ((!var.type.dims.empty() &&
              var.type.dims.front().is_list &&
              IsPositionalBraceLiteralExpr(var.init_expr)) ||
             (!var.type.dims.empty() &&
              !var.type.dims.front().is_list &&
              IsListLiteralExpr(var.init_expr)) ||
             (var.type.dims.empty() && IsListLiteralExpr(var.init_expr))) {
    if (error) *error = "array/list literal requires array or list type";
    return false;
  }
  TypeRef init_type;
  bool have_init_type = false;
  if (var.init_expr.kind == ExprKind::Switch) {
    if (AnalyzeSwitchExpr(var.init_expr,
                          ctx,
                          scopes,
                          current_artifact,
                          require_switch_returns,
                          &var.type,
                          &init_type,
                          error,
                          type_params,
                          expected_return,
                          return_is_void,
                          loop_depth)) {
      have_init_type = true;
    } else {
      return false;
    }
  } else if (InferExprType(var.init_expr, ctx, scopes, current_artifact, &init_type)) {
    have_init_type = true;
  }
  if (have_init_type) {
    if (!TypesCompatibleForExpr(var.type, init_type, var.init_expr)) {
      if (error) *error = "initializer type mismatch";
      return false;
    }
  }
  if (var.init_expr.kind == ExprKind::ArtifactLiteral && var.type.dims.empty()) {
    auto artifact_it = ctx.artifacts.find(var.type.name);
    if (artifact_it != ctx.artifacts.end()) {
      std::unordered_map<std::string, TypeRef> mapping;
      if (!BuildArtifactTypeParamMap(var.type, artifact_it->second, &mapping, error)) {
        return false;
      }
      if (!ValidateArtifactLiteral(var.init_expr,
                                   artifact_it->second,
                                   mapping,
                                   ctx,
                                   scopes,
                                   current_artifact,
                                   error)) {
        return false;
      }
    }
  } else if (var.type.dims.empty() && IsListLiteralExpr(var.init_expr)) {
    if (error) *error = "array/list literal requires array or list type";
    return false;
  }
  return true;
}

bool GetSwitchBranchValueExpr(const SwitchBranch& branch,
                              bool require_explicit_return,
                              const Expr** out_expr,
                              std::string* error) {
  if (!out_expr) return false;
  *out_expr = nullptr;
  if (branch.is_block) {
    if (branch.block.empty() ||
        branch.block.back().kind != StmtKind::Return ||
        !branch.block.back().has_return_expr) {
      if (error) *error = "switch branch block must end with a return value";
      return false;
    }
    *out_expr = &branch.block.back().expr;
    return true;
  }
  if (!branch.has_inline_value) {
    if (error) *error = "switch branch requires a value";
    return false;
  }
  if (require_explicit_return && !branch.is_explicit_return) {
    if (error) *error = "assigning switch branches must use 'return'";
    return false;
  }
  *out_expr = &branch.value;
  return true;
}

bool AnalyzeSwitchExpr(const Expr& expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       bool require_explicit_return,
                       const TypeRef* expected_type,
                       TypeRef* out_type,
                       std::string* error,
                       const std::unordered_set<std::string>* type_params,
                       const TypeRef* expected_return,
                       bool return_is_void,
                       int loop_depth) {
  if (expr.kind != ExprKind::Switch || expr.children.empty()) {
    if (error) *error = "invalid switch expression";
    return false;
  }
  if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
  if (expr.switch_branches.empty()) {
    if (error) *error = "switch requires at least one branch";
    return false;
  }
  const std::unordered_set<std::string> empty_type_params;
  const auto& branch_type_params = type_params ? *type_params : empty_type_params;
  size_t default_count = 0;
  bool has_type = false;
  TypeRef common;
  for (const auto& branch : expr.switch_branches) {
    if (branch.is_default) {
      default_count++;
    } else {
      if (!CheckExpr(branch.condition, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(branch.condition, ctx, scopes, current_artifact, error)) return false;
    }
    const Expr* value_expr = nullptr;
    if (!GetSwitchBranchValueExpr(branch, require_explicit_return, &value_expr, error)) return false;
    if (!value_expr) return false;

    auto branch_scopes = scopes;
    if (branch.is_block) {
      branch_scopes.emplace_back();
      for (size_t stmt_index = 0; stmt_index + 1 < branch.block.size(); ++stmt_index) {
        if (!CheckStmt(branch.block[stmt_index],
                       ctx,
                       branch_type_params,
                       expected_return,
                       return_is_void,
                       loop_depth,
                       branch_scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
    }
    const auto& value_scopes = branch.is_block ? branch_scopes : scopes;

    if (expected_type) {
      VarDecl temp;
      temp.name = "__switch_branch";
      temp.type = *expected_type;
      temp.has_init_expr = true;
      temp.init_expr = *value_expr;
      if (!ValidateVarInitExpr(temp,
                               ctx,
                               value_scopes,
                               current_artifact,
                               false,
                               error)) {
        return false;
      }
    } else {
      if (!CheckExpr(*value_expr, ctx, value_scopes, current_artifact, error)) return false;
      TypeRef value_type;
      if (!InferExprType(*value_expr, ctx, value_scopes, current_artifact, &value_type)) {
        if (error && error->empty()) *error = "switch branch type mismatch";
        return false;
      }
      if (!has_type) {
        if (!CloneTypeRef(value_type, &common)) return false;
        has_type = true;
      } else {
        if (!TypesCompatibleForExpr(common, value_type, *value_expr)) {
          if (error) *error = "switch branch type mismatch";
          return false;
        }
      }
    }
  }
  if (default_count != 1) {
    if (error) *error = "switch must have exactly one default branch";
    return false;
  }
  if (out_type) {
    if (expected_type) {
      if (!CloneTypeRef(*expected_type, out_type)) return false;
    } else {
      if (!CloneTypeRef(common, out_type)) return false;
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
    if (cond_type.pointer_depth != 0 || !IsBoolTypeName(cond_type.name)) {
      if (error) *error = "condition must be bool";
      return false;
    }
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

  return CheckUnaryOpTypeRules(expr.op, operand, expr.children[0], error);
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

  return CheckBinaryOpTypeRules(expr.op, lhs, rhs, expr.children[0], expr.children[1], error);
}

bool LooksLikeFnLiteralStart(const std::vector<Token>& tokens, size_t index) {
  if (index >= tokens.size() || tokens[index].kind != TokenKind::LParen) return false;
  ++index;
  if (index < tokens.size() && tokens[index].kind == TokenKind::RParen) {
    ++index;
    return index < tokens.size() && tokens[index].kind == TokenKind::LBrace;
  }
  bool expect_name = true;
  while (index < tokens.size()) {
    if (expect_name) {
      if (tokens[index].kind != TokenKind::Identifier) return false;
      expect_name = false;
      ++index;
      continue;
    }
    if (tokens[index].kind == TokenKind::Comma) {
      expect_name = true;
      ++index;
      continue;
    }
    if (tokens[index].kind == TokenKind::RParen) {
      ++index;
      return index < tokens.size() && tokens[index].kind == TokenKind::LBrace;
    }
    return false;
  }
  return false;
}

bool ContainsNestedFnLiteralTokens(const std::vector<Token>& tokens) {
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    if ((tokens[i].kind == TokenKind::Assign || tokens[i].kind == TokenKind::KwReturn ||
         tokens[i].kind == TokenKind::Comma || tokens[i].kind == TokenKind::LBracket) &&
        LooksLikeFnLiteralStart(tokens, i + 1)) {
      return true;
    }
  }
  return false;
}

bool CheckFnLiteralAgainstType(const Expr& fn_expr,
                               const TypeRef& target_type,
                               std::string* error) {
  if (!target_type.is_proc) {
    if (error) *error = "fn literal requires procedure type";
    return false;
  }
  if (fn_expr.fn_params.size() != target_type.proc_params.size()) {
    if (error) {
      *error = "fn literal parameter count mismatch: expected " +
               std::to_string(target_type.proc_params.size()) + ", got " +
               std::to_string(fn_expr.fn_params.size());
    }
    return false;
  }
  for (size_t i = 0; i < fn_expr.fn_params.size(); ++i) {
    if (fn_expr.fn_params[i].type.name.empty()) continue;
    if (!TypeEquals(fn_expr.fn_params[i].type, target_type.proc_params[i])) {
      if (error) *error = "fn literal parameter type mismatch";
      return false;
    }
  }
  if (ContainsNestedFnLiteralTokens(fn_expr.fn_body_tokens)) {
    if (error) *error = "nested fn literals are not supported";
    return false;
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
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        return true;
      }
      if (expr.text == "System") {
        if (IsReservedModuleEnabled(ctx, "Math") || IsReservedModuleEnabled(ctx, "IO") ||
            IsReservedModuleEnabled(ctx, "Time") || IsReservedModuleEnabled(ctx, "DL") ||
            IsReservedModuleEnabled(ctx, "OS") || IsReservedModuleEnabled(ctx, "File") ||
            IsReservedModuleEnabled(ctx, "Log") || IsReservedModuleEnabled(ctx, "Thread") ||
            IsReservedModuleEnabled(ctx, "Channel") || IsReservedModuleEnabled(ctx, "Random") ||
            IsReservedModuleEnabled(ctx, "Env") || IsReservedModuleEnabled(ctx, "Path") ||
            IsReservedModuleEnabled(ctx, "FS")) {
          return true;
        }
      }
      if (expr.text == "len" ||
          IsPrimitiveCastName(expr.text) || GetAtCastTargetName(expr.text, nullptr)) {
        return true;
      }
      if (FindLocal(scopes, expr.text)) return true;
      if (current_artifact && IsArtifactMemberName(current_artifact, expr.text)) {
        if (error) *error = "artifact members must be accessed via self: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (ctx.top_level.find(expr.text) != ctx.top_level.end()) {
        if (ctx.modules.find(expr.text) != ctx.modules.end()) {
          if (error) *error = "module is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        if (ctx.artifacts.find(expr.text) != ctx.artifacts.end()) {
          if (error) *error = "type is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        if (ctx.enum_types.find(expr.text) != ctx.enum_types.end()) {
          if (error) *error = "enum type is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        return true;
      }
      if (IsReservedModuleEnabled(ctx, expr.text)) {
        if (error) *error = "module is not a value: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (ctx.externs_by_module.find(expr.text) != ctx.externs_by_module.end()) {
        return true;
      }
      if (ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
        if (error) *error = "unqualified enum value: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (ResolveUsingReservedCallTarget(ctx, expr.text, nullptr, nullptr)) {
        return true;
      }
      if (error) *error = "undeclared identifier: " + expr.text;
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    case ExprKind::Literal:
      return true;
    case ExprKind::FormatString: {
      size_t placeholder_count = 0;
      if (!CountFormatPlaceholders(expr.text, &placeholder_count, error)) return false;
      if (placeholder_count != expr.args.size()) {
        if (error) {
          *error = "format placeholder count mismatch: expected " +
                   std::to_string(placeholder_count) + ", got " +
                   std::to_string(expr.args.size());
        }
        return false;
      }
      for (const auto& arg : expr.args) {
        if (!CheckExpr(arg, ctx, scopes, current_artifact, error)) return false;
        TypeRef arg_type;
        if (!InferExprType(arg, ctx, scopes, current_artifact, &arg_type)) {
          if (error && error->empty()) *error = "format expects scalar arguments";
          return false;
        }
        if (arg_type.pointer_depth != 0 ||
            arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
          if (error) *error = "format expects scalar arguments";
          return false;
        }
        if (!(IsNumericTypeName(arg_type.name) ||
              IsBoolTypeName(arg_type.name) ||
              arg_type.name == "string")) {
          if (error) *error = "format supports numeric, bool, or string";
          return false;
        }
      }
      return true;
    }
    case ExprKind::Unary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (expr.op == "++" || expr.op == "--" || expr.op == "post++" || expr.op == "post--") {
        if (!CheckAssignmentTarget(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      }
      return CheckUnaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Binary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) {
        if (!CheckAssignmentTarget(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckExpr(expr.children[1], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) {
        TypeRef target_type;
        TypeRef value_type;
        bool have_target = InferExprType(expr.children[0], ctx, scopes, current_artifact, &target_type);
        bool have_value = InferExprType(expr.children[1], ctx, scopes, current_artifact, &value_type);
        if (expr.op != "=" && have_target && have_value) {
          TypeRef rhs_for_op;
          if (!CloneTypeRef(value_type, &rhs_for_op)) return false;
          if (!TypeEquals(target_type, rhs_for_op) &&
              IsLiteralCompatibleWithScalarType(expr.children[1], target_type)) {
            if (!CloneTypeRef(target_type, &rhs_for_op)) return false;
          }
          if (!CheckCompoundAssignOp(expr.op, target_type, rhs_for_op, error)) return false;
          return true;
        }
        if (have_target && expr.children[1].kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(expr.children[1], target_type, error)) return false;
        }
        if (have_target &&
            !target_type.dims.empty() &&
            target_type.dims.front().is_list &&
            IsListLiteralExpr(expr.children[1])) {
          if (!CheckListLiteralElementTypes(expr.children[1],
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            target_type,
                                            error)) {
            return false;
          }
        } else if (have_target &&
                   !target_type.dims.empty() &&
                   !target_type.dims.front().is_list &&
                   IsPositionalBraceLiteralExpr(expr.children[1])) {
          if (!CheckArrayLiteralShape(expr.children[1], target_type.dims, 0, error)) {
            return false;
          }
          TypeRef base_type;
          if (!CloneTypeRef(target_type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(expr.children[1],
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             target_type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
        } else if (have_target &&
                   ((!target_type.dims.empty() &&
                     target_type.dims.front().is_list &&
                     IsPositionalBraceLiteralExpr(expr.children[1])) ||
                    (!target_type.dims.empty() &&
                     !target_type.dims.front().is_list &&
                     IsListLiteralExpr(expr.children[1])) ||
                    (target_type.dims.empty() &&
                     IsListLiteralExpr(expr.children[1])))) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
        if (have_target && have_value &&
            !TypesCompatibleForExpr(target_type, value_type, expr.children[1])) {
          if (error) *error = "assignment type mismatch";
          return false;
        }
        return true;
      }
      return CheckBinaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Call:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      for (const auto& arg : expr.args) {
        if (!CheckExpr(arg, ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckCallTarget(expr.children[0], expr.args.size(), ctx, scopes, current_artifact, error)) return false;
      if (IsIoPrintCallExpr(expr.children[0], ctx)) {
        if (expr.args.empty()) {
          if (error) *error = "call argument count mismatch for IO." + expr.children[0].text;
          return false;
        }
        if (expr.args.size() == 1) {
          TypeRef arg_type;
          if (!InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
            if (error && error->empty()) *error = "IO.print expects scalar argument";
            return false;
          }
          if (arg_type.pointer_depth != 0 ||
              arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
            if (error) *error = "IO.print expects scalar argument";
            return false;
          }
          if (!(IsNumericTypeName(arg_type.name) ||
                IsBoolTypeName(arg_type.name) ||
                arg_type.name == "char" ||
                arg_type.name == "string")) {
            if (error) *error = "IO.print supports numeric, bool, char, or string";
            return false;
          }
        } else {
          if (!(expr.args[0].kind == ExprKind::Literal &&
                expr.args[0].literal_kind == LiteralKind::String)) {
            if (error) *error = "IO.print format call expects string literal as first argument";
            return false;
          }
          size_t placeholder_count = 0;
          if (!CountFormatPlaceholders(expr.args[0].text, &placeholder_count, error)) {
            return false;
          }
          const size_t value_count = expr.args.size() - 1;
          if (placeholder_count != value_count) {
            if (error) {
              *error = "IO.print format placeholder count mismatch: expected " +
                       std::to_string(placeholder_count) + ", got " +
                       std::to_string(value_count);
            }
            return false;
          }
          for (size_t i = 1; i < expr.args.size(); ++i) {
            TypeRef arg_type;
            if (!InferExprType(expr.args[i], ctx, scopes, current_artifact, &arg_type)) {
              if (error && error->empty()) *error = "IO.print format expects scalar arguments";
              return false;
            }
            if (arg_type.pointer_depth != 0 ||
                arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
              if (error) *error = "IO.print format expects scalar arguments";
              return false;
            }
            if (!(IsNumericTypeName(arg_type.name) ||
                  IsBoolTypeName(arg_type.name) ||
                  arg_type.name == "char" ||
                  arg_type.name == "string")) {
              if (error) *error = "IO.print supports numeric, bool, char, or string";
              return false;
            }
          }
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier && expr.children[0].text == "len") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for len: expected 1, got " +
                              std::to_string(expr.args.size());
          return false;
        }
        TypeRef arg_type;
        if (InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (arg_type.dims.empty() && arg_type.name != "string") {
            if (error) *error = "len expects array, list, or string argument";
            return false;
          }
        } else {
          if (error && error->empty()) *error = "len expects array, list, or string argument";
          return false;
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier) {
        std::string cast_target;
        const bool is_at_cast = GetAtCastTargetName(expr.children[0].text, &cast_target);
        if (!is_at_cast && IsPrimitiveCastName(expr.children[0].text)) {
          if (error) {
            *error = "primitive cast syntax requires '@': use @" +
                     expr.children[0].text + "(value)";
          }
          return false;
        }
        if (is_at_cast) {
          if (expr.args.size() != 1) {
            if (error) *error = "call argument count mismatch for " + cast_target + ": expected 1, got " +
                                std::to_string(expr.args.size());
            return false;
          }
          TypeRef arg_type;
          if (!InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
            if (error && error->empty()) *error = cast_target + " cast expects scalar argument";
            return false;
          }
          if (arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
            if (error) *error = cast_target + " cast expects scalar argument";
            return false;
          }
          if (cast_target == "string") {
            if (!IsNumericTypeName(arg_type.name) && !IsBoolTypeName(arg_type.name)) {
              if (error) *error = "string cast expects numeric or bool argument";
              return false;
            }
          } else if (IsStringTypeName(arg_type.name) && !(cast_target == "i32" || cast_target == "f64")) {
            if (error) *error = cast_target + " cast from string is unsupported";
            return false;
          }
        }
      }
      {
        bool is_using_io_print = false;
        if (expr.children[0].kind == ExprKind::Identifier && IsIoPrintName(expr.children[0].text)) {
          std::string using_module;
          is_using_io_print = ResolveUsingReservedCallTarget(ctx, expr.children[0].text, &using_module, nullptr) &&
                              using_module == "IO";
        }
        if (!IsIoPrintCallExpr(expr.children[0], ctx) && !is_using_io_print &&
            !(expr.children[0].kind == ExprKind::Identifier &&
              (expr.children[0].text == "len" ||
               GetAtCastTargetName(expr.children[0].text, nullptr)))) {
          if (!CheckCallArgTypes(expr, ctx, scopes, current_artifact, error)) return false;
        }
      }
      return true;
    case ExprKind::Member: {
      const bool is_dot = (expr.op == ".");
      const bool is_ptr = (expr.op == "->");
      if (is_dot && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            IsIoPrintCallExpr(expr, ctx)) {
          return true;
        }
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end()) {
          auto members_it = ctx.enum_members_by_type.find(base.text);
          if (members_it != ctx.enum_members_by_type.end()) {
            if (members_it->second.find(expr.text) == members_it->second.end()) {
              if (error) *error = "unknown enum member: " + base.text + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
          }
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
            auto mod_it = ctx.externs_by_module.find(dl_module);
            if (mod_it != ctx.externs_by_module.end() &&
                mod_it->second.find(expr.text) != mod_it->second.end()) {
              return true;
            }
          }
          auto module_it = ctx.modules.find(base.text);
          if (module_it != ctx.modules.end()) {
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text)) {
              if (error) {
                *error = UnknownMemberErrorWithSuggestion(
                    base.text, expr.text, ModuleMembers(module_it->second));
              }
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
            return true;
          }
          std::string module_name;
          if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
            TypeRef var_type;
            CallTargetInfo info;
            if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
                GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
              return true;
            }
            if (error) {
              std::string resolved;
              ResolveReservedModuleName(ctx, module_name, &resolved);
              *error = UnknownMemberErrorWithSuggestion(
                  module_name, expr.text, ReservedModuleMembers(resolved.empty() ? module_name : resolved));
            }
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
        }
      }
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (is_dot && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
            auto mod_it = ctx.externs_by_module.find(dl_module);
            if (mod_it != ctx.externs_by_module.end() &&
                mod_it->second.find(expr.text) != mod_it->second.end()) {
              return true;
            }
          }
          auto module_it = ctx.modules.find(base.text);
          if (module_it != ctx.modules.end()) {
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text)) {
              if (error) {
                *error = UnknownMemberErrorWithSuggestion(
                    base.text, expr.text, ModuleMembers(module_it->second));
              }
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
            return true;
          }
          std::string module_name;
          if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
            TypeRef var_type;
            CallTargetInfo info;
            if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
                GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
              return true;
            }
            if (error) {
              std::string resolved;
              ResolveReservedModuleName(ctx, module_name, &resolved);
              *error = UnknownMemberErrorWithSuggestion(
                  module_name, expr.text, ReservedModuleMembers(resolved.empty() ? module_name : resolved));
            }
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
        }
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
          TypeRef var_type;
          CallTargetInfo info;
          if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
              GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
            return true;
          }
          if (error) {
            std::string resolved;
            ResolveReservedModuleName(ctx, module_name, &resolved);
            *error = UnknownMemberErrorWithSuggestion(
                module_name, expr.text, ReservedModuleMembers(resolved.empty() ? module_name : resolved));
          }
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        TypeRef base_type;
        if (InferExprType(base, ctx, scopes, current_artifact, &base_type)) {
          if (!base_type.dims.empty() && base_type.dims.front().is_list &&
              IsListMethodName(expr.text)) {
            return true;
          }
          auto artifact_it = ctx.artifacts.find(base_type.name);
          if (artifact_it != ctx.artifacts.end()) {
            const ArtifactDecl* artifact = artifact_it->second;
            if (!FindArtifactField(artifact, expr.text) &&
                !FindArtifactMethod(artifact, expr.text)) {
              if (error) *error = "unknown artifact member: " + base_type.name + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
          }
        }
      }
      if (is_ptr && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        TypeRef base_type;
        if (InferExprType(base, ctx, scopes, current_artifact, &base_type)) {
          if (base_type.pointer_depth == 0) {
            if (error) *error = "pointer member access requires a pointer type";
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
          TypeRef pointee = base_type;
          pointee.pointer_depth -= 1;
          auto artifact_it = ctx.artifacts.find(pointee.name);
          if (artifact_it != ctx.artifacts.end()) {
            const ArtifactDecl* artifact = artifact_it->second;
            if (!FindArtifactField(artifact, expr.text) &&
                !FindArtifactMethod(artifact, expr.text)) {
              if (error) *error = "unknown artifact member: " + pointee.name + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
          } else {
            if (error) *error = "pointer member access requires artifact type";
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
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
    }
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
    case ExprKind::Switch:
      return AnalyzeSwitchExpr(expr, ctx, scopes, current_artifact, false, nullptr, nullptr, error);
  }
  return true;
}

bool StmtsReturn(const std::vector<Stmt>& stmts) {
  return Simple::Lang::TAST::AnalyzeBlockFlow(stmts).always_returns;
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
  const bool is_main = (fn.name == "main" && fn.return_type.name == "i32");
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
  if (!return_is_void && !StmtsReturn(fn.body) && !is_main) {
    if (error) *error = "non-void function does not return on all paths";
    return false;
  }
  return true;
}

} // namespace

bool ValidateProgram(const Program& program, std::string* error) {
  ValidateContext ctx;
  std::vector<std::unordered_map<std::string, LocalInfo>> empty_scopes;
  empty_scopes.emplace_back();
  if (program.decls.empty() && program.top_level_stmts.empty()) {
    if (error) *error = "program has no declarations or top-level statements";
    return false;
  }
  for (const auto& decl : program.decls) {
    const std::string* name_ptr = nullptr;
    switch (decl.kind) {
      case DeclKind::ModuleHeader:
        break;
      case DeclKind::Import:
      {
        if (decl.import_decl.is_using) {
          const auto alias_it = ctx.reserved_import_aliases.find(decl.import_decl.path);
          if (alias_it == ctx.reserved_import_aliases.end()) {
            if (error) *error = "using requires prior import: " + decl.import_decl.path;
            return false;
          }
          ctx.reserved_imports.insert(alias_it->second);
          ctx.using_reserved_modules.insert(alias_it->second);
          break;
        }
        std::string canonical_import;
        if (!CanonicalizeReservedImportPath(decl.import_decl.path, &canonical_import)) {
          if (error) *error = "unsupported import path: " + decl.import_decl.path;
          return false;
        }
        ctx.reserved_imports.insert(canonical_import);
        if (decl.import_decl.has_alias && !decl.import_decl.alias.empty()) {
          ctx.reserved_import_aliases[decl.import_decl.alias] = canonical_import;
        } else {
          ctx.reserved_import_aliases[decl.import_decl.path] = canonical_import;
        }
        break;
      }
      case DeclKind::Extern:
        if (decl.ext.has_module) {
          ctx.externs_by_module[decl.ext.module][decl.ext.name] = &decl.ext;
        } else {
          name_ptr = &decl.ext.name;
          ctx.externs[decl.ext.name] = &decl.ext;
        }
        break;
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
          ctx.enum_members_by_type[decl.enm.name] = std::move(local_members);
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
        if (decl.var.has_init_expr && decl.var.type.pointer_depth > 0) {
          std::vector<std::unordered_map<std::string, LocalInfo>> empty_scopes;
          bool known = false;
          bool points_to_immutable = false;
          GetPointerImmutabilityFromExpr(decl.var.init_expr,
                                         ctx,
                                         empty_scopes,
                                         nullptr,
                                         &known,
                                         &points_to_immutable);
          if (known) {
            ctx.global_points_to_immutable[decl.var.name] = points_to_immutable;
          }
        }
        break;
    }
    if (name_ptr && !ctx.top_level.insert(*name_ptr).second) {
      if (error) *error = "duplicate top-level declaration: " + *name_ptr;
      return false;
    }
  }

  if (!program.top_level_stmts.empty()) {
    std::vector<std::unordered_map<std::string, LocalInfo>> scopes;
    scopes.emplace_back();
    std::unordered_set<std::string> type_params;
    TypeRef script_return;
    script_return.name = "i32";
    for (const auto& stmt : program.top_level_stmts) {
      if (stmt.kind == StmtKind::Return) {
        if (error) *error = "top-level return is not allowed";
        return false;
      }
      if (!CheckStmt(stmt,
                     ctx,
                     type_params,
                     &script_return,
                     false,
                     0,
                     scopes,
                     nullptr,
                     error)) {
        if (error && !error->empty()) {
          *error = "in top-level script: " + *error;
        }
        return false;
      }
    }
  }

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case DeclKind::ModuleHeader:
      case DeclKind::Import:
        break;
      case DeclKind::Extern:
        {
          std::unordered_set<std::string> param_names;
          std::unordered_set<std::string> type_params;
          if (!CheckTypeRef(decl.ext.return_type, ctx, type_params, TypeUse::Return, error)) return false;
          if (!IsSupportedDlAbiType(decl.ext.return_type, ctx, true)) {
            if (error) *error = "extern ABI return type is not supported";
            return false;
          }
          for (const auto& param : decl.ext.params) {
            if (!param_names.insert(param.name).second) {
              if (error) *error = "duplicate extern parameter name: " + param.name;
              return false;
            }
            if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
            if (!IsSupportedDlAbiType(param.type, ctx, false)) {
              if (error) *error = "extern ABI parameter type is not supported";
              return false;
            }
          }
        }
        break;
      case DeclKind::Function:
        {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(decl.func.generics, &type_params, error)) return false;
          if (!CheckFunctionBody(decl.func, ctx, type_params, nullptr, error)) {
            if (error && !error->empty()) {
              *error = "in function '" + decl.func.name + "': " + *error;
            }
            return false;
          }
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
            if (field.has_init_expr) {
              if (!ValidateVarInitExpr(field,
                                       ctx,
                                       empty_scopes,
                                       nullptr,
                                       true,
                                       error)) {
                return false;
              }
            }
          }
          for (const auto& method : decl.artifact.methods) {
            if (!names.insert(method.name).second) {
              if (error) *error = "duplicate artifact member: " + method.name;
              return false;
            }
          }
          for (const auto& method : decl.artifact.methods) {
            std::unordered_set<std::string> method_params;
            if (!CollectTypeParamsMerged(decl.artifact.generics,
                                         method.generics,
                                         &method_params,
                                         error)) {
              return false;
            }
            if (!CheckFunctionBody(method, ctx, method_params, &decl.artifact, error)) {
              if (error && !error->empty()) {
                *error = "in function '" + decl.artifact.name + "." + method.name + "': " + *error;
              }
              return false;
            }
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
            if (var.has_init_expr) {
              if (!ValidateVarInitExpr(var,
                                       ctx,
                                       empty_scopes,
                                       nullptr,
                                       true,
                                       error)) {
                return false;
              }
            }
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
          if (!CheckFunctionBody(fn, ctx, type_params, nullptr, error)) {
            if (error && !error->empty()) {
              *error = "in function '" + decl.module.name + "." + fn.name + "': " + *error;
            }
            return false;
          }
        }
        break;
      case DeclKind::Enum:
      case DeclKind::Variable:
        if (decl.kind == DeclKind::Variable) {
          std::unordered_set<std::string> type_params;
          if (!CheckTypeRef(decl.var.type, ctx, type_params, TypeUse::Value, error)) return false;
          if (decl.var.has_init_expr) {
            if (!ValidateVarInitExpr(decl.var,
                                     ctx,
                                     empty_scopes,
                                     nullptr,
                                     true,
                                     error)) {
              return false;
            }
          }
        }
        break;
    }
  }

  return true;
}

bool ValidateProgramFromString(const std::string& text, std::string* error) {
  Program program;
  if (!CAST::ParseProgramFromString(text, &program, error)) return false;
  return ValidateProgram(program, error);
}

bool ValidateProgramDiagnostic(const Program& program,
                               Diagnostics::Diagnostic* diagnostic) {
  std::string error;
  if (ValidateProgram(program, &error)) return true;
  if (diagnostic) {
    *diagnostic = Diagnostics::MakeDiagnostic("E4001",
                                              Diagnostics::DiagnosticPhase::TAST,
                                              error);
  }
  return false;
}

bool ValidateProgramFromStringDiagnostic(const std::string& text,
                                         Diagnostics::Diagnostic* diagnostic) {
  Program program;
  std::string error;
  if (!CAST::ParseProgramFromString(text, &program, &error)) {
    if (diagnostic) {
      *diagnostic = Diagnostics::MakeDiagnostic("E2001",
                                                Diagnostics::DiagnosticPhase::CAST,
                                                error);
    }
    return false;
  }
  return ValidateProgramDiagnostic(program, diagnostic);
}

} // namespace Simple::Lang
