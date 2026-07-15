#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

bool CountFormatPlaceholders(const std::string& fmt,
                             size_t* out_count,
                             std::string* error);
bool CheckFormatPlaceholderCount(const std::string& fmt,
                                 size_t value_count,
                                 const std::string& context,
                                 std::string* error);
bool CheckAggregateLiteralPositionalCount(const Simple::Lang::AST::Expr& expr,
                                         size_t field_count,
                                         std::string* error);
bool CheckAggregateLiteralDuplicateNamedFields(const Simple::Lang::AST::Expr& expr,
                                              std::string* error);
bool CheckAggregateLiteralFieldSpecifiedOnce(const std::string& field_name,
                                            const std::unordered_set<std::string>& seen,
                                            std::string* error);
bool CheckAggregateLiteralKnownField(const std::string& field_name,
                                    const std::unordered_set<std::string>& valid_fields,
                                    std::string* error);
bool CheckAggregateLiteralRequiredField(const std::string& field_name,
                                       bool has_init_expr,
                                       const std::unordered_set<std::string>& seen,
                                       std::string* error);
bool CheckArrayListLiteralTargetShape(const Simple::Lang::AST::TypeRef& target_type,
                                      const Simple::Lang::AST::Expr& literal_expr,
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
bool CheckTypesCompatibleForExpr(const Simple::Lang::AST::TypeRef& expected,
                                 const Simple::Lang::AST::TypeRef& actual,
                                 const Simple::Lang::AST::Expr& expr,
                                 const std::string& error_message,
                                 std::string* error);
bool IsListLiteralExpr(const Simple::Lang::AST::Expr& expr);
bool CheckArrayLiteralShape(const Simple::Lang::AST::Expr& expr,
                            const std::vector<Simple::Lang::AST::TypeDim>& dims,
                            size_t dim_index,
                            std::string* error);
bool IsPositionalBraceLiteralExpr(const Simple::Lang::AST::Expr& expr);

} // namespace Simple::Lang::TAST
