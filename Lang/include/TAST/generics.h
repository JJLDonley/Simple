#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

using GenericSubstitutionMap = std::unordered_map<std::string, Simple::Lang::AST::TypeRef>;

enum class GenericDeclarationKind {
  Function,
  Data,
  Artifact,
  Method,
};

struct GenericDeclarationMetadata {
  GenericDeclarationKind kind = GenericDeclarationKind::Function;
  std::string owner_name;
  std::string name;
  std::vector<std::string> type_params;
};

bool CollectGenericDeclarationMetadata(const Simple::Lang::AST::Program& program,
                                       std::vector<GenericDeclarationMetadata>* out,
                                       std::string* error);

bool CollectTypeParams(const std::vector<std::string>& generics,
                       std::unordered_set<std::string>* out,
                       std::string* error);
bool CollectTypeParamsMerged(const std::vector<std::string>& a,
                             const std::vector<std::string>& b,
                             std::unordered_set<std::string>* out,
                             std::string* error);

bool ApplyTypeSubstitution(Simple::Lang::AST::TypeRef* type,
                           const GenericSubstitutionMap& substitutions);
bool SubstituteTypeParams(const Simple::Lang::AST::TypeRef& src,
                          const GenericSubstitutionMap& substitutions,
                          Simple::Lang::AST::TypeRef* out);
bool BuildArtifactTypeParamMap(const Simple::Lang::AST::TypeRef& instance_type,
                               const Simple::Lang::AST::ArtifactDecl* artifact,
                               GenericSubstitutionMap* out,
                               std::string* error);
bool BuildExplicitTypeArgMap(const std::vector<std::string>& type_params,
                             const std::vector<Simple::Lang::AST::TypeRef>& explicit_args,
                             GenericSubstitutionMap* out,
                             std::string* error);
bool UnifyTypeParams(const Simple::Lang::AST::TypeRef& param,
                     const Simple::Lang::AST::TypeRef& arg,
                     const std::unordered_set<std::string>& type_params,
                     GenericSubstitutionMap* mapping);
bool SubstituteGenericTypes(const Simple::Lang::AST::TypeRef& input,
                            const GenericSubstitutionMap& substitutions,
                            Simple::Lang::AST::TypeRef* out);

} // namespace Simple::Lang::TAST
