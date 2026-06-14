#include "TAST/generics.h"

#include <utility>

#include "TAST/types.h"

namespace Simple::Lang::TAST {

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

bool CollectTypeParamsMerged(const std::vector<std::string>& a,
                             const std::vector<std::string>& b,
                             std::unordered_set<std::string>* out,
                             std::string* error) {
  if (!out) return false;
  out->clear();
  for (const auto& name : a) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  for (const auto& name : b) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  return true;
}

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

bool BuildExplicitTypeArgMap(const std::vector<std::string>& type_params,
                             const std::vector<Simple::Lang::AST::TypeRef>& explicit_args,
                             GenericSubstitutionMap* out,
                             std::string* error) {
  if (!out) return false;
  out->clear();
  if (type_params.size() != explicit_args.size()) {
    if (error) {
      *error = "generic type argument count mismatch: expected " +
               std::to_string(type_params.size()) + ", got " + std::to_string(explicit_args.size());
    }
    return false;
  }
  for (size_t i = 0; i < type_params.size(); ++i) {
    Simple::Lang::AST::TypeRef copy;
    if (!CloneTypeRef(explicit_args[i], &copy)) return false;
    (*out)[type_params[i]] = std::move(copy);
  }
  return true;
}

bool UnifyTypeParams(const Simple::Lang::AST::TypeRef& param,
                     const Simple::Lang::AST::TypeRef& arg,
                     const std::unordered_set<std::string>& type_params,
                     GenericSubstitutionMap* mapping) {
  if (!mapping) return false;
  if (type_params.find(param.name) != type_params.end()) {
    if (!param.dims.empty()) {
      if (!TypeDimsEqual(param.dims, arg.dims)) return false;
      Simple::Lang::AST::TypeRef base;
      if (!CloneTypeRef(arg, &base)) return false;
      base.dims.clear();
      auto it = mapping->find(param.name);
      if (it == mapping->end()) {
        (*mapping)[param.name] = std::move(base);
        return true;
      }
      return TypeEquals(it->second, base);
    }
    auto it = mapping->find(param.name);
    if (it == mapping->end()) {
      Simple::Lang::AST::TypeRef copy;
      if (!CloneTypeRef(arg, &copy)) return false;
      (*mapping)[param.name] = std::move(copy);
      return true;
    }
    return TypeEquals(it->second, arg);
  }
  if (param.pointer_depth != arg.pointer_depth) return false;
  if (param.is_proc != arg.is_proc) return false;
  if (!TypeDimsEqual(param.dims, arg.dims)) return false;
  if (param.name != arg.name) return false;
  if (param.type_args.size() != arg.type_args.size()) return false;
  for (size_t i = 0; i < param.type_args.size(); ++i) {
    if (!UnifyTypeParams(param.type_args[i], arg.type_args[i], type_params, mapping)) return false;
  }
  if (param.is_proc) {
    if (param.proc_params.size() != arg.proc_params.size()) return false;
    for (size_t i = 0; i < param.proc_params.size(); ++i) {
      if (!UnifyTypeParams(param.proc_params[i], arg.proc_params[i], type_params, mapping)) return false;
    }
    if (param.proc_return && arg.proc_return) {
      if (!UnifyTypeParams(*param.proc_return, *arg.proc_return, type_params, mapping)) return false;
    } else if (param.proc_return || arg.proc_return) {
      return false;
    }
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
