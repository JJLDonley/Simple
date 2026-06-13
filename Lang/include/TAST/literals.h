#pragma once

#include <string>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

bool InferLiteralType(const Simple::Lang::AST::Expr& expr,
                      const Simple::Lang::AST::TypeRef* expected,
                      Simple::Lang::AST::TypeRef* out,
                      std::string* error);

bool IsLiteralCompatibleWithType(const Simple::Lang::AST::Expr& expr,
                                 const Simple::Lang::AST::TypeRef& expected);

} // namespace Simple::Lang::TAST
