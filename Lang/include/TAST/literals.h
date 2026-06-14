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
bool IsListLiteralExpr(const Simple::Lang::AST::Expr& expr);
bool IsPositionalBraceLiteralExpr(const Simple::Lang::AST::Expr& expr);

} // namespace Simple::Lang::TAST
