#include "TAST/calls.h"

#include "TAST/types.h"

namespace Simple::Lang::TAST {

namespace {

bool LooksLikeFnLiteralStart(const std::vector<Token>& tokens, size_t index) {
  if (index >= tokens.size() || tokens[index].kind != TokenKind::LParen) return false;
  ++index;
  if (index < tokens.size() && tokens[index].kind == TokenKind::RParen) {
    ++index;
    return index < tokens.size() && tokens[index].kind == TokenKind::LBrace;
  }
  bool expect_name = true;
  while (index < tokens.size()) {
    if (expect_name) {
      if (tokens[index].kind != TokenKind::Identifier) return false;
      expect_name = false;
      ++index;
      continue;
    }
    if (tokens[index].kind == TokenKind::Comma) {
      expect_name = true;
      ++index;
      continue;
    }
    if (tokens[index].kind == TokenKind::RParen) {
      ++index;
      return index < tokens.size() && tokens[index].kind == TokenKind::LBrace;
    }
    return false;
  }
  return false;
}

bool IsI32BufferType(const TypeRef& type) {
  return type.name == "i32" && !type.is_proc && type.type_args.empty() && type.dims.size() == 1;
}

bool ContainsNestedFnLiteralTokens(const std::vector<Token>& tokens) {
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    if ((tokens[i].kind == TokenKind::Assign || tokens[i].kind == TokenKind::KwReturn ||
         tokens[i].kind == TokenKind::Comma || tokens[i].kind == TokenKind::LBracket) &&
        LooksLikeFnLiteralStart(tokens, i + 1)) {
      return true;
    }
  }
  return false;
}

} // namespace

bool IsCallExpr(const Expr& expr, const Expr** out_callee) {
  if (expr.kind != ExprKind::Call || expr.children.empty()) return false;
  if (out_callee) *out_callee = &expr.children[0];
  return true;
}

bool CheckFunctionCallArgs(const Simple::Lang::AST::FuncDecl* fn,
                           size_t arg_count,
                           std::string* error) {
  if (!fn) return false;
  if (fn->params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch for " + fn->name + ": expected " +
               std::to_string(fn->params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckProcTypeArgs(const TypeRef* type, size_t arg_count, std::string* error) {
  if (!type || !type->is_proc) return false;
  if (type->proc_params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch: expected " +
               std::to_string(type->proc_params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckCallTypeArgCount(size_t type_param_count,
                           size_t explicit_type_arg_count,
                           std::string* error) {
  if (type_param_count > 0 && explicit_type_arg_count > 0 && explicit_type_arg_count != type_param_count) {
    if (error) {
      *error = "generic type argument count mismatch: expected " +
               std::to_string(type_param_count) + ", got " + std::to_string(explicit_type_arg_count);
    }
    return false;
  }
  if (type_param_count == 0 && explicit_type_arg_count > 0) {
    if (error) *error = "non-generic call cannot take type arguments";
    return false;
  }
  return true;
}

bool CheckReservedMathCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  if (member == "abs") {
    if (args.size() != 1) return true;
    const TypeRef& arg = args[0];
    if ((arg.name != "i32" && arg.name != "i64") || !arg.dims.empty() || arg.is_proc) {
      if (error) *error = "Math.abs expects i32 or i64 argument";
      return false;
    }
    return true;
  }
  if (member == "sqrt") {
    if (args.size() != 1) return true;
    const TypeRef& arg = args[0];
    if ((arg.name != "f32" && arg.name != "f64") || !arg.dims.empty() || arg.is_proc) {
      if (error) *error = "Math.sqrt expects f32 or f64 argument";
      return false;
    }
    return true;
  }
  if (member == "min" || member == "max") {
    if (args.size() != 2) return true;
    const TypeRef& a = args[0];
    const TypeRef& b = args[1];
    auto allowed = [](const TypeRef& t) {
      return t.name == "i32" || t.name == "i64" || t.name == "f32" || t.name == "f64";
    };
    if (!allowed(a) || !allowed(b) || !TypeEquals(a, b) || !a.dims.empty() || !b.dims.empty()) {
      if (error) *error = "Math." + member + " expects two numeric arguments of the same type";
      return false;
    }
    return true;
  }
  return true;
}

bool IsFormatArgTypeName(const std::string& name) {
  return IsNumericTypeName(name) || IsBoolTypeName(name) || name == "string";
}

bool IsIoPrintArgTypeName(const std::string& name) {
  return IsNumericTypeName(name) || IsBoolTypeName(name) || name == "char" || name == "string";
}

bool CheckScalarCallArgTypes(const std::vector<TypeRef>& args,
                             const std::string& error_message,
                             std::string* error) {
  for (const auto& arg : args) {
    if (!IsScalarType(arg)) {
      if (error) *error = error_message;
      return false;
    }
  }
  return true;
}

bool CheckFormatCallArgTypes(const std::vector<TypeRef>& args, std::string* error) {
  if (!CheckScalarCallArgTypes(args, "format expects scalar arguments", error)) return false;
  for (const auto& arg : args) {
    if (!IsFormatArgTypeName(arg.name)) {
      if (error) *error = "format supports numeric, bool, or string";
      return false;
    }
  }
  return true;
}

bool CheckIoPrintCallArgTypes(const std::vector<TypeRef>& args, std::string* error) {
  if (!CheckScalarCallArgTypes(args,
                               args.size() == 1 ? "IO.print expects scalar argument"
                                                : "IO.print format expects scalar arguments",
                               error)) {
    return false;
  }
  for (const auto& arg : args) {
    if (!IsIoPrintArgTypeName(arg.name)) {
      if (error) *error = "IO.print supports numeric, bool, char, or string";
      return false;
    }
  }
  return true;
}

bool CheckIoPrintFormatTemplateArg(const Expr& expr, std::string* error) {
  if (expr.kind != ExprKind::Literal || expr.literal_kind != LiteralKind::String) {
    if (error) *error = "IO.print format call expects string literal as first argument";
    return false;
  }
  return true;
}

bool CheckSingleArgCallCount(const std::string& name, size_t arg_count, std::string* error) {
  if (arg_count != 1) {
    if (error) {
      *error = "call argument count mismatch for " + name + ": expected 1, got " +
               std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckReservedDlOpenArgTypes(const std::vector<TypeRef>& args,
                                 std::string* error) {
  if (args.size() != 1 && args.size() != 2) {
    if (error) *error = "DL.open expects (string) or (string, manifest)";
    return false;
  }
  const TypeRef& path = args[0];
  if (path.name != "string" || !path.dims.empty()) {
    if (error) *error = "DL.open expects first argument string path";
    return false;
  }
  return true;
}

bool CheckReservedFileCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  if (member == "open") {
    if (args.size() != 2) return true;
    const TypeRef& path = args[0];
    const TypeRef& flags = args[1];
    if (path.name != "string" || !path.dims.empty() || flags.name != "i32" || !flags.dims.empty()) {
      if (error) *error = "File.open expects (string, i32)";
      return false;
    }
    return true;
  }
  if (member == "close") {
    if (args.size() != 1) return true;
    const TypeRef& fd = args[0];
    if (fd.name != "i32" || !fd.dims.empty()) {
      if (error) *error = "File.close expects (i32)";
      return false;
    }
    return true;
  }
  if (member == "read" || member == "write") {
    if (args.size() != 3) return true;
    const TypeRef& fd = args[0];
    const TypeRef& buf = args[1];
    const TypeRef& len = args[2];
    if (fd.name != "i32" || !fd.dims.empty() || len.name != "i32" || !len.dims.empty() ||
        !IsI32BufferType(buf)) {
      if (error) *error = "File." + member + " expects (i32, i32[], i32)";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckReservedIoBufferCallArgTypes(const std::string& member,
                                       const std::vector<TypeRef>& args,
                                       std::string* error) {
  if (member == "buffer_new") {
    if (args.size() != 1) return true;
    const TypeRef& len = args[0];
    if (len.name != "i32" || !len.dims.empty()) {
      if (error) *error = "IO.buffer_new expects (i32)";
      return false;
    }
    return true;
  }
  if (member == "buffer_len") {
    if (args.size() != 1) return true;
    if (!IsI32BufferType(args[0])) {
      if (error) *error = "IO.buffer_len expects (i32[])";
      return false;
    }
    return true;
  }
  if (member == "buffer_fill") {
    if (args.size() != 3) return true;
    const TypeRef& buf = args[0];
    const TypeRef& value = args[1];
    const TypeRef& count = args[2];
    if (!IsI32BufferType(buf) || value.name != "i32" || !value.dims.empty() ||
        count.name != "i32" || !count.dims.empty()) {
      if (error) *error = "IO.buffer_fill expects (i32[], i32, i32)";
      return false;
    }
    return true;
  }
  if (member == "buffer_copy") {
    if (args.size() != 3) return true;
    const TypeRef& dst = args[0];
    const TypeRef& src = args[1];
    const TypeRef& count = args[2];
    if (!IsI32BufferType(dst) || !IsI32BufferType(src) || count.name != "i32" || !count.dims.empty()) {
      if (error) *error = "IO.buffer_copy expects (i32[], i32[], i32)";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckReservedTimeCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  if (member == "mono_ns" || member == "wall_ns") {
    if (!args.empty()) {
      if (error) *error = "Time." + member + " expects no arguments";
      return false;
    }
    return true;
  }
  if (member == "formatWallNs") {
    if (args.size() != 1) {
      if (error) *error = "Time.formatWallNs expects (i64)";
      return false;
    }
    const TypeRef& ns = args[0];
    if (ns.name != "i64" || !ns.dims.empty()) {
      if (error) *error = "Time.formatWallNs expects (i64)";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckFnLiteralAgainstType(const Expr& fn_expr,
                               const TypeRef& target_type,
                               std::string* error) {
  if (!target_type.is_proc) {
    if (error) *error = "fn literal requires procedure type";
    return false;
  }
  if (fn_expr.fn_params.size() != target_type.proc_params.size()) {
    if (error) {
      *error = "fn literal parameter count mismatch: expected " +
               std::to_string(target_type.proc_params.size()) + ", got " +
               std::to_string(fn_expr.fn_params.size());
    }
    return false;
  }
  for (size_t i = 0; i < fn_expr.fn_params.size(); ++i) {
    if (fn_expr.fn_params[i].type.name.empty()) continue;
    if (!TypeEquals(fn_expr.fn_params[i].type, target_type.proc_params[i])) {
      if (error) *error = "fn literal parameter type mismatch";
      return false;
    }
  }
  if (ContainsNestedFnLiteralTokens(fn_expr.fn_body_tokens)) {
    if (error) *error = "nested fn literals are not supported";
    return false;
  }
  return true;
}

bool CheckCallExpression(const Expr& expr, std::string* error) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Call) {
    if (error) *error = "expected call expression";
    return false;
  }
  if (expr.children.empty()) {
    if (error) *error = "call expression missing callee";
    return false;
  }
  return true;
}

} // namespace Simple::Lang::TAST
