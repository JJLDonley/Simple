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

bool CheckUniqueParamName(const std::string& name,
                          std::unordered_set<std::string>* seen,
                          const std::string& error_prefix,
                          std::string* error) {
  if (!seen) return false;
  if (!seen->insert(name).second) {
    if (error) *error = error_prefix + name;
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

bool CheckReservedMathCallArgTypes(StandardMathMember member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  switch (member) {
    case StandardMathMember::Abs: {
      if (args.size() != 1) return true;
      const TypeRef& arg = args[0];
      if ((arg.name != "i32" && arg.name != "i64") || !arg.dims.empty() || arg.is_proc) {
        if (error) *error = "Math.abs expects i32 or i64 argument";
        return false;
      }
      return true;
    }
    case StandardMathMember::Sqrt: {
      if (args.size() != 1) return true;
      const TypeRef& arg = args[0];
      if ((arg.name != "f32" && arg.name != "f64") || !arg.dims.empty() || arg.is_proc) {
        if (error) *error = "Math.sqrt expects f32 or f64 argument";
        return false;
      }
      return true;
    }
    case StandardMathMember::Min:
    case StandardMathMember::Max: {
      if (args.size() != 2) return true;
      const TypeRef& a = args[0];
      const TypeRef& b = args[1];
      auto allowed = [](const TypeRef& t) {
        return t.name == "i32" || t.name == "i64" || t.name == "f32" || t.name == "f64";
      };
      if (!allowed(a) || !allowed(b) || !TypeEquals(a, b) || !a.dims.empty() || !b.dims.empty()) {
        if (error) *error = "Math." + std::string(ToMember(member)) + " expects two numeric arguments of the same type";
        return false;
      }
      return true;
    }
    case StandardMathMember::PI:
    case StandardMathMember::Clamp:
    case StandardMathMember::Lerp:
      return true;
  }
  return true;
}

bool CheckReservedMathCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  const auto parsed = ParseMember(StandardModule::Math, member);
  if (!parsed || !std::holds_alternative<StandardMathMember>(*parsed)) return true;
  return CheckReservedMathCallArgTypes(std::get<StandardMathMember>(*parsed), args, error);
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
                               args.size() == 1 ? "Standard.IO.print expects scalar argument"
                                                : "Standard.IO.print format expects scalar arguments",
                               error)) {
    return false;
  }
  for (const auto& arg : args) {
    if (!IsIoPrintArgTypeName(arg.name)) {
      if (error) *error = "Standard.IO.print supports numeric, bool, char, or string";
      return false;
    }
  }
  return true;
}

bool CheckIoPrintFormatTemplateArg(const Expr& expr, std::string* error) {
  if (expr.kind != ExprKind::Literal || expr.literal_kind != LiteralKind::String) {
    if (error) *error = "Standard.IO.print format call expects string literal as first argument";
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
    if (error) *error = "System.FFI.open expects (string) or (string, manifest)";
    return false;
  }
  const TypeRef& path = args[0];
  if (path.name != "string" || !path.dims.empty()) {
    if (error) *error = "System.FFI.open expects first argument string path";
    return false;
  }
  return true;
}

bool CheckReservedFileCallArgTypes(SystemFSMember member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  switch (member) {
    case SystemFSMember::Open: {
      if (args.size() != 2) return true;
      const TypeRef& path = args[0];
      const TypeRef& flags = args[1];
      if (path.name != "string" || !path.dims.empty() || flags.name != "i32" || !flags.dims.empty()) {
        if (error) *error = "System.FS.open expects (string, i32)";
        return false;
      }
      return true;
    }
    case SystemFSMember::Close: {
      if (args.size() != 1) return true;
      const TypeRef& fd = args[0];
      if (fd.name != "i32" || !fd.dims.empty()) {
        if (error) *error = "System.FS.close expects (i32)";
        return false;
      }
      return true;
    }
    case SystemFSMember::Read:
    case SystemFSMember::Write: {
      if (args.size() != 3) return true;
      const TypeRef& fd = args[0];
      const TypeRef& buf = args[1];
      const TypeRef& len = args[2];
      if (fd.name != "i32" || !fd.dims.empty() || len.name != "i32" || !len.dims.empty() ||
          !IsI32BufferType(buf)) {
        if (error) *error = "System.FS." + std::string(ToMember(member)) + " expects (i32, i32[], i32)";
        return false;
      }
      return true;
    }
    case SystemFSMember::Flush:
    case SystemFSMember::Seek:
    case SystemFSMember::Tell:
    case SystemFSMember::Stat:
    case SystemFSMember::Exists:
    case SystemFSMember::IsFile:
    case SystemFSMember::IsDir:
    case SystemFSMember::ListDir:
    case SystemFSMember::NextDirEntry:
    case SystemFSMember::CloseDir:
    case SystemFSMember::Mkdir:
    case SystemFSMember::MkdirAll:
    case SystemFSMember::Remove:
    case SystemFSMember::Copy:
    case SystemFSMember::Rename:
    case SystemFSMember::Cwd:
    case SystemFSMember::SetCwd:
    case SystemFSMember::ReadText:
    case SystemFSMember::WriteText:
    case SystemFSMember::ReadBytes:
    case SystemFSMember::WriteBytes:
      return true;
  }
  return true;
}

bool CheckReservedFileCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  const auto parsed = ParseMember(SystemModule::FS, member);
  if (!parsed || !std::holds_alternative<SystemFSMember>(*parsed)) return true;
  return CheckReservedFileCallArgTypes(std::get<SystemFSMember>(*parsed), args, error);
}

bool CheckReservedIoBufferCallArgTypes(SystemIOMember member,
                                       const std::vector<TypeRef>& args,
                                       std::string* error) {
  switch (member) {
    case SystemIOMember::BufferNew: {
      if (args.size() != 1) return true;
      const TypeRef& len = args[0];
      if (len.name != "i32" || !len.dims.empty()) {
        if (error) *error = "System.IO.buffer_new expects (i32)";
        return false;
      }
      return true;
    }
    case SystemIOMember::BufferLen: {
      if (args.size() != 1) return true;
      if (!IsI32BufferType(args[0])) {
        if (error) *error = "System.IO.buffer_len expects (i32[])";
        return false;
      }
      return true;
    }
    case SystemIOMember::BufferFill: {
      if (args.size() != 3) return true;
      const TypeRef& buf = args[0];
      const TypeRef& value = args[1];
      const TypeRef& count = args[2];
      if (!IsI32BufferType(buf) || value.name != "i32" || !value.dims.empty() ||
          count.name != "i32" || !count.dims.empty()) {
        if (error) *error = "System.IO.buffer_fill expects (i32[], i32, i32)";
        return false;
      }
      return true;
    }
    case SystemIOMember::BufferCopy: {
      if (args.size() != 3) return true;
      const TypeRef& dst = args[0];
      const TypeRef& src = args[1];
      const TypeRef& count = args[2];
      if (!IsI32BufferType(dst) || !IsI32BufferType(src) || count.name != "i32" || !count.dims.empty()) {
        if (error) *error = "System.IO.buffer_copy expects (i32[], i32[], i32)";
        return false;
      }
      return true;
    }
    case SystemIOMember::Stdin:
    case SystemIOMember::Stdout:
    case SystemIOMember::Stderr:
    case SystemIOMember::Write:
    case SystemIOMember::WriteText:
    case SystemIOMember::Flush:
      return true;
  }
  return true;
}

bool CheckReservedIoBufferCallArgTypes(const std::string& member,
                                       const std::vector<TypeRef>& args,
                                       std::string* error) {
  const auto parsed = ParseMember(SystemModule::IO, member);
  if (!parsed || !std::holds_alternative<SystemIOMember>(*parsed)) return true;
  return CheckReservedIoBufferCallArgTypes(std::get<SystemIOMember>(*parsed), args, error);
}

bool CheckReservedTimeCallArgTypes(StandardTimeMember member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  switch (member) {
    case StandardTimeMember::MonoNs:
    case StandardTimeMember::NowNs:
    case StandardTimeMember::MonoSnake:
    case StandardTimeMember::WallSnake:
      if (!args.empty()) {
        if (error) *error = "Time." + std::string(ToMember(member)) + " expects no arguments";
        return false;
      }
      return true;
    case StandardTimeMember::FormatWallNs: {
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
    case StandardTimeMember::SleepMs:
      return true;
  }
  return true;
}

bool CheckReservedTimeCallArgTypes(const std::string& member,
                                   const std::vector<TypeRef>& args,
                                   std::string* error) {
  const auto parsed = ParseMember(StandardModule::Time, member);
  if (!parsed || !std::holds_alternative<StandardTimeMember>(*parsed)) return true;
  return CheckReservedTimeCallArgTypes(std::get<StandardTimeMember>(*parsed), args, error);
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
