#pragma once

#include <string>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::GEN {

struct GenericInstantiationRequest {
  std::string base_name;
  std::vector<std::string> argument_identities;
  uint32_t line = 0;
  uint32_t column = 0;
};

std::string TypeRefIdentity(const Simple::Lang::AST::TypeRef& type);
bool CollectInstantiationRequestsFromType(const Simple::Lang::AST::TypeRef& type,
                                          std::vector<GenericInstantiationRequest>* out);
bool CollectInstantiationRequestsFromProgram(const Simple::Lang::AST::Program& program,
                                             std::vector<GenericInstantiationRequest>* out);

} // namespace Simple::Lang::GEN
