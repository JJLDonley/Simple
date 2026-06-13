#pragma once

#include <string>
#include <unordered_map>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

using GenericSubstitutionMap = std::unordered_map<std::string, Simple::Lang::AST::TypeRef>;

bool SubstituteGenericTypes(const Simple::Lang::AST::TypeRef& input,
                            const GenericSubstitutionMap& substitutions,
                            Simple::Lang::AST::TypeRef* out);

} // namespace Simple::Lang::TAST
