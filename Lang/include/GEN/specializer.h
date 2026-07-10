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
};

struct GenericInstantiationNode {
  GenericInstantiationRequest request;
  std::vector<GenericInstantiationRequest> dependencies;
};

struct GenericSpecializationPlan {
  GenericInstantiationRequest request;
  Simple::Lang::TAST::GenericDeclarationMetadata declaration;
  std::string specialized_symbol;
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
bool ResolveInstantiationOrder(const std::vector<GenericInstantiationNode>& nodes,
                               std::vector<GenericInstantiationRequest>* ordered_requests,
                               std::string* error);
bool BuildSpecializationPlan(const std::vector<Simple::Lang::TAST::GenericDeclarationMetadata>& declarations,
                             const std::vector<GenericInstantiationRequest>& requests,
                             std::vector<GenericSpecializationPlan>* out,
                             std::string* error);

} // namespace Simple::Lang::GEN
