#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool CheckCallExpression(const Expr& expr, std::string* error);
bool CheckFunctionCallArgs(const Simple::Lang::AST::FuncDecl* fn,
                           size_t arg_count,
                           std::string* error);
bool CheckProcTypeArgs(const TypeRef* type, size_t arg_count, std::string* error);
bool CheckCallTypeArgCount(size_t type_param_count,
                           size_t explicit_type_arg_count,
                           std::string* error);
bool CheckReservedMathCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error);
bool CheckReservedTimeCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error);
bool CheckReservedIoBufferCallArgTypes(const std::string& member,
                                       const std::vector<TypeRef>& args,
                                       std::string* error);
bool CheckReservedFileCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error);
bool CheckReservedDlOpenArgTypes(const std::vector<TypeRef>& args,
                                 std::string* error);
bool CheckScalarCallArgTypes(const std::vector<TypeRef>& args,
                             const std::string& error_message,
                             std::string* error);
bool CheckFormatCallArgTypes(const std::vector<TypeRef>& args, std::string* error);
bool CheckIoPrintCallArgTypes(const std::vector<TypeRef>& args, std::string* error);
bool CheckSingleArgCallCount(const std::string& name, size_t arg_count, std::string* error);
bool CheckFnLiteralAgainstType(const Expr& fn_expr,
                               const TypeRef& target_type,
                               std::string* error);

} // namespace Simple::Lang::TAST
