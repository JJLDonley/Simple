#include "TAST/generics.h"

#include <utility>

#include "TAST/types.h"

namespace Simple::Lang::TAST {

bool CollectGenericDeclarationMetadata(const Simple::Lang::AST::Program& program,
                                       std::vector<GenericDeclarationMetadata>* out,
                                       std::string* error) {
  if (!out) return false;
  out->clear();
  auto append = [&](GenericDeclarationKind kind,
                    std::string owner,
                    std::string name,
                    const std::vector<std::string>& type_params) -> bool {
    if (type_params.empty()) return true;
    std::unordered_set<std::string> collected;
    if (!CollectTypeParams(type_params, &collected, error)) return false;
    GenericDeclarationMetadata metadata;
    metadata.kind = kind;
    metadata.owner_name = std::move(owner);
    metadata.name = std::move(name);
    metadata.type_params = type_params;
    out->push_back(std::move(metadata));
    return true;
  };

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case Simple::Lang::AST::DeclKind::Function:
        if (!append(GenericDeclarationKind::Function, {}, decl.func.name, decl.func.generics)) {
          return false;
        }
        break;
      case Simple::Lang::AST::DeclKind::Aggregate:
        if (!append(decl.aggregate.is_struct ? GenericDeclarationKind::Data
                                          : GenericDeclarationKind::Aggregate,
                    {},
                    decl.aggregate.name,
                    decl.aggregate.generics)) {
          return false;
        }
        for (const auto& method : decl.aggregate.methods) {
          std::unordered_set<std::string> merged_set;
          if (!CollectTypeParamsMerged(decl.aggregate.generics, method.generics, &merged_set, error)) {
            return false;
          }
          std::vector<std::string> merged = decl.aggregate.generics;
          merged.insert(merged.end(), method.generics.begin(), method.generics.end());
          if (!append(GenericDeclarationKind::Method, decl.aggregate.name, method.name, merged)) {
            return false;
          }
        }
        break;
      case Simple::Lang::AST::DeclKind::Module:
        for (const auto& function : decl.module.functions) {
          if (!append(GenericDeclarationKind::Function,
                      decl.module.name,
                      function.name,
                      function.generics)) {
            return false;
          }
        }
        break;
      default:
        break;
    }
  }
  return true;
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
    replacement.dims.insert(replacement.dims.begin(), type->dims.begin(), type->dims.end());
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

bool BuildAggregateTypeParamMap(const Simple::Lang::AST::TypeRef& instance_type,
                               const Simple::Lang::AST::AggregateDecl* aggregate,
                               GenericSubstitutionMap* out,
                               std::string* error) {
  if (!out) return false;
  out->clear();
  if (!aggregate) return false;
  if (aggregate->generics.empty()) return true;
  if (instance_type.type_args.size() != aggregate->generics.size()) {
    if (error) {
      *error = "generic type argument count mismatch for " + aggregate->name;
    }
    return false;
  }
  for (size_t i = 0; i < aggregate->generics.size(); ++i) {
    Simple::Lang::AST::TypeRef copy;
    if (!CloneTypeRef(instance_type.type_args[i], &copy)) return false;
    (*out)[aggregate->generics[i]] = std::move(copy);
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
    if (arg.pointer_depth < param.pointer_depth ||
        arg.dims.size() < param.dims.size()) {
      return false;
    }
    for (size_t i = 0; i < param.dims.size(); ++i) {
      if (param.dims[i].is_list != arg.dims[i].is_list ||
          param.dims[i].has_size != arg.dims[i].has_size ||
          (param.dims[i].has_size && param.dims[i].size != arg.dims[i].size)) {
        return false;
      }
    }
    Simple::Lang::AST::TypeRef base;
    if (!CloneTypeRef(arg, &base)) return false;
    base.pointer_depth -= param.pointer_depth;
    base.dims.erase(base.dims.begin(), base.dims.begin() + param.dims.size());
    auto it = mapping->find(param.name);
    if (it == mapping->end()) {
      (*mapping)[param.name] = std::move(base);
      return true;
    }
    return TypeEquals(it->second, base);
  }
  if (param.pointer_depth != arg.pointer_depth) return false;
  if (param.is_optional_syntax != arg.is_optional_syntax) return false;
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

} // namespace Simple::Lang::TAST
