#include "TAST/generics.h"

#include <utility>

namespace Simple::Lang::TAST {

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
