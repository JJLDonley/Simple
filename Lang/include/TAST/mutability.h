#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

template <typename LocalInfo>
const LocalInfo* FindLocal(const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const std::string& name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
}

template <typename LocalInfo>
bool AddLocal(std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
              const std::string& name,
              const LocalInfo& info,
              std::string* error) {
  if (scopes.empty()) scopes.emplace_back();
  auto& current = scopes.back();
  if (!current.emplace(name, info).second) {
    if (error) *error = "duplicate local declaration: " + name;
    return false;
  }
  return true;
}

bool IsMutable(Simple::Lang::Mutability mutability);
bool IsAddressOfExpr(const Simple::Lang::AST::Expr& expr,
                     const Simple::Lang::AST::Expr** out_target = nullptr);
bool IsIndexExpr(const Simple::Lang::AST::Expr& expr,
                 const Simple::Lang::AST::Expr** out_base = nullptr);
bool CheckMutableAssignment(Simple::Lang::Mutability mutability,
                            std::string* error);

} // namespace Simple::Lang::TAST
