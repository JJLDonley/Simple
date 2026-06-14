#include "TAST/generics.h"

#include <utility>

#include "TAST/types.h"

namespace Simple::Lang::TAST {

bool ApplyTypeSubstitution(Simple::Lang::AST::TypeRef* type,
                           const GenericSubstitutionMap& substitutions) {
  if (!type) return false;
  for (auto& arg : type->type_args) {
    if (!ApplyTypeSubstitution(&arg, substitutions)) return false;
  }
  if (type->is_proc) {
    for (auto& param : type->proc_params) {
      if (!ApplyTypeSubstitution(&param, substitutions)) return false;
    }
    if (type->proc_return) {
      if (!ApplyTypeSubstitution(type->proc_return.get(), substitutions)) return false;
    }
  }
  auto it = substitutions.find(type->name);
  if (it == substitutions.end()) return true;
  Simple::Lang::AST::TypeRef replacement;
  if (!CloneTypeRef(it->second, &replacement)) return false;
  replacement.pointer_depth += type->pointer_depth;
  if (!type->dims.empty()) {
    replacement.dims.insert(replacement.dims.end(), type->dims.begin(), type->dims.end());
  }
  *type = std::move(replacement);
  return true;
}

bool SubstituteTypeParams(const Simple::Lang::AST::TypeRef& src,
                          const GenericSubstitutionMap& substitutions,
                          Simple::Lang::AST::TypeRef* out) {
  if (!out) return false;
  if (!CloneTypeRef(src, out)) return false;
  return ApplyTypeSubstitution(out, substitutions);
}

bool BuildArtifactTypeParamMap(const Simple::Lang::AST::TypeRef& instance_type,
                               const Simple::Lang::AST::ArtifactDecl* artifact,
                               GenericSubstitutionMap* out,
                               std::string* error) {
  if (!out) return false;
  out->clear();
  if (!artifact) return false;
  if (artifact->generics.empty()) return true;
  if (instance_type.type_args.size() != artifact->generics.size()) {
    if (error) {
      *error = "generic type argument count mismatch for " + artifact->name;
    }
    return false;
  }
  for (size_t i = 0; i < artifact->generics.size(); ++i) {
    Simple::Lang::AST::TypeRef copy;
    if (!CloneTypeRef(instance_type.type_args[i], &copy)) return false;
    (*out)[artifact->generics[i]] = std::move(copy);
  }
  return true;
}

bool SubstituteGenericTypes(const Simple::Lang::AST::TypeRef& input,
                            const GenericSubstitutionMap& substitutions,
                            Simple::Lang::AST::TypeRef* out) {
  if (!out) return false;
  auto it = substitutions.find(input.name);
  if (it != substitutions.end() && input.pointer_depth == 0 && input.dims.empty() &&
      input.type_args.empty() && !input.is_proc) {
    *out = it->second;
    return true;
  }
  *out = input;
  for (auto& arg : out->type_args) {
    Simple::Lang::AST::TypeRef substituted;
    if (!SubstituteGenericTypes(arg, substitutions, &substituted)) return false;
    arg = std::move(substituted);
  }
  for (auto& param : out->proc_params) {
    Simple::Lang::AST::TypeRef substituted;
    if (!SubstituteGenericTypes(param, substitutions, &substituted)) return false;
    param = std::move(substituted);
  }
  if (out->proc_return) {
    Simple::Lang::AST::TypeRef substituted;
    if (!SubstituteGenericTypes(*out->proc_return, substitutions, &substituted)) return false;
    *out->proc_return = std::move(substituted);
  }
  return true;
}

} // namespace Simple::Lang::TAST
