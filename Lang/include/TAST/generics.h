#pragma once

#include <string>
#include <unordered_map>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

using GenericSubstitutionMap = std::unordered_map<std::string, Simple::Lang::AST::TypeRef>;

bool ApplyTypeSubstitution(Simple::Lang::AST::TypeRef* type,
                           const GenericSubstitutionMap& substitutions);
bool SubstituteTypeParams(const Simple::Lang::AST::TypeRef& src,
                          const GenericSubstitutionMap& substitutions,
                          Simple::Lang::AST::TypeRef* out);
bool BuildArtifactTypeParamMap(const Simple::Lang::AST::TypeRef& instance_type,
                               const Simple::Lang::AST::ArtifactDecl* artifact,
                               GenericSubstitutionMap* out,
                               std::string* error);
bool SubstituteGenericTypes(const Simple::Lang::AST::TypeRef& input,
                            const GenericSubstitutionMap& substitutions,
                            Simple::Lang::AST::TypeRef* out);

} // namespace Simple::Lang::TAST
