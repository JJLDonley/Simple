#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

bool CountFormatPlaceholders(const std::string& fmt,
                             size_t* out_count,
                             std::string* error);
bool InferLiteralType(const Simple::Lang::AST::Expr& expr,
                      const Simple::Lang::AST::TypeRef* expected,
                      Simple::Lang::AST::TypeRef* out,
                      std::string* error);

bool IsLiteralCompatibleWithType(const Simple::Lang::AST::Expr& expr,
                                 const Simple::Lang::AST::TypeRef& expected);
bool IsLiteralCompatibleWithScalarType(const Simple::Lang::AST::Expr& expr,
                                       const Simple::Lang::AST::TypeRef& expected);
bool TypesCompatibleForExpr(const Simple::Lang::AST::TypeRef& expected,
                            const Simple::Lang::AST::TypeRef& actual,
                            const Simple::Lang::AST::Expr& expr);
bool IsListLiteralExpr(const Simple::Lang::AST::Expr& expr);
bool CheckArrayLiteralShape(const Simple::Lang::AST::Expr& expr,
                            const std::vector<Simple::Lang::AST::TypeDim>& dims,
                            size_t dim_index,
                            std::string* error);
bool IsPositionalBraceLiteralExpr(const Simple::Lang::AST::Expr& expr);

} // namespace Simple::Lang::TAST
