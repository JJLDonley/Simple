#pragma once

#include <string>
#include <vector>

#include "AST/ast.h"
#include "TAST/generics.h"

namespace Simple::Lang::GEN {

struct GenericInstantiationRequest {
  std::string base_name;
  std::vector<std::string> argument_identities;
  uint32_t line = 0;
  uint32_t column = 0;
  std::vector<Simple::Lang::AST::TypeRef> argument_types;
};

struct GenericSpecializationBinding {
  std::string parameter_name;
  std::string type_identity;
  bool has_concrete_type = false;
  Simple::Lang::AST::TypeRef concrete_type;
};

struct GenericSpecializationPlan {
  GenericInstantiationRequest request;
  Simple::Lang::TAST::GenericDeclarationMetadata declaration;
  std::string specialized_symbol;
  std::vector<GenericSpecializationBinding> bindings;
};

std::string TypeRefIdentity(const Simple::Lang::AST::TypeRef& type);
bool CollectInstantiationRequestsFromType(const Simple::Lang::AST::TypeRef& type,
                                          std::vector<GenericInstantiationRequest>* out);
bool CollectInstantiationRequestsFromProgram(const Simple::Lang::AST::Program& program,
                                             std::vector<GenericInstantiationRequest>* out);
std::string InstantiationRequestKey(const GenericInstantiationRequest& request);
std::string SpecializedSymbolName(const GenericInstantiationRequest& request);
bool NormalizeInstantiationRequests(const std::vector<GenericInstantiationRequest>& requests,
                                    std::vector<GenericInstantiationRequest>* unique_requests);
bool BuildSpecializationPlan(const std::vector<Simple::Lang::TAST::GenericDeclarationMetadata>& declarations,
                             const std::vector<GenericInstantiationRequest>& requests,
                             std::vector<GenericSpecializationPlan>* out,
                             std::string* error);
bool BuildSpecializationPlanFromProgram(const Simple::Lang::AST::Program& program,
                                        std::vector<GenericSpecializationPlan>* out,
                                        std::string* error);
bool BuildGenericSubstitutionMap(const GenericSpecializationPlan& plan,
                                 Simple::Lang::TAST::GenericSubstitutionMap* out,
                                 std::string* error);
bool SpecializeFunctionDeclaration(const Simple::Lang::AST::FuncDecl& source,
                                   const GenericSpecializationPlan& plan,
                                   Simple::Lang::AST::FuncDecl* out,
                                   std::string* error);
bool SpecializeArtifactLayoutDeclaration(const Simple::Lang::AST::ArtifactDecl& source,
                                         const GenericSpecializationPlan& plan,
                                         Simple::Lang::AST::ArtifactDecl* out,
                                         std::string* error);
bool MaterializeConcreteProgram(const Simple::Lang::AST::Program& source,
                                const std::vector<GenericSpecializationPlan>& plans,
                                Simple::Lang::AST::Program* out,
                                std::string* error);
bool MaterializeProgramForEmission(const Simple::Lang::AST::Program& source,
                                   Simple::Lang::AST::Program* out,
                                   bool* materialized,
                                   std::string* error);

} // namespace Simple::Lang::GEN
