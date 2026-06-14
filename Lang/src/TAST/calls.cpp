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
