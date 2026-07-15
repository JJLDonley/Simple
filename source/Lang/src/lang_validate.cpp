#include "TAST/type_checker.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CAST/parser.h"
#include "lang_reserved.h"
#include "native/registry.h"
#include "RAST/import_graph.h"
#include "RAST/member_resolution.h"
#include "RAST/reserved_resolution.h"
#include "TAST/abi.h"
#include "TAST/calls.h"
#include "TAST/control_flow.h"
#include "TAST/expressions.h"
#include "TAST/generics.h"
#include "TAST/literals.h"
#include "TAST/mutability.h"
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
  LibraryModuleSet reserved_imports;
  LibraryModuleAliasMap reserved_import_aliases;
  std::unordered_set<std::string> using_reserved_modules;
  std::unordered_set<std::string> using_modules;
  std::unordered_set<std::string> imported_modules;
  std::unordered_map<const Expr*, std::vector<TypeRef>>* inferred_generic_calls = nullptr;
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

struct TaggedTypeInfo {
  TaggedArtifactKind kind = TaggedArtifactKind::None;
  const TypeRef* value_type = nullptr;
  const TypeRef* error_type = nullptr;
  const ArtifactDecl* artifact = nullptr;
};

const TypeRef* FindArtifactFieldType(const ArtifactDecl& artifact,
                                     const std::string& name) {
  for (const auto& field : artifact.fields) {
    if (field.name == name) return &field.type;
  }
  return nullptr;
}

bool ResolveTaggedType(const TypeRef& type,
                       const ValidateContext& ctx,
                       TaggedTypeInfo* out) {
  if (!out || type.pointer_depth != 0 || !type.dims.empty() || type.is_proc) return false;
  *out = {};
  if (TAST::IsOptionalType(type)) {
    out->kind = TaggedArtifactKind::Optional;
    out->value_type = TAST::OptionalValueType(type);
    return out->value_type != nullptr;
  }
  if (type.name == "Result" && type.type_args.size() == 2) {
    out->kind = TaggedArtifactKind::Result;
    out->value_type = &type.type_args[0];
    out->error_type = &type.type_args[1];
    return true;
  }
  const auto artifact_it = ctx.artifacts.find(type.name);
  if (artifact_it == ctx.artifacts.end() ||
      artifact_it->second->tagged_kind == TaggedArtifactKind::None) {
    return false;
  }
  out->artifact = artifact_it->second;
  out->kind = out->artifact->tagged_kind;
  out->value_type = FindArtifactFieldType(*out->artifact, "value");
  if (out->kind == TaggedArtifactKind::Result) {
    out->error_type = FindArtifactFieldType(*out->artifact, "error");
  }
  return out->value_type &&
         (out->kind != TaggedArtifactKind::Result || out->error_type);
}

bool IsSwitchPattern(const SwitchBranch& branch) {
  return branch.pattern_kind != SwitchPatternKind::None;
}

void PrefixErrorLocation(uint32_t line, uint32_t column, std::string* error) {
  if (!error || error->empty() || line == 0) return;
  *error = std::to_string(line) + ":" + std::to_string(column) + ": " + *error;
}

bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out);
bool TryGetNativeReservedModuleCallTarget(const std::string& resolved,
                                          const std::string& member,
                                          CallTargetInfo* out);
bool ResolveUsingReservedCallTarget(const ValidateContext& ctx,
                                    const std::string& member,
                                    std::string* out_module,
                                    CallTargetInfo* out);
bool ResolveUsingModuleExternCallTarget(const ValidateContext& ctx,
                                        const std::string& member,
                                        std::string* out_module,
                                        CallTargetInfo* out);
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
bool IsDirectFnLiteralCall(const Expr& expr) {
  return expr.kind == ExprKind::Call && !expr.children.empty() &&
         expr.children[0].kind == ExprKind::FnLiteral;
}
bool BuildDirectFnLiteralSignature(
    const Expr& call,
    const TypeRef& result_type,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const ArtifactDecl* current_artifact,
    TypeRef* signature,
    std::string* error);
bool ValidateFnLiteralBody(
    const Expr& expr,
    const TypeRef& signature,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& outer_scopes,
    const ArtifactDecl* current_artifact,
    std::string* error);
bool ValidateExprAgainstExpected(
    const Expr& expr,
    const TypeRef& expected,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const ArtifactDecl* current_artifact,
    std::string* error);
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

enum class TypeUse : uint8_t {
  Value,
  Return,
};

using RAST::FindArtifactField;
using RAST::FindArtifactMethod;
using RAST::FindModuleFunc;
using RAST::FindModuleVar;
using RAST::GetModuleNameFromExpr;
using RAST::GetReservedModuleVarType;
using RAST::IsArtifactMemberName;
using RAST::IsIoPrintName;
using RAST::ModuleMembers;
using RAST::NativeModuleNameForReserved;
using RAST::NormalizeDlMemberName;
using RAST::ReservedModuleMembers;
using RAST::UnknownMemberErrorWithSuggestion;
using TAST::AddLocal;
using TAST::ApplyTypeSubstitution;
using TAST::FindLocal;
using TAST::BuildArtifactTypeParamMap;
using TAST::BuildExplicitTypeArgMap;
using TAST::CheckCompoundAssignOp;
using TAST::CheckConditionType;
using TAST::CheckDlDynamicSignature;
using TAST::CheckFunctionCallArgs;
using TAST::CheckCallTypeArgCount;
using TAST::CheckArrayLiteralShape;
using TAST::CheckBinaryOpTypeRules;
using TAST::CheckExternAbiType;
using TAST::CheckFnLiteralAgainstType;
using TAST::CheckArtifactLiteralDuplicateNamedFields;
using TAST::CheckArtifactLiteralFieldSpecifiedOnce;
using TAST::CheckArtifactLiteralKnownField;
using TAST::CheckArtifactLiteralPositionalCount;
using TAST::CheckArtifactLiteralRequiredField;
using RAST::CheckUsingImportHasPriorAlias;
using TAST::CheckArrayListLiteralTargetShape;
using TAST::CheckFormatCallArgTypes;
using TAST::CheckEnumMemberValue;
using TAST::CheckFormatPlaceholderCount;
using TAST::CheckFunctionReturnFlow;
using TAST::CheckReturnStmtValuePresence;
using TAST::CheckPrimitiveCastArgType;
using TAST::CheckAssignTargetSelfName;
using TAST::CheckPrimitiveCastSyntaxName;
using TAST::CheckProgramHasDeclarationsOrTopLevelStatements;
using TAST::CheckReservedDlOpenArgTypes;
using TAST::CheckReservedFileCallArgTypes;
using TAST::CheckReservedIoBufferCallArgTypes;
using TAST::CheckIoPrintCallArgTypes;
using TAST::CheckIoPrintFormatTemplateArg;
using TAST::CheckReservedMathCallArgTypes;
using TAST::CheckReservedTimeCallArgTypes;
using TAST::CheckSingleArgCallCount;
using TAST::CheckSwitchExprShape;
using TAST::CheckTopLevelStmtAllowsReturn;
using TAST::CheckTypesCompatibleForExpr;
using TAST::CheckKnownTypeName;
using TAST::CheckProcTypeArgs;
using TAST::CheckTypeArgumentRules;
using TAST::CheckUnaryOpTypeRules;
using TAST::CheckUniqueNamedMember;
using TAST::CheckVoidTypeArgs;
using TAST::CheckUniqueParamName;
using TAST::CollectTypeParams;
using TAST::CollectTypeParamsMerged;
using TAST::CloneElementType;
using TAST::CloneTypeRef;
using TAST::CloneTypeVector;
using TAST::GetAtCastTargetName;
using TAST::GetSwitchBranchValueExpr;
using TAST::InferLiteralType;
using TAST::IsBuiltinCallIdentifierName;
using TAST::IsBinaryExpr;
using TAST::IsBuiltinValueIdentifierName;
using TAST::IsBoolTypeName;
using TAST::IsCallExpr;
using TAST::IsAddressableExpr;
using TAST::IsAddressOfExpr;
using TAST::IsAssignOp;
using TAST::IsFloatTypeName;
using TAST::IsIndexExpr;
using TAST::IsIntegerTypeName;
using TAST::IsLiteralCompatibleWithScalarType;
using TAST::IsListLiteralExpr;
using TAST::IsLenCompatibleType;
using TAST::IsListMethodName;
using TAST::IsMemberAccessExpr;
using TAST::IsNumericTypeName;
using TAST::IsPositionalBraceLiteralExpr;
using TAST::IsPrimitiveCastName;
using TAST::IsPrimitiveTypeName;
using TAST::IsScalarType;
using TAST::IsStringTypeName;
using TAST::IsSupportedDlAbiType;
using TAST::IsUnaryExpr;
using TAST::MakeListType;
using TAST::MakeSimpleType;
using TAST::NativeTypeToLangType;
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
  return RAST::IsIoPrintCallExpr(callee, ctx.reserved_imports, ctx.reserved_import_aliases);
}

bool IsReservedModuleEnabled(const ValidateContext& ctx, const std::string& name) {
  return RAST::IsReservedModuleEnabled(ctx.reserved_imports, ctx.reserved_import_aliases, name);
}

bool ResolveReservedModuleName(const ValidateContext& ctx,
                               const std::string& name,
                               std::string* out) {
  return RAST::ResolveReservedModuleName(ctx.reserved_imports, ctx.reserved_import_aliases, name, out);
}

bool ResolveReservedModuleId(const ValidateContext& ctx,
                             const std::string& name,
                             LibraryModuleId* out) {
  return RAST::ResolveReservedModuleId(ctx.reserved_imports, ctx.reserved_import_aliases, name, out);
}

bool IsLibraryRootEnabled(const ValidateContext& ctx, LibraryRoot root) {
  for (const auto& module : ctx.reserved_imports) {
    if (module.root == root) return true;
  }
  for (const auto& entry : ctx.reserved_import_aliases) {
    if (entry.second.root == root) return true;
  }
  return false;
}

bool GetDlOpenManifestModule(const Expr& expr,
                             const ValidateContext& ctx,
                             std::string* out_module) {
  return RAST::GetDlOpenManifestModule(expr,
                                       ctx.reserved_imports,
                                       ctx.reserved_import_aliases,
                                       ctx.externs_by_module,
                                       out_module);
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

bool ResolveReservedModuleVarType(const ValidateContext& ctx,
                                  const std::string& module,
                                  const std::string& member,
                                  TypeRef* out) {
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module, &resolved)) return false;
  return GetReservedModuleVarType(resolved, member, out);
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

bool LibraryTypeToTypeRef(const LibraryTypeSpec& spec, TypeRef* out) {
  if (!out || spec.name.empty()) return false;
  std::string name(spec.name);
  if (name.size() >= 2 && name.substr(name.size() - 2) == "[]") {
    *out = MakeListType(name.substr(0, name.size() - 2));
    return true;
  }
  *out = MakeSimpleType(name);
  return true;
}

bool ApplyLibrarySignatureToCallTarget(const LibrarySignatureSpec& signature,
                                       CallTargetInfo* out) {
  if (!out) return false;
  out->params.clear();
  out->type_params.clear();
  out->is_proc = signature.is_proc;
  out->type_params.reserve(signature.type_params.size());
  for (std::string_view type_param : signature.type_params) {
    out->type_params.emplace_back(type_param);
  }
  out->params.reserve(signature.params.size());
  for (const LibraryParamSpec& param : signature.params) {
    TypeRef param_type;
    if (!LibraryTypeToTypeRef(param.type, &param_type)) return false;
    out->params.push_back(std::move(param_type));
  }
  if (!LibraryTypeToTypeRef(signature.return_type, &out->return_type)) return false;
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

  const auto module_id = ParseCanonicalLibraryModule(resolved);
  if (!module_id) return TryGetNativeReservedModuleCallTarget(resolved, member, out);
  const auto signature = GetLibrarySignature(*module_id, member);
  if (!signature) return false;
  return ApplyLibrarySignatureToCallTarget(*signature, out);
}

bool ResolveUsingReservedCallTarget(const ValidateContext& ctx,
                                    const std::string& member,
                                    std::string* out_module,
                                    CallTargetInfo* out) {
  bool found = false;
  std::string found_module;
  CallTargetInfo found_info;
  for (const auto& module : ctx.using_reserved_modules) {
    CallTargetInfo candidate;
    if (!GetReservedModuleCallTarget(ctx, module, member, &candidate)) continue;
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

bool ResolveUsingModuleExternCallTarget(const ValidateContext& ctx,
                                        const std::string& member,
                                        std::string* out_module,
                                        CallTargetInfo* out) {
  bool found = false;
  std::string found_module;
  CallTargetInfo found_info;
  for (const auto& module : ctx.using_modules) {
    auto mod_it = ctx.externs_by_module.find(module);
    if (mod_it == ctx.externs_by_module.end()) continue;
    auto ext_it = mod_it->second.find(member);
    if (ext_it == mod_it->second.end()) continue;
    if (found) return false;
    found = true;
    found_module = module;
    found_info.params.clear();
    found_info.return_type = ext_it->second->return_type;
    found_info.return_mutability = ext_it->second->return_mutability;
    found_info.type_params.clear();
    found_info.is_proc = false;
    for (const auto& param : ext_it->second->params) found_info.params.push_back(param.type);
  }
  if (!found) return false;
  if (out_module) *out_module = std::move(found_module);
  if (out) *out = std::move(found_info);
  return true;
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
    if (!InferExprType(call_args[i], ctx, scopes, current_artifact, &arg_type)) continue;
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
      if (!CheckVoidTypeArgs(pointee, error)) {
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
    if (!CheckVoidTypeArgs(type, error)) {
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    return true;
  }

  if (type.name == kOptionalTypeInternalName && !type.is_optional_syntax) {
    if (error) *error = "internal optional type must use postfix '?' syntax";
    PrefixErrorLocation(type.line, type.column, error);
    return false;
  }

  const bool is_primitive = IsPrimitiveTypeName(type.name);
  const bool is_type_param = type_params.find(type.name) != type_params.end();
  size_t canonical_generic_arity = 0;
  const bool is_canonical_generic =
      TAST::CanonicalGenericTypeArity(type.name, &canonical_generic_arity);
  const bool is_user_type = ctx.top_level.find(type.name) != ctx.top_level.end();

  if (IsReservedModuleEnabled(ctx, type.name)) {
    if (error) *error = "module is not a type: " + type.name;
    PrefixErrorLocation(type.line, type.column, error);
    return false;
  }

  if (!CheckKnownTypeName(type, is_primitive, is_type_param,
                          is_user_type || is_canonical_generic, error)) {
    PrefixErrorLocation(type.line, type.column, error);
    return false;
  }

  if (is_canonical_generic) {
    if (!CheckTypeArgumentRules(type,
                                false,
                                false,
                                false,
                                &canonical_generic_arity,
                                error)) {
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
  } else if (is_user_type && !is_type_param) {
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
    auto art_it = ctx.artifact_generics.find(type.name);
    const size_t* expected_artifact_type_args =
        art_it != ctx.artifact_generics.end() ? &art_it->second : nullptr;
    if (!CheckTypeArgumentRules(type,
                                is_primitive,
                                is_type_param,
                                ctx.enum_types.find(type.name) != ctx.enum_types.end(),
                                expected_artifact_type_args,
                                error)) {
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
  }

  if (!type.type_args.empty()) {
    if (!CheckTypeArgumentRules(type,
                                is_primitive,
                                is_type_param,
                                false,
                                nullptr,
                                error)) {
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    for (const auto& arg : type.type_args) {
      if (!CheckTypeRef(arg, ctx, type_params, TypeUse::Value, error)) return false;
    }
  }

  return true;
}

bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out) {
  if (!out) return false;
  switch (expr.kind) {
    case ExprKind::Literal:
      return InferLiteralType(expr, nullptr, out, nullptr);
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
        auto ext_mod_it = ctx.externs_by_module.find(base.text);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(expr.text);
          if (ext_it != ext_mod_it->second.end()) {
            return CloneTypeRef(ext_it->second->return_type, out);
          }
        }
        return false;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          if (ResolveReservedModuleVarType(ctx, module_name, expr.text, out)) {
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
      TypeRef base_type;
      if (InferExprType(base, ctx, scopes, current_artifact, &base_type)) {
        auto artifact_it = ctx.artifacts.find(base_type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        std::unordered_map<std::string, TypeRef> mapping;
        if (artifact && !artifact->generics.empty()) {
          if (!BuildArtifactTypeParamMap(base_type, artifact, &mapping, nullptr)) return false;
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
      if (base_type.name == "string" && base_type.dims.empty()) {
        out->name = "char";
        return true;
      }
      if (base_type.dims.empty()) return false;
      TypeRef result;
      if (!CloneTypeRef(base_type, &result)) return false;
      result.dims.erase(result.dims.begin());
      return CloneTypeRef(result, out);
    }
    case ExprKind::Unary: {
      const Expr* operand_expr = nullptr;
      if (!IsUnaryExpr(expr, &operand_expr)) return false;
      TypeRef operand;
      if (!InferExprType(*operand_expr, ctx, scopes, current_artifact, &operand)) return false;
      const std::string op = expr.op.rfind("post", 0) == 0 ? expr.op.substr(4) : expr.op;
      if (op == "&") {
        TypeRef result = operand;
        result.pointer_depth += 1;
        return CloneTypeRef(result, out);
      }
      if (op == "?") {
        TaggedTypeInfo tagged;
        if (!ResolveTaggedType(operand, ctx, &tagged) || !tagged.value_type) return false;
        return CloneTypeRef(*tagged.value_type, out);
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
      const Expr* lhs_expr = nullptr;
      const Expr* rhs_expr = nullptr;
      if (!IsBinaryExpr(expr, &lhs_expr, &rhs_expr)) return false;
      TypeRef lhs;
      TypeRef rhs;
      if (!InferExprType(*lhs_expr, ctx, scopes, current_artifact, &lhs)) return false;
      if (!InferExprType(*rhs_expr, ctx, scopes, current_artifact, &rhs)) return false;
      if (!IsScalarType(lhs) || !IsScalarType(rhs)) return false;

      TypeRef common;
      if (TypeEquals(lhs, rhs)) {
        if (!CloneTypeRef(lhs, &common)) return false;
      } else {
        if (IsLiteralCompatibleWithScalarType(*lhs_expr, rhs)) {
          if (!CloneTypeRef(rhs, &common)) return false;
        } else if (IsLiteralCompatibleWithScalarType(*rhs_expr, lhs)) {
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

bool ValidatePropagationStmt(
    const Stmt& stmt,
    const TypeRef* expected_return,
    const ValidateContext& ctx,
    std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const ArtifactDecl* current_artifact,
    bool recurse_blocks,
    std::string* error);

bool ValidatePropagationBlock(
    const std::vector<Stmt>& body,
    const TypeRef* expected_return,
    const ValidateContext& ctx,
    std::vector<std::unordered_map<std::string, LocalInfo>> scopes,
    const ArtifactDecl* current_artifact,
    std::string* error);

bool ValidatePropagationExpr(
    const Expr& expr,
    const TypeRef* expected_return,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const ArtifactDecl* current_artifact,
    std::string* error) {
  if (expr.kind == ExprKind::Unary && expr.op == "post?") {
    if (expr.children.size() != 1) {
      if (error) *error = "propagation operator expects one operand";
      return false;
    }
    TypeRef operand;
    if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &operand)) {
      if (error) *error = "cannot resolve propagation operand type";
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    }
    TaggedTypeInfo operand_tagged;
    if (!ResolveTaggedType(operand, ctx, &operand_tagged)) {
      if (error) *error = "operator '?' requires optional or Result operand";
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    }
    if (!expected_return) {
      if (error) *error = "operator '?' requires an enclosing function";
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    }
    TaggedTypeInfo return_tagged;
    if (!ResolveTaggedType(*expected_return, ctx, &return_tagged) ||
        return_tagged.kind != operand_tagged.kind) {
      if (error) {
        *error = operand_tagged.kind == TaggedArtifactKind::Optional
                     ? "optional propagation requires an optional return type"
                     : "Result propagation requires a Result return type";
      }
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    }
    if (operand_tagged.kind == TaggedArtifactKind::Optional &&
        (!operand_tagged.value_type || !return_tagged.value_type ||
         !TypeEquals(*operand_tagged.value_type, *return_tagged.value_type))) {
      if (error) *error = "optional propagation requires the same payload type";
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    }
    if (operand_tagged.kind == TaggedArtifactKind::Result &&
        (!operand_tagged.error_type || !return_tagged.error_type ||
         !TypeEquals(*operand_tagged.error_type, *return_tagged.error_type))) {
      if (error) *error = "Result propagation requires the same error type";
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    }
  }
  for (const auto& child : expr.children) {
    if (!ValidatePropagationExpr(child, expected_return, ctx, scopes, current_artifact, error)) {
      return false;
    }
  }
  for (const auto& arg : expr.args) {
    if (!ValidatePropagationExpr(arg, expected_return, ctx, scopes, current_artifact, error)) {
      return false;
    }
  }
  for (const auto& value : expr.field_values) {
    if (!ValidatePropagationExpr(value, expected_return, ctx, scopes, current_artifact, error)) {
      return false;
    }
  }
  TaggedTypeInfo switch_tagged;
  TypeRef switch_subject;
  const bool have_switch_tagged =
      expr.kind == ExprKind::Switch && !expr.children.empty() &&
      InferExprType(expr.children[0], ctx, scopes, current_artifact, &switch_subject) &&
      ResolveTaggedType(switch_subject, ctx, &switch_tagged);
  for (const auto& branch : expr.switch_branches) {
    if (!branch.is_default && !IsSwitchPattern(branch) &&
        !ValidatePropagationExpr(branch.condition,
                                 expected_return,
                                 ctx,
                                 scopes,
                                 current_artifact,
                                 error)) {
      return false;
    }
    auto branch_scopes = scopes;
    if (branch.is_block || !branch.pattern_binding.empty()) branch_scopes.emplace_back();
    if (!branch.pattern_binding.empty() && have_switch_tagged) {
      const TypeRef* binding_type = switch_tagged.value_type;
      if (switch_tagged.kind == TaggedArtifactKind::Result &&
          branch.pattern_field == "error") {
        binding_type = switch_tagged.error_type;
      }
      if (binding_type) {
        LocalInfo binding;
        binding.mutability = Mutability::Immutable;
        binding.type = binding_type;
        branch_scopes.back()[branch.pattern_binding] = binding;
      }
    }
    if (branch.has_inline_value &&
        !ValidatePropagationExpr(branch.value,
                                 expected_return,
                                 ctx,
                                 branch_scopes,
                                 current_artifact,
                                 error)) {
      return false;
    }
    if (branch.is_block &&
        !ValidatePropagationBlock(branch.block,
                                  expected_return,
                                  ctx,
                                  branch_scopes,
                                  current_artifact,
                                  error)) {
      return false;
    }
  }
  return true;
}

bool ValidatePropagationStmt(
    const Stmt& stmt,
    const TypeRef* expected_return,
    const ValidateContext& ctx,
    std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const ArtifactDecl* current_artifact,
    bool recurse_blocks,
    std::string* error) {
  if (scopes.empty()) scopes.emplace_back();
  auto validate = [&](const Expr& value) {
    return ValidatePropagationExpr(
        value, expected_return, ctx, scopes, current_artifact, error);
  };
  switch (stmt.kind) {
    case StmtKind::Return:
      if (stmt.has_return_expr && !validate(stmt.expr)) return false;
      break;
    case StmtKind::Expr:
      if (!validate(stmt.expr)) return false;
      break;
    case StmtKind::Assign:
      if (!validate(stmt.target) || !validate(stmt.expr)) return false;
      break;
    case StmtKind::VarDecl:
      if (stmt.var_decl.has_init_expr && !validate(stmt.var_decl.init_expr)) return false;
      scopes.back()[stmt.var_decl.name] =
          LocalInfo{stmt.var_decl.mutability, &stmt.var_decl.type, {}, false};
      break;
    case StmtKind::IfChain:
      for (const auto& branch : stmt.if_branches) {
        if (!validate(branch.first)) return false;
        if (recurse_blocks &&
            !ValidatePropagationBlock(branch.second,
                                      expected_return,
                                      ctx,
                                      scopes,
                                      current_artifact,
                                      error)) {
          return false;
        }
      }
      if (recurse_blocks &&
          !ValidatePropagationBlock(stmt.else_branch,
                                    expected_return,
                                    ctx,
                                    scopes,
                                    current_artifact,
                                    error)) {
        return false;
      }
      break;
    case StmtKind::IfStmt:
      if (!validate(stmt.if_cond)) return false;
      if (recurse_blocks &&
          (!ValidatePropagationBlock(stmt.if_then,
                                     expected_return,
                                     ctx,
                                     scopes,
                                     current_artifact,
                                     error) ||
           !ValidatePropagationBlock(stmt.if_else,
                                     expected_return,
                                     ctx,
                                     scopes,
                                     current_artifact,
                                     error))) {
        return false;
      }
      break;
    case StmtKind::WhileLoop:
      if (!validate(stmt.loop_cond)) return false;
      if (recurse_blocks &&
          !ValidatePropagationBlock(stmt.loop_body,
                                    expected_return,
                                    ctx,
                                    scopes,
                                    current_artifact,
                                    error)) {
        return false;
      }
      break;
    case StmtKind::ForLoop: {
      auto loop_scopes = scopes;
      loop_scopes.emplace_back();
      if (stmt.has_loop_var_decl) {
        if (stmt.loop_var_decl.has_init_expr &&
            !ValidatePropagationExpr(stmt.loop_var_decl.init_expr,
                                     expected_return,
                                     ctx,
                                     loop_scopes,
                                     current_artifact,
                                     error)) {
          return false;
        }
        loop_scopes.back()[stmt.loop_var_decl.name] =
            LocalInfo{stmt.loop_var_decl.mutability, &stmt.loop_var_decl.type, {}, false};
      } else if (!ValidatePropagationExpr(stmt.loop_iter,
                                          expected_return,
                                          ctx,
                                          loop_scopes,
                                          current_artifact,
                                          error)) {
        return false;
      }
      if (!ValidatePropagationExpr(stmt.loop_cond,
                                   expected_return,
                                   ctx,
                                   loop_scopes,
                                   current_artifact,
                                   error) ||
          !ValidatePropagationExpr(stmt.loop_step,
                                   expected_return,
                                   ctx,
                                   loop_scopes,
                                   current_artifact,
                                   error)) {
        return false;
      }
      if (recurse_blocks &&
          !ValidatePropagationBlock(stmt.loop_body,
                                    expected_return,
                                    ctx,
                                    loop_scopes,
                                    current_artifact,
                                    error)) {
        return false;
      }
      break;
    }
    case StmtKind::Break:
    case StmtKind::Skip:
      break;
  }
  return true;
}

bool ValidatePropagationBlock(
    const std::vector<Stmt>& body,
    const TypeRef* expected_return,
    const ValidateContext& ctx,
    std::vector<std::unordered_map<std::string, LocalInfo>> scopes,
    const ArtifactDecl* current_artifact,
    std::string* error) {
  if (scopes.empty()) scopes.emplace_back();
  for (const auto& stmt : body) {
    if (!ValidatePropagationStmt(
            stmt, expected_return, ctx, scopes, current_artifact, true, error)) {
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
  const Expr* member_base = nullptr;
  if (IsMemberAccessExpr(expr, &member_base, nullptr)) {
    const Expr& base = *member_base;
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
  const Expr* index_base = nullptr;
  if (IsIndexExpr(expr, &index_base)) {
    return IsMutableStorageExpr(*index_base, ctx, scopes, current_artifact, out_known);
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
  const Expr* address_target = nullptr;
  if (IsAddressOfExpr(expr, &address_target)) {
    bool known = false;
    const bool is_mutable = IsMutableStorageExpr(*address_target, ctx, scopes, current_artifact, &known);
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
    if (!CheckPrimitiveCastSyntaxName(callee.text, error)) return false;
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
    if (ResolveUsingReservedCallTarget(ctx, callee.text, &using_module, &using_info) ||
        ResolveUsingModuleExternCallTarget(ctx, callee.text, &using_module, &using_info)) {
      if (IsCanonicalLibraryModule(using_module, StandardModule::IO) && IsIoPrintName(callee.text)) {
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
            if (!CheckDlDynamicSignature(*ext_it->second, ctx.enum_types, ctx.artifacts, error)) return false;
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
        auto ext_mod_it = ctx.externs_by_module.find(base.text);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(callee.text);
          if (ext_it != ext_mod_it->second.end()) {
            if (ext_it->second->params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for extern " + base.text + "." + callee.text +
                         ": expected " + std::to_string(ext_it->second->params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
        }
        return true;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          CallTargetInfo info;
          if (GetReservedModuleCallTarget(ctx, module_name, callee.text, &info)) {
            LibraryModuleId resolved_module{};
            const bool is_system_ffi_open =
                ResolveReservedModuleId(ctx, module_name, &resolved_module) &&
                IsLibraryModule(resolved_module, SystemModule::FFI) &&
                ParseMember(SystemModule::FFI, NormalizeDlMemberName(callee.text)) == SystemMember(SystemFFIMember::Open);
            if (!is_system_ffi_open && info.params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for " + module_name + "." + callee.text +
                         ": expected " + std::to_string(info.params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            if (is_system_ffi_open && arg_count != 1 && arg_count != 2) {
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

bool PopulateArtifactCallTarget(const TypeRef& instance_type,
                                const ArtifactDecl* artifact,
                                const std::string& member,
                                CallTargetInfo* out,
                                std::string* error) {
  if (!artifact || !out) return false;
  std::unordered_map<std::string, TypeRef> substitutions;
  if (!artifact->generics.empty() &&
      !BuildArtifactTypeParamMap(instance_type, artifact, &substitutions, error)) {
    return false;
  }
  if (const FuncDecl* method = FindArtifactMethod(artifact, member)) {
    out->params.clear();
    if (!SubstituteTypeParams(method->return_type, substitutions, &out->return_type)) return false;
    out->return_mutability = method->return_mutability;
    out->type_params = method->generics;
    out->is_proc = false;
    for (const auto& param : method->params) {
      TypeRef resolved;
      if (!SubstituteTypeParams(param.type, substitutions, &resolved)) return false;
      out->params.push_back(std::move(resolved));
    }
    return true;
  }
  const VarDecl* field = FindArtifactField(artifact, member);
  if (!field || !field->type.is_proc) return false;
  TypeRef resolved;
  if (!SubstituteTypeParams(field->type, substitutions, &resolved)) return false;
  out->params.clear();
  out->type_params.clear();
  out->is_proc = true;
  out->return_mutability = resolved.proc_return_mutability;
  if (!CloneTypeVector(resolved.proc_params, &out->params)) return false;
  if (resolved.proc_return && !CloneTypeRef(*resolved.proc_return, &out->return_type)) return false;
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
    if (ResolveUsingReservedCallTarget(ctx, callee.text, nullptr, out) ||
        ResolveUsingModuleExternCallTarget(ctx, callee.text, nullptr, out)) return true;
    return false;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    std::string qualified_module_name;
    if (GetModuleNameFromExpr(base, &qualified_module_name) &&
        IsReservedModuleEnabled(ctx, qualified_module_name) &&
        GetReservedModuleCallTarget(ctx, qualified_module_name, callee.text, out)) {
      return true;
    }
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
            if (!CheckDlDynamicSignature(*ext_it->second, ctx.enum_types, ctx.artifacts, error)) return false;
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
        auto ext_mod_it = ctx.externs_by_module.find(base.text);
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
    }
    TypeRef instance_type;
    if (InferExprType(base, ctx, scopes, current_artifact, &instance_type)) {
      const auto artifact_it = ctx.artifacts.find(instance_type.name);
      const ArtifactDecl* artifact =
          artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
      if (PopulateArtifactCallTarget(instance_type, artifact, callee.text, out, error)) return true;
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
  const Expr* call_callee = nullptr;
  if (!IsCallExpr(call_expr, &call_callee)) return true;
  const Expr& callee = *call_callee;
  if (callee.kind == ExprKind::FnLiteral) {
    TypeRef void_type = MakeSimpleType("void");
    TypeRef signature;
    if (!BuildDirectFnLiteralSignature(
            call_expr, void_type, ctx, scopes, current_artifact, &signature, error)) {
      return false;
    }
    for (size_t i = 0; i < call_expr.args.size(); ++i) {
      if (!ValidateExprAgainstExpected(call_expr.args[i], signature.proc_params[i],
                                       ctx, scopes, current_artifact, error)) {
        return false;
      }
    }
    return true;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    std::string module_name;
    if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
      LibraryModuleId mod{};
      if (!ResolveReservedModuleId(ctx, module_name, &mod)) return true;
      const std::string& name = callee.text;
      auto infer_arg = [&](size_t index, TypeRef* out_type) -> bool {
        if (!out_type) return false;
        if (index >= call_expr.args.size()) return false;
        return InferExprType(call_expr.args[index], ctx, scopes, current_artifact, out_type);
      };
      if (IsLibraryModule(mod, StandardModule::Math)) {
        std::vector<TypeRef> arg_types;
        arg_types.reserve(call_expr.args.size());
        for (size_t i = 0; i < call_expr.args.size(); ++i) {
          TypeRef arg;
          if (!infer_arg(i, &arg)) return true;
          arg_types.push_back(std::move(arg));
        }
        const auto member = ParseMember(StandardModule::Math, name);
        if (!member || !std::holds_alternative<StandardMathMember>(*member)) return true;
        return CheckReservedMathCallArgTypes(std::get<StandardMathMember>(*member), arg_types, error);
      }
      if (IsLibraryModule(mod, SystemModule::IO)) {
        std::vector<TypeRef> arg_types;
        arg_types.reserve(call_expr.args.size());
        for (size_t i = 0; i < call_expr.args.size(); ++i) {
          TypeRef arg;
          if (!infer_arg(i, &arg)) return true;
          arg_types.push_back(std::move(arg));
        }
        const auto member = ParseMember(SystemModule::IO, name);
        if (!member || !std::holds_alternative<SystemIOMember>(*member)) return true;
        return CheckReservedIoBufferCallArgTypes(std::get<SystemIOMember>(*member), arg_types, error);
      }
      if (IsLibraryModule(mod, SystemModule::Time)) {
        std::vector<TypeRef> arg_types;
        arg_types.reserve(call_expr.args.size());
        for (size_t i = 0; i < call_expr.args.size(); ++i) {
          TypeRef arg;
          if (!infer_arg(i, &arg)) return true;
          arg_types.push_back(std::move(arg));
        }
        return CheckReservedTimeCallArgTypes(name, arg_types, error);
      }
      if (IsLibraryModule(mod, SystemModule::FFI) &&
          ParseMember(SystemModule::FFI, NormalizeDlMemberName(name)) == SystemMember(SystemFFIMember::Open)) {
        TypeRef path;
        if (!call_expr.args.empty() && !infer_arg(0, &path)) return true;
        std::vector<TypeRef> arg_types(call_expr.args.size());
        if (!arg_types.empty()) arg_types[0] = std::move(path);
        if (!CheckReservedDlOpenArgTypes(arg_types, error)) return false;
        if (call_expr.args.size() == 2) {
          if (call_expr.args[1].kind != ExprKind::Identifier) {
            if (error) *error = "System.FFI.open manifest must be an extern module identifier";
            return false;
          }
          const std::string manifest = call_expr.args[1].text;
          auto mod_it = ctx.externs_by_module.find(manifest);
          if (mod_it == ctx.externs_by_module.end() || mod_it->second.empty()) {
            if (error) *error = "System.FFI.open manifest has no extern symbols: " + manifest;
            return false;
          }
          for (const auto& entry : mod_it->second) {
            if (!CheckDlDynamicSignature(*entry.second, ctx.enum_types, ctx.artifacts, error)) return false;
          }
        }
        return true;
      }
      CallTargetInfo reserved_info;
      if (GetReservedModuleCallTarget(ctx, module_name, name, &reserved_info)) {
        for (size_t i = 0; i < reserved_info.params.size() && i < call_expr.args.size(); ++i) {
          TypeRef actual;
          if (!infer_arg(i, &actual)) return true;
          if (!CheckTypesCompatibleForExpr(reserved_info.params[i], actual, call_expr.args[i],
                                           "call argument type mismatch", error)) return false;
        }
        return true;
      }
    }
  }
  CallTargetInfo info;
  if (!GetCallTargetInfo(callee, ctx, scopes, current_artifact, &info, error)) return true;
  if (!CheckCallTypeArgCount(info.type_params.size(), call_expr.type_args.size(), error)) return false;

  std::unordered_map<std::string, TypeRef> mapping;
  if (!info.type_params.empty()) {
    std::unordered_set<std::string> type_param_set(info.type_params.begin(), info.type_params.end());
    if (!call_expr.type_args.empty()) {
      if (!BuildExplicitTypeArgMap(info.type_params, call_expr.type_args, &mapping, error)) return false;
    } else {
      if (!InferTypeArgsFromCall(info.params, call_expr.args, type_param_set,
                                 ctx, scopes, current_artifact, &mapping)) {
        if (error) *error = "cannot infer type arguments for call";
        return false;
      }
      if (ctx.inferred_generic_calls) {
        std::vector<TypeRef> inferred;
        inferred.reserve(info.type_params.size());
        for (const auto& name : info.type_params) {
          auto inferred_it = mapping.find(name);
          if (inferred_it == mapping.end()) return false;
          TypeRef copy;
          if (!CloneTypeRef(inferred_it->second, &copy)) return false;
          inferred.push_back(std::move(copy));
        }
        (*ctx.inferred_generic_calls)[&call_expr] = std::move(inferred);
      }
    }
  }

  for (size_t i = 0; i < info.params.size() && i < call_expr.args.size(); ++i) {
    TypeRef expected;
    if (!SubstituteTypeParams(info.params[i], mapping, &expected)) return false;
    TypeRef actual;
    if (!InferExprType(call_expr.args[i], ctx, scopes, current_artifact, &actual)) {
      if (!ValidateExprAgainstExpected(
              call_expr.args[i], expected, ctx, scopes, current_artifact, error)) {
        return false;
      }
      continue;
    }
    if (!CheckTypesCompatibleForExpr(expected, actual, call_expr.args[i],
                                     "call argument type mismatch", error)) return false;
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
    const Expr* member_base = nullptr;
    if (IsMemberAccessExpr(expr, &member_base, nullptr)) {
      const Expr& base = *member_base;
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
    const Expr* call_callee = nullptr;
    if (IsCallExpr(expr, &call_callee)) {
      CallTargetInfo info;
      if (!GetCallTargetInfo(*call_callee, ctx, scopes, current_artifact, &info, nullptr)) return true;
      return info.return_mutability == Mutability::Mutable;
    }
    if (expr.kind == ExprKind::Index) {
      if (expr.children.empty()) return true;
      return is_mutable_expr(expr.children[0]);
    }
    return true;
  };
  if (target.kind == ExprKind::Identifier) {
    if (!CheckAssignTargetSelfName(target, error)) return false;
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
  const Expr* target_member_base = nullptr;
  if (IsMemberAccessExpr(target, &target_member_base, nullptr)) {
    const Expr& base = *target_member_base;
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
  if (!CheckArtifactLiteralPositionalCount(expr, field_count, error)) return false;
  if (!CheckArtifactLiteralDuplicateNamedFields(expr, error)) return false;
  std::unordered_set<std::string> seen;
  for (const auto& name : expr.field_names) seen.insert(name);
  for (size_t i = 0; i < expr.children.size(); ++i) {
    if (i >= field_count) break;
    const auto& field = artifact->fields[i];
    if (!CheckArtifactLiteralFieldSpecifiedOnce(field.name, seen, error)) return false;
    seen.insert(field.name);
    TypeRef expected;
    if (!SubstituteTypeParams(field.type, type_mapping, &expected)) return false;
    if (!ValidateExprAgainstExpected(
            expr.children[i], expected, ctx, scopes, current_artifact, error)) {
      return false;
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
      if (!CheckArtifactLiteralKnownField(name, valid, error)) return false;
    }
    for (size_t i = 0; i < expr.field_names.size(); ++i) {
      const auto& name = expr.field_names[i];
      auto it = field_map.find(name);
      if (it == field_map.end()) continue;
      TypeRef expected;
      if (!SubstituteTypeParams(it->second->type, type_mapping, &expected)) return false;
      if (!ValidateExprAgainstExpected(
              expr.field_values[i], expected, ctx, scopes, current_artifact, error)) {
        return false;
      }
    }
  }
  for (const auto& field : artifact->fields) {
    if (!CheckArtifactLiteralRequiredField(field.name, field.has_init_expr, seen, error)) return false;
  }
  return true;
}

bool BuildDirectFnLiteralSignature(
    const Expr& call,
    const TypeRef& result_type,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const ArtifactDecl* current_artifact,
    TypeRef* signature,
    std::string* error) {
  if (!signature || call.kind != ExprKind::Call || call.children.empty() ||
      call.children[0].kind != ExprKind::FnLiteral) {
    return false;
  }
  const Expr& literal = call.children[0];
  if (literal.fn_params.size() != call.args.size()) {
    if (error) {
      *error = "call argument count mismatch for fn literal: expected " +
               std::to_string(literal.fn_params.size()) + ", got " +
               std::to_string(call.args.size());
    }
    return false;
  }
  *signature = TypeRef{};
  signature->is_proc = true;
  signature->proc_return = std::make_unique<TypeRef>();
  if (!CloneTypeRef(result_type, signature->proc_return.get())) return false;
  signature->proc_params.reserve(call.args.size());
  for (size_t i = 0; i < call.args.size(); ++i) {
    TypeRef param_type;
    if (!literal.fn_params[i].type.name.empty() || literal.fn_params[i].type.is_proc) {
      if (!CloneTypeRef(literal.fn_params[i].type, &param_type)) return false;
    } else if (!InferExprType(
                   call.args[i], ctx, scopes, current_artifact, &param_type)) {
      if (error) *error = "cannot infer direct fn literal parameter type";
      return false;
    }
    signature->proc_params.push_back(std::move(param_type));
  }
  return true;
}

bool ValidateFnLiteralBody(
    const Expr& expr,
    const TypeRef& signature,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& outer_scopes,
    const ArtifactDecl* current_artifact,
    std::string* error) {
  if (!CheckFnLiteralAgainstType(expr, signature, error)) return false;
  if (!signature.proc_return) {
    if (error) *error = "fn literal procedure type is missing a return type";
    return false;
  }

  FuncDecl lambda;
  lambda.return_mutability = signature.proc_return_mutability;
  if (!CloneTypeRef(*signature.proc_return, &lambda.return_type)) return false;
  lambda.body = expr.fn_body;
  std::vector<std::unordered_map<std::string, LocalInfo>> lambda_scopes = outer_scopes;
  lambda_scopes.emplace_back();
  std::unordered_set<std::string> names;
  for (size_t i = 0; i < expr.fn_params.size(); ++i) {
    if (!CheckUniqueParamName(
            expr.fn_params[i].name, &names, "duplicate fn literal parameter: ", error)) {
      return false;
    }
    LocalInfo info;
    info.mutability = expr.fn_params[i].mutability;
    info.type = &signature.proc_params[i];
    lambda_scopes.back().emplace(expr.fn_params[i].name, std::move(info));
  }

  const bool returns_void = lambda.return_type.name == "void";
  const std::unordered_set<std::string> type_params;
  for (const auto& stmt : lambda.body) {
    if (!CheckStmt(stmt, ctx, type_params, &lambda.return_type, returns_void, 0,
                   lambda_scopes, current_artifact, error)) {
      if (error && !error->empty()) *error = "in fn literal: " + *error;
      return false;
    }
  }
  if (!CheckFunctionReturnFlow(lambda, error)) {
    if (error && !error->empty()) *error = "in fn literal: " + *error;
    return false;
  }
  return true;
}

bool ValidateExprAgainstExpected(
    const Expr& expr,
    const TypeRef& expected,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const ArtifactDecl* current_artifact,
    std::string* error) {
  if (expr.kind == ExprKind::Call && !expr.children.empty() &&
      expr.children[0].kind == ExprKind::FnLiteral) {
    TypeRef signature;
    if (!BuildDirectFnLiteralSignature(
            expr, expected, ctx, scopes, current_artifact, &signature, error) ||
        !ValidateFnLiteralBody(expr.children[0], signature, ctx, scopes, current_artifact, error)) {
      return false;
    }
    for (size_t i = 0; i < expr.args.size(); ++i) {
      if (!ValidateExprAgainstExpected(
              expr.args[i], signature.proc_params[i], ctx, scopes, current_artifact, error)) {
        return false;
      }
    }
    return true;
  }

  TaggedTypeInfo tagged;
  if (ResolveTaggedType(expected, ctx, &tagged) && expr.kind == ExprKind::ArtifactLiteral) {
    if (tagged.kind == TaggedArtifactKind::Optional) {
      if (!expr.field_names.empty() || !expr.field_values.empty() || expr.children.size() > 1) {
        if (error) *error = "optional literal must be '{}' or '{ value }'";
        return false;
      }
      if (expr.children.empty()) return true;
      return ValidateExprAgainstExpected(
          expr.children[0], *tagged.value_type, ctx, scopes, current_artifact, error);
    }
    if (tagged.kind == TaggedArtifactKind::Result) {
      if (!expr.children.empty() || expr.field_names.size() != 1 ||
          expr.field_values.size() != 1) {
        if (error) {
          *error = "Result literal requires exactly one '.value' or '.error' payload";
        }
        return false;
      }
      const std::string& field = expr.field_names[0];
      const TypeRef* payload_type = nullptr;
      if (field == "value") payload_type = tagged.value_type;
      if (field == "error") payload_type = tagged.error_type;
      if (!payload_type) {
        if (error) *error = "Result literal field must be '.value' or '.error'";
        return false;
      }
      return ValidateExprAgainstExpected(
          expr.field_values[0], *payload_type, ctx, scopes, current_artifact, error);
    }
  }

  if (expr.kind == ExprKind::ArtifactLiteral && expected.dims.empty()) {
    const auto artifact_it = ctx.artifacts.find(expected.name);
    if (artifact_it == ctx.artifacts.end()) {
      if (error) *error = "artifact literal requires an artifact or tagged target type";
      return false;
    }
    std::unordered_map<std::string, TypeRef> mapping;
    if (!BuildArtifactTypeParamMap(expected, artifact_it->second, &mapping, error)) {
      return false;
    }
    return ValidateArtifactLiteral(
        expr, artifact_it->second, mapping, ctx, scopes, current_artifact, error);
  }

  if (!expected.dims.empty() && expected.dims.front().is_list &&
      IsListLiteralExpr(expr)) {
    return CheckListLiteralElementTypes(expr, ctx, scopes, current_artifact, expected, error);
  }
  if (!expected.dims.empty() && !expected.dims.front().is_list &&
      IsPositionalBraceLiteralExpr(expr)) {
    if (!CheckArrayLiteralShape(expr, expected.dims, 0, error)) return false;
    TypeRef base_type;
    if (!CloneTypeRef(expected, &base_type)) return false;
    base_type.dims.clear();
    return CheckArrayLiteralElementTypes(expr,
                                         ctx,
                                         scopes,
                                         current_artifact,
                                         expected.dims,
                                         0,
                                         base_type,
                                         error);
  }
  if (expr.kind == ExprKind::FnLiteral) {
    return ValidateFnLiteralBody(expr, expected, ctx, scopes, current_artifact, error);
  }
  if (expr.kind == ExprKind::Member && !expr.children.empty() &&
      expr.children[0].kind == ExprKind::Identifier &&
      ctx.enum_types.find(expr.children[0].text) != ctx.enum_types.end() &&
      (expected.name == expr.children[0].text || expected.name == "i32")) {
    return CheckExpr(expr, ctx, scopes, current_artifact, error);
  }
  if (!CheckExpr(expr, ctx, scopes, current_artifact, error)) return false;
  TypeRef actual;
  if (!InferExprType(expr, ctx, scopes, current_artifact, &actual)) {
    if (error) *error = "cannot infer expression type for typed context";
    return false;
  }
  return CheckTypesCompatibleForExpr(
      expected, actual, expr, "expression type mismatch", error);
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
  auto propagation_scopes = scopes;
  if (!ValidatePropagationStmt(
          stmt,
          expected_return,
          ctx,
          propagation_scopes,
          current_artifact,
          false,
          error)) {
    return false;
  }

  switch (stmt.kind) {
    case StmtKind::Return:
      if (!CheckReturnStmtValuePresence(stmt, return_is_void, error)) return false;
      if (stmt.has_return_expr) {
        if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
        const bool direct_fn_call = IsDirectFnLiteralCall(stmt.expr);
        const bool contextual_return =
            stmt.expr.kind == ExprKind::ArtifactLiteral ||
            stmt.expr.kind == ExprKind::FnLiteral || direct_fn_call;
        if (expected_return && contextual_return &&
            !ValidateExprAgainstExpected(
                stmt.expr, *expected_return, ctx, scopes, current_artifact, error)) {
          return false;
        }
        if (expected_return && !contextual_return) {
          TypeRef actual;
          if (InferExprType(stmt.expr, ctx, scopes, current_artifact, &actual)) {
            if (!CheckTypesCompatibleForExpr(*expected_return, actual, stmt.expr,
                                             "return type mismatch", error)) return false;
          }
        }
        return true;
      }
      return true;
    case StmtKind::Expr: {
      if (stmt.expr.kind == ExprKind::ArtifactLiteral) {
        if (error) *error = "contextual literal requires a typed value context";
        return false;
      }
      if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
      if (IsDirectFnLiteralCall(stmt.expr)) {
        const TypeRef void_type = MakeSimpleType("void");
        if (!ValidateExprAgainstExpected(
                stmt.expr, void_type, ctx, scopes, current_artifact, error)) {
          return false;
        }
      }
      TypeRef expression_type;
      if (InferExprType(stmt.expr, ctx, scopes, current_artifact, &expression_type)) {
        TaggedTypeInfo tagged;
        if (ResolveTaggedType(expression_type, ctx, &tagged) &&
            tagged.kind == TaggedArtifactKind::Result) {
          if (error) {
            *error = "Result value must be returned, stored, propagated, or exhaustively handled";
          }
          return false;
        }
      }
      return true;
    }
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
        if (have_target && stmt.expr.kind == ExprKind::ArtifactLiteral &&
            !ValidateExprAgainstExpected(
                stmt.expr, target_type, ctx, scopes, current_artifact, error)) {
          return false;
        }
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
        } else if (have_target && IsDirectFnLiteralCall(stmt.expr)) {
          if (!ValidateExprAgainstExpected(
                  stmt.expr, target_type, ctx, scopes, current_artifact, error) ||
              !CloneTypeRef(target_type, &value_type)) {
            return false;
          }
          have_value = true;
        } else {
          have_value = InferExprType(stmt.expr, ctx, scopes, current_artifact, &value_type);
        }
        if (have_target && stmt.expr.kind == ExprKind::FnLiteral) {
          if (!ValidateFnLiteralBody(stmt.expr, target_type, ctx, scopes, current_artifact, error)) return false;
        }
        if (have_target && have_value &&
            !CheckTypesCompatibleForExpr(target_type, value_type, stmt.expr,
                                         "assignment type mismatch", error)) return false;
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
        } else if (have_target && !CheckArrayListLiteralTargetShape(target_type, stmt.expr, error)) {
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
        // CheckStmt records a type pointer in the scope. The temporary statement
        // is only used for validation, so retain the type owned by the for-loop.
        auto loop_local = scopes.back().find(stmt.loop_var_decl.name);
        if (loop_local != scopes.back().end()) {
          loop_local->second.type = &stmt.loop_var_decl.type;
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
      if (!ValidateExprAgainstExpected(
              child, element_type, ctx, scopes, current_artifact, error)) {
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
    if (!ValidateExprAgainstExpected(
            child, element_type, ctx, scopes, current_artifact, error)) {
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
  if (!ValidatePropagationExpr(
          var.init_expr, expected_return, ctx, scopes, current_artifact, error)) {
    return false;
  }
  TaggedTypeInfo tagged_target;
  const bool is_tagged_literal =
      var.init_expr.kind == ExprKind::ArtifactLiteral &&
      ResolveTaggedType(var.type, ctx, &tagged_target);
  const bool is_direct_fn_call = IsDirectFnLiteralCall(var.init_expr);
  if ((is_tagged_literal || is_direct_fn_call) &&
      !ValidateExprAgainstExpected(
          var.init_expr, var.type, ctx, scopes, current_artifact, error)) {
    return false;
  }
  if (var.init_expr.kind != ExprKind::Switch &&
      !CheckExpr(var.init_expr, ctx, scopes, current_artifact, error)) {
    return false;
  }
  if (var.init_expr.kind == ExprKind::FnLiteral) {
    if (!ValidateFnLiteralBody(var.init_expr, var.type, ctx, scopes, current_artifact, error)) {
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
  } else if (!CheckArrayListLiteralTargetShape(var.type, var.init_expr, error)) {
    return false;
  }
  TypeRef init_type;
  bool have_init_type = false;
  if (is_direct_fn_call) {
    if (!CloneTypeRef(var.type, &init_type)) return false;
    have_init_type = true;
  } else if (var.init_expr.kind == ExprKind::Switch) {
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
    if (!CheckTypesCompatibleForExpr(var.type, init_type, var.init_expr,
                                     "initializer type mismatch", error)) return false;
  }
  if (!is_tagged_literal && var.init_expr.kind == ExprKind::ArtifactLiteral &&
      var.type.dims.empty()) {
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
  } else if (!CheckArrayListLiteralTargetShape(var.type, var.init_expr, error)) {
    return false;
  }
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
  if (!CheckSwitchExprShape(expr, error)) return false;
  if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
  TypeRef subject_type;
  if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &subject_type)) {
    if (error) *error = "cannot infer switch subject type";
    return false;
  }
  TaggedTypeInfo subject_tagged;
  const bool tagged_subject = ResolveTaggedType(subject_type, ctx, &subject_tagged);
  bool uses_patterns = false;
  for (const auto& branch : expr.switch_branches) {
    uses_patterns = uses_patterns || IsSwitchPattern(branch);
  }
  if (uses_patterns && !tagged_subject) {
    if (error) *error = "structural switch patterns require optional or Result subject";
    return false;
  }

  const std::unordered_set<std::string> empty_type_params;
  const auto& branch_type_params = type_params ? *type_params : empty_type_params;
  size_t default_count = 0;
  size_t absent_count = 0;
  size_t present_count = 0;
  size_t value_count = 0;
  size_t error_count = 0;
  bool has_type = false;
  TypeRef common;
  for (const auto& branch : expr.switch_branches) {
    const TypeRef* pattern_binding_type = nullptr;
    if (uses_patterns) {
      if (branch.is_default || !IsSwitchPattern(branch)) {
        if (error) *error = "cannot mix structural patterns with conditions or default";
        return false;
      }
      if (subject_tagged.kind == TaggedArtifactKind::Optional) {
        if (branch.pattern_kind == SwitchPatternKind::Absent) {
          absent_count++;
          if (!branch.pattern_binding.empty()) {
            if (error) *error = "absent optional pattern cannot bind a value";
            return false;
          }
        } else if (branch.pattern_kind == SwitchPatternKind::Present) {
          present_count++;
          pattern_binding_type = subject_tagged.value_type;
        } else {
          if (error) *error = "optional switch requires '{}' and '{ binding }' patterns";
          return false;
        }
      } else {
        if (branch.pattern_kind != SwitchPatternKind::Tagged) {
          if (error) *error = "Result switch requires '.value' and '.error' patterns";
          return false;
        }
        if (branch.pattern_field == "value") {
          value_count++;
          pattern_binding_type = subject_tagged.value_type;
        } else if (branch.pattern_field == "error") {
          error_count++;
          pattern_binding_type = subject_tagged.error_type;
        } else {
          if (error) *error = "Result switch pattern must use '.value' or '.error'";
          return false;
        }
      }
    } else if (branch.is_default) {
      default_count++;
    } else {
      if (!CheckExpr(branch.condition, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(branch.condition, ctx, scopes, current_artifact, error)) return false;
    }

    const Expr* value_expr = nullptr;
    if (!GetSwitchBranchValueExpr(branch, require_explicit_return, &value_expr, error)) return false;
    if (!value_expr) return false;

    auto branch_scopes = scopes;
    if (branch.is_block || pattern_binding_type) branch_scopes.emplace_back();
    if (pattern_binding_type) {
      LocalInfo binding;
      binding.mutability = Mutability::Immutable;
      binding.type = pattern_binding_type;
      if (!AddLocal(branch_scopes, branch.pattern_binding, binding, error)) return false;
    }
    if (branch.is_block) {
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
    const auto& value_scopes =
        (branch.is_block || pattern_binding_type) ? branch_scopes : scopes;

    if (expected_type) {
      if (!ValidateExprAgainstExpected(
              *value_expr, *expected_type, ctx, value_scopes, current_artifact, error)) {
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
      } else if (!CheckTypesCompatibleForExpr(common,
                                              value_type,
                                              *value_expr,
                                              "switch branch type mismatch",
                                              error)) {
        return false;
      }
    }
  }
  if (uses_patterns) {
    const bool exhaustive_optional = subject_tagged.kind == TaggedArtifactKind::Optional &&
                                     absent_count == 1 && present_count == 1;
    const bool exhaustive_result = subject_tagged.kind == TaggedArtifactKind::Result &&
                                   value_count == 1 && error_count == 1;
    if (!exhaustive_optional && !exhaustive_result) {
      if (error) *error = "tagged switch must contain each state exactly once";
      return false;
    }
  } else if (default_count != 1) {
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
    return CheckConditionType(cond_type, error);
  }
  return true;
}

bool CheckUnaryOpTypes(const Expr& expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  const Expr* operand_expr = nullptr;
  if (!IsUnaryExpr(expr, &operand_expr)) return true;
  TypeRef operand;
  if (!InferExprType(*operand_expr, ctx, scopes, current_artifact, &operand)) return true;

  return CheckUnaryOpTypeRules(expr.op, operand, *operand_expr, error);
}

bool CheckBinaryOpTypes(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error) {
  const Expr* lhs_expr = nullptr;
  const Expr* rhs_expr = nullptr;
  if (!IsBinaryExpr(expr, &lhs_expr, &rhs_expr)) return true;
  TypeRef lhs;
  TypeRef rhs;
  const bool have_lhs = InferExprType(*lhs_expr, ctx, scopes, current_artifact, &lhs);
  const bool have_rhs = InferExprType(*rhs_expr, ctx, scopes, current_artifact, &rhs);
  const bool lhs_direct = IsDirectFnLiteralCall(*lhs_expr);
  const bool rhs_direct = IsDirectFnLiteralCall(*rhs_expr);
  if (lhs_direct && have_rhs && !rhs_direct) {
    if (!ValidateExprAgainstExpected(*lhs_expr, rhs, ctx, scopes, current_artifact, error) ||
        !CloneTypeRef(rhs, &lhs)) {
      return false;
    }
  } else if (rhs_direct && have_lhs && !lhs_direct) {
    if (!ValidateExprAgainstExpected(*rhs_expr, lhs, ctx, scopes, current_artifact, error) ||
        !CloneTypeRef(lhs, &rhs)) {
      return false;
    }
  } else if (lhs_direct || rhs_direct) {
    if (error) *error = "direct fn literal call requires a typed result context";
    return false;
  } else if (have_lhs && !have_rhs && ctx.enum_types.find(lhs.name) != ctx.enum_types.end()) {
    if (!ValidateExprAgainstExpected(*rhs_expr, lhs, ctx, scopes, current_artifact, error) ||
        !CloneTypeRef(lhs, &rhs)) {
      return false;
    }
  } else if (!have_lhs && have_rhs && ctx.enum_types.find(rhs.name) != ctx.enum_types.end()) {
    if (!ValidateExprAgainstExpected(*lhs_expr, rhs, ctx, scopes, current_artifact, error) ||
        !CloneTypeRef(rhs, &lhs)) {
      return false;
    }
  } else if (!have_lhs || !have_rhs) {
    return true;
  }

  if (TypeEquals(lhs, rhs) && ctx.enum_types.find(lhs.name) != ctx.enum_types.end()) {
    if (expr.op == "==" || expr.op == "!=") return true;
    if (error) *error = "enum operands support only '==' and '!='";
    return false;
  }
  return CheckBinaryOpTypeRules(expr.op, lhs, rhs, *lhs_expr, *rhs_expr, error);
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
      if (expr.text == "System" && IsLibraryRootEnabled(ctx, LibraryRoot::System)) return true;
      if (IsBuiltinValueIdentifierName(expr.text)) return true;
      if (FindLocal(scopes, expr.text)) return true;
      if (current_artifact && IsArtifactMemberName(current_artifact, expr.text)) {
        if (error) *error = "artifact members must be accessed via self: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (ctx.top_level.find(expr.text) != ctx.top_level.end()) {
        if (ctx.modules.find(expr.text) != ctx.modules.end()) {
          if (ctx.externs_by_module.find(expr.text) != ctx.externs_by_module.end()) return true;
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
      if (ResolveUsingReservedCallTarget(ctx, expr.text, nullptr, nullptr) ||
          ResolveUsingModuleExternCallTarget(ctx, expr.text, nullptr, nullptr)) {
        return true;
      }
      if (error) *error = "undeclared identifier: " + expr.text;
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    case ExprKind::Literal:
      return true;
    case ExprKind::FormatString: {
      if (!CheckFormatPlaceholderCount(expr.text, expr.args.size(), "format", error)) return false;
      std::vector<TypeRef> arg_types;
      arg_types.reserve(expr.args.size());
      for (const auto& arg : expr.args) {
        if (!CheckExpr(arg, ctx, scopes, current_artifact, error)) return false;
        TypeRef arg_type;
        if (!InferExprType(arg, ctx, scopes, current_artifact, &arg_type)) {
          if (error && error->empty()) *error = "format expects scalar arguments";
          return false;
        }
        arg_types.push_back(std::move(arg_type));
      }
      return CheckFormatCallArgTypes(arg_types, error);
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
          if (!ValidateFnLiteralBody(expr.children[1], target_type, ctx, scopes, current_artifact, error)) return false;
        }
        if (have_target && IsDirectFnLiteralCall(expr.children[1])) {
          if (!ValidateExprAgainstExpected(
                  expr.children[1], target_type, ctx, scopes, current_artifact, error) ||
              !CloneTypeRef(target_type, &value_type)) {
            return false;
          }
          have_value = true;
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
        } else if (have_target && !CheckArrayListLiteralTargetShape(target_type, expr.children[1], error)) {
          return false;
        }
        if (have_target && have_value &&
            !CheckTypesCompatibleForExpr(target_type, value_type, expr.children[1],
                                         "assignment type mismatch", error)) return false;
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
            if (error && error->empty()) *error = "Standard.IO.print expects scalar argument";
            return false;
          }
          std::vector<TypeRef> arg_types = {arg_type};
          if (!CheckIoPrintCallArgTypes(arg_types, error)) return false;
        } else {
          if (!CheckIoPrintFormatTemplateArg(expr.args[0], error)) return false;
          const size_t value_count = expr.args.size() - 1;
          if (!CheckFormatPlaceholderCount(expr.args[0].text, value_count, "Standard.IO.print format", error)) {
            return false;
          }
          std::vector<TypeRef> arg_types;
          arg_types.reserve(expr.args.size() - 1);
          for (size_t i = 1; i < expr.args.size(); ++i) {
            TypeRef arg_type;
            if (!InferExprType(expr.args[i], ctx, scopes, current_artifact, &arg_type)) {
              if (error && error->empty()) *error = "Standard.IO.print format expects scalar arguments";
              return false;
            }
            arg_types.push_back(std::move(arg_type));
          }
          if (!CheckIoPrintCallArgTypes(arg_types, error)) return false;
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier && expr.children[0].text == "len") {
        if (!CheckSingleArgCallCount("len", expr.args.size(), error)) return false;
        TypeRef arg_type;
        if (InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (!IsLenCompatibleType(arg_type)) {
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
        if (!is_at_cast && !CheckPrimitiveCastSyntaxName(expr.children[0].text, error)) return false;
        if (is_at_cast) {
          if (!CheckSingleArgCallCount(cast_target, expr.args.size(), error)) return false;
          TypeRef arg_type;
          if (!InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
            if (error && error->empty()) *error = cast_target + " cast expects scalar argument";
            return false;
          }
          if (!CheckPrimitiveCastArgType(cast_target, arg_type, error)) return false;
        }
      }
      {
        bool is_using_io_print = false;
        if (expr.children[0].kind == ExprKind::Identifier && IsIoPrintName(expr.children[0].text)) {
          std::string using_module;
          is_using_io_print = ResolveUsingReservedCallTarget(ctx, expr.children[0].text, &using_module, nullptr) &&
                              IsCanonicalLibraryModule(using_module, StandardModule::IO);
        }
        if (!IsIoPrintCallExpr(expr.children[0], ctx) && !is_using_io_print &&
            !(expr.children[0].kind == ExprKind::Identifier &&
              IsBuiltinCallIdentifierName(expr.children[0].text))) {
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
        std::string whole_module_name;
        if (GetModuleNameFromExpr(expr, &whole_module_name) &&
            (IsReservedModuleEnabled(ctx, whole_module_name) ||
             ctx.modules.find(whole_module_name) != ctx.modules.end() ||
             ctx.externs_by_module.find(whole_module_name) != ctx.externs_by_module.end())) {
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
            const auto ext_mod_it = ctx.externs_by_module.find(base.text);
            const bool has_extern_member = ext_mod_it != ctx.externs_by_module.end() &&
                                           ext_mod_it->second.find(expr.text) != ext_mod_it->second.end();
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text) &&
                !has_extern_member) {
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
            if (ResolveReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
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
            const auto ext_mod_it = ctx.externs_by_module.find(base.text);
            const bool has_extern_member = ext_mod_it != ctx.externs_by_module.end() &&
                                           ext_mod_it->second.find(expr.text) != ext_mod_it->second.end();
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text) &&
                !has_extern_member) {
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
            if (ResolveReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
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
        if (IsIoPrintCallExpr(expr, ctx)) return true;
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
          TypeRef var_type;
          CallTargetInfo info;
          if (ResolveReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
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
          TaggedTypeInfo tagged;
          if (ResolveTaggedType(base_type, ctx, &tagged)) {
            if (error) {
              *error = "tagged payload access requires exhaustive pattern binding or '?'";
            }
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
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
          if (base_type.dims.empty() && base_type.name != "string") {
            if (error) *error = "indexing is only valid on arrays, lists, and strings";
            return false;
          }
        } else if (expr.children[0].kind == ExprKind::Literal) {
          if (error) *error = "indexing is only valid on arrays, lists, and strings";
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
    if (!CheckUniqueParamName(param.name, &param_names, "duplicate parameter name: ", error)) return false;
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
  return CheckFunctionReturnFlow(fn, error);
}

} // namespace

static bool ValidateProgramImpl(
    const Program& program,
    std::unordered_map<const Expr*, std::vector<TypeRef>>* inferred_generic_calls,
    std::string* error) {
  ValidateContext ctx;
  ctx.inferred_generic_calls = inferred_generic_calls;
  std::vector<std::unordered_map<std::string, LocalInfo>> empty_scopes;
  empty_scopes.emplace_back();
  if (!CheckProgramHasDeclarationsOrTopLevelStatements(program, error)) return false;
  for (const auto& decl : program.decls) {
    const std::string* name_ptr = nullptr;
    switch (decl.kind) {
      case DeclKind::ModuleHeader:
        break;
      case DeclKind::Import:
      {
        if (decl.import_decl.is_using) {
          const auto alias_it = ctx.reserved_import_aliases.find(decl.import_decl.path);
          if (alias_it != ctx.reserved_import_aliases.end()) {
            ctx.reserved_imports.insert(alias_it->second);
            ctx.using_reserved_modules.insert(std::string(ToCanonicalName(alias_it->second)));
            break;
          }
          if (ctx.modules.find(decl.import_decl.path) != ctx.modules.end() ||
              ctx.externs_by_module.find(decl.import_decl.path) != ctx.externs_by_module.end()) {
            ctx.using_modules.insert(decl.import_decl.path);
            break;
          }
          if (ctx.imported_modules.find(decl.import_decl.path) != ctx.imported_modules.end()) {
            break;
          }
          const size_t dot = decl.import_decl.path.rfind('.');
          if (dot != std::string::npos) {
            const std::string leaf = decl.import_decl.path.substr(dot + 1);
            if (ctx.modules.find(leaf) != ctx.modules.end() ||
                ctx.externs_by_module.find(leaf) != ctx.externs_by_module.end()) {
              ctx.using_modules.insert(leaf);
              break;
            }
          }
          if (error) *error = "using requires prior import or namespace: " + decl.import_decl.path;
          return false;
        }
        const auto library_import = ParseLibraryImportPath(decl.import_decl.path);
        if (!library_import) {
          std::string replacement;
          if (LegacyReservedImportReplacement(decl.import_decl.path, &replacement)) {
            if (error) *error = "unsupported import path: " + decl.import_decl.path + "; use " + replacement;
            return false;
          }
          ctx.imported_modules.insert(decl.import_decl.path);
          break;
        }
        LibraryModuleId module_id{library_import->root, library_import->module_index};
        ctx.reserved_imports.insert(module_id);
        if (decl.import_decl.has_alias && !decl.import_decl.alias.empty()) {
          ctx.reserved_import_aliases[decl.import_decl.alias] = module_id;
        } else {
          ctx.reserved_import_aliases[decl.import_decl.path] = module_id;
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
            if (!CheckEnumMemberValue(member, error)) return false;
            if (!CheckUniqueNamedMember(member.name, &local_members, "duplicate enum member: ", error)) return false;
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
        for (const auto& ext : decl.module.externs) {
          ctx.externs_by_module[ext.module][ext.name] = &ext;
        }
        if (!decl.module.source_module.empty()) {
          const std::string qualified_module = decl.module.source_module + "." + decl.module.name;
          if (qualified_module != decl.module.name) {
            ctx.modules[qualified_module] = &decl.module;
            for (const auto& ext : decl.module.externs) {
              ctx.externs_by_module[qualified_module][ext.name] = &ext;
            }
          }
        }
        break;
      case DeclKind::Function:
        name_ptr = &decl.func.name;
        ctx.functions[decl.func.name] = &decl.func;
        break;
      case DeclKind::Variable:
        name_ptr = &decl.var.name;
        ctx.globals[decl.var.name] = &decl.var;
        if (decl.var.has_init_expr && decl.var.type.pointer_depth > 0) {
          std::vector<std::unordered_map<std::string, LocalInfo>> initializer_scopes;
          bool known = false;
          bool points_to_immutable = false;
          GetPointerImmutabilityFromExpr(decl.var.init_expr,
                                         ctx,
                                         initializer_scopes,
                                         nullptr,
                                         &known,
                                         &points_to_immutable);
          if (known) {
            ctx.global_points_to_immutable[decl.var.name] = points_to_immutable;
          }
        }
        break;
    }
    if (name_ptr) {
      size_t canonical_generic_arity = 0;
      if (TAST::CanonicalGenericTypeArity(*name_ptr, &canonical_generic_arity)) {
        if (error) *error = "cannot redeclare canonical generic type: " + *name_ptr;
        return false;
      }
      if (!CheckUniqueNamedMember(*name_ptr,
                                  &ctx.top_level,
                                  "duplicate top-level declaration: ",
                                  error)) {
        return false;
      }
    }
  }

  if (!program.top_level_stmts.empty()) {
    std::vector<std::unordered_map<std::string, LocalInfo>> scopes;
    scopes.emplace_back();
    std::unordered_set<std::string> type_params;
    TypeRef script_return;
    script_return.name = "i32";
    for (const auto& stmt : program.top_level_stmts) {
      if (!CheckTopLevelStmtAllowsReturn(stmt, error)) return false;
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
          if (!CheckExternAbiType(decl.ext.return_type,
                                  ctx.enum_types,
                                  ctx.artifacts,
                                  true,
                                  "extern ABI return type is not supported",
                                  error)) {
            return false;
          }
          for (const auto& param : decl.ext.params) {
            if (!CheckUniqueParamName(param.name, &param_names, "duplicate extern parameter name: ", error)) return false;
            if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
            if (!CheckExternAbiType(param.type,
                                    ctx.enum_types,
                                    ctx.artifacts,
                                    false,
                                    "extern ABI parameter type is not supported",
                                    error)) {
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
            if (!CheckUniqueNamedMember(field.name, &names, "duplicate artifact member: ", error)) return false;
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
            if (!CheckUniqueNamedMember(method.name, &names, "duplicate artifact member: ", error)) return false;
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
            if (!CheckUniqueNamedMember(var.name, &names, "duplicate module member: ", error)) return false;
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
            if (!CheckUniqueNamedMember(fn.name, &names, "duplicate module member: ", error)) return false;
          }
          for (const auto& ext : decl.module.externs) {
            if (!CheckUniqueNamedMember(ext.name, &names, "duplicate module member: ", error)) return false;
            std::unordered_set<std::string> param_names;
            std::unordered_set<std::string> type_params;
            if (!CheckTypeRef(ext.return_type, ctx, type_params, TypeUse::Return, error)) return false;
            if (!CheckExternAbiType(ext.return_type,
                                    ctx.enum_types,
                                    ctx.artifacts,
                                    true,
                                    "extern ABI return type is not supported",
                                    error)) {
              return false;
            }
            for (const auto& param : ext.params) {
              if (!CheckUniqueParamName(param.name, &param_names, "duplicate extern parameter name: ", error)) return false;
              if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
              if (!CheckExternAbiType(param.type,
                                      ctx.enum_types,
                                      ctx.artifacts,
                                      false,
                                      "extern ABI parameter type is not supported",
                                      error)) {
                return false;
              }
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

bool ValidateProgram(const Program& program, std::string* error) {
  return ValidateProgramImpl(program, nullptr, error);
}

bool TAST::AnnotateInferredGenericCallTypeArguments(Program* program,
                                                    std::string* error) {
  if (!program) return false;
  std::unordered_map<const Expr*, std::vector<TypeRef>> inferred_generic_calls;
  if (!ValidateProgramImpl(*program, &inferred_generic_calls, error)) return false;
  for (auto& entry : inferred_generic_calls) {
    auto* expression = const_cast<Expr*>(entry.first);
    expression->type_args = std::move(entry.second);
  }
  if (error) error->clear();
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
