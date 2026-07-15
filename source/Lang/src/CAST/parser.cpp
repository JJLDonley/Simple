#include "CAST/parser.h"

#include "Lexer/lexer.h"

#include <limits>
#include <stdexcept>

namespace {

constexpr const char* kReturnFirstProcedureDiagnostic =
    "return-first procedure declarations are not supported; expected '(params) -> return_type'";

bool ParseIntegerLiteral(const std::string& text, uint64_t* out) {
  if (!out) return false;
  try {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
      *out = static_cast<uint64_t>(std::stoull(text.substr(2), nullptr, 16));
      return true;
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
      uint64_t value = 0;
      for (size_t i = 2; i < text.size(); ++i) {
        char c = text[i];
        if (c != '0' && c != '1') return false;
        if (value > (std::numeric_limits<uint64_t>::max() >> 1)) return false;
        value = (value << 1) | static_cast<uint64_t>(c - '0');
      }
      *out = value;
      return true;
    }
    *out = static_cast<uint64_t>(std::stoull(text, nullptr, 10));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace

namespace Simple::Lang::CAST {

namespace {

bool IsKeywordToken(TokenKind kind) {
  switch (kind) {
    case TokenKind::KwWhile:
    case TokenKind::KwFor:
    case TokenKind::KwBreak:
    case TokenKind::KwSkip:
    case TokenKind::KwReturn:
    case TokenKind::KwIf:
    case TokenKind::KwElse:
    case TokenKind::KwDefault:
    case TokenKind::KwSwitch:
    case TokenKind::KwFn:
    case TokenKind::KwAsync:
    case TokenKind::KwAwait:
    case TokenKind::KwSelf:
    case TokenKind::KwClass:
    case TokenKind::KwStruct:
    case TokenKind::KwEnum:
    case TokenKind::KwModule:
    case TokenKind::KwNamespace:
    case TokenKind::KwImport:
    case TokenKind::KwUsing:
    case TokenKind::KwExtern:
    case TokenKind::KwAs:
    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
      return true;
    default:
      return false;
  }
}

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
  constexpr size_t kExpressionAttemptsPerToken = 64;
  constexpr size_t kMinimumExpressionBudget = 1024;
  if (tokens_.size() >
      (std::numeric_limits<size_t>::max() - kMinimumExpressionBudget) /
          kExpressionAttemptsPerToken) {
    expression_budget_ = std::numeric_limits<size_t>::max();
  } else {
    expression_budget_ = tokens_.size() * kExpressionAttemptsPerToken +
                         kMinimumExpressionBudget;
  }
}

std::string Parser::ErrorWithLocation() const {
  if (error_.empty()) return error_;
  if (tokens_.empty()) return error_;
  uint32_t line = 1;
  uint32_t col = 1;
  if (!IsAtEnd()) {
    line = Peek().line;
    col = Peek().column;
  } else if (index_ > 0) {
    line = tokens_[index_ - 1].line;
    col = tokens_[index_ - 1].column;
  }
  return std::to_string(line) + ":" + std::to_string(col) + ": " + error_;
}

const Token& Parser::Peek(size_t offset) const {
  if (index_ + offset >= tokens_.size()) return tokens_.back();
  return tokens_[index_ + offset];
}

const Token& Parser::Advance() {
  if (!IsAtEnd()) {
    ++index_;
  }
  return tokens_[index_ - 1];
}

bool Parser::Match(TokenKind kind) {
  if (Peek().kind == kind) {
    Advance();
    return true;
  }
  return false;
}

bool Parser::IsAtEnd() const {
  return Peek().kind == TokenKind::End;
}

bool Parser::ParseType(TypeRef* out) {
  if (!out) return false;
  if (!ParseTypeInner(out)) return false;
  if (!IsAtEnd()) {
    error_ = "unexpected token after type: " + Peek().text;
    return false;
  }
  return true;
}

bool Parser::ParseProgram(Program* out) {
  if (!out) return false;
  out->decls.clear();
  out->top_level_stmts.clear();
  while (!IsAtEnd()) {
    const size_t save_index = index_;
    Decl decl;
    if (ParseDecl(&decl)) {
      out->decls.push_back(std::move(decl));
      continue;
    }
    const size_t decl_fail_index = index_;
    const std::string decl_error = error_;
    const bool can_retry_as_stmt =
        (decl_fail_index == save_index) ||
        (decl_fail_index == save_index + 1 &&
         decl_error == "expected ':' or '::' after identifier");
    if (!can_retry_as_stmt) {
      return false;
    }
    index_ = save_index;
    Stmt stmt;
    if (!ParseStmt(&stmt)) return false;
    out->top_level_stmts.push_back(std::move(stmt));
  }
  return !had_error_;
}

bool Parser::ParseBlock(std::vector<Stmt>* out) {
  if (!out) return false;
  out->clear();
  if (!ParseBlockStmts(out)) return false;
  if (!IsAtEnd()) {
    error_ = "unexpected token after block: " + Peek().text;
    return false;
  }
  return !had_error_;
}

bool Parser::ParseTypeSuffixes(TypeRef* out) {
  if (!out) return false;
  while (pending_type_argument_closes_ == 0) {
    if (Peek().line > LastTokenLine()) break;
    if (Peek().kind == TokenKind::LBracket || IsTypeDimensionBrace()) {
      if (!ParseTypeDims(out)) return false;
      continue;
    }
    if (Match(TokenKind::Star)) {
      ++out->pointer_depth;
      continue;
    }
    if (Match(TokenKind::Question)) {
      TypeRef inner = std::move(*out);
      TypeRef optional;
      optional.name = kOptionalTypeInternalName;
      optional.is_optional_syntax = true;
      optional.line = inner.line;
      optional.column = inner.column;
      optional.type_args.push_back(std::move(inner));
      *out = std::move(optional);
      continue;
    }
    break;
  }
  return true;
}

bool Parser::ParseTypeInner(TypeRef* out) {
  if (!out) return false;

  if (Match(TokenKind::KwFn)) {
    TypeRef proc;
    proc.is_proc = true;
    proc.proc_return_mutability = Mutability::Mutable;

    if (Match(TokenKind::Lt)) {
      if (!ParseTypeArgs(&proc.type_args)) return false;
    }
    if (!Match(TokenKind::LParen)) {
      error_ = "expected '(' after fn";
      return false;
    }
    if (!Match(TokenKind::RParen)) {
      for (;;) {
        if (Peek().kind == TokenKind::Identifier &&
            (Peek(1).kind == TokenKind::Colon ||
             Peek(1).kind == TokenKind::DoubleColon)) {
          Advance();
          Advance();
        }
        TypeRef param;
        if (!ParseTypeInner(&param)) return false;
        proc.proc_params.push_back(std::move(param));
        if (Match(TokenKind::Comma)) continue;
        if (Match(TokenKind::RParen)) break;
        error_ = "expected ',' or ')' in fn type parameter list";
        return false;
      }
    }
    if (!Match(TokenKind::Arrow)) {
      error_ = "expected '->' after fn parameter list";
      return false;
    }
    proc.proc_return = std::make_unique<TypeRef>();
    if (!ParseTypeInner(proc.proc_return.get())) return false;
    *out = std::move(proc);
    return true;
  }

  if (Match(TokenKind::LParen)) {
    TypeRef grouped;
    if (!ParseTypeInner(&grouped)) return false;
    if (!Match(TokenKind::RParen)) {
      error_ = "expected ')' after grouped type";
      return false;
    }
    if (!ParseTypeSuffixes(&grouped)) return false;
    *out = std::move(grouped);
    return true;
  }

  const Token& tok = Peek();
  if (tok.kind != TokenKind::Identifier) {
    error_ = "expected type name";
    return false;
  }
  out->type_args.clear();
  out->dims.clear();
  out->is_optional_syntax = false;
  out->is_proc = false;
  out->proc_params.clear();
  out->proc_return.reset();
  out->proc_return_mutability = Mutability::Mutable;
  out->name = tok.text;
  out->line = tok.line;
  out->column = tok.column;
  out->pointer_depth = 0;
  Advance();

  if (Match(TokenKind::Lt)) {
    if (!ParseTypeArgs(&out->type_args)) return false;
  }

  return ParseTypeSuffixes(out);
}

bool Parser::ParseDecl(Decl* out) {
  if (Match(TokenKind::KwModule)) {
    std::string module_name;
    const Token& name_tok = Peek();
    if (name_tok.kind != TokenKind::Identifier) {
      error_ = "expected module name after 'module'";
      return false;
    }
    module_name = name_tok.text;
    Advance();
    while (Match(TokenKind::Dot)) {
      const Token& seg_tok = Peek();
      if (seg_tok.kind != TokenKind::Identifier) {
        error_ = "expected identifier after '.' in module name";
        return false;
      }
      module_name += ".";
      module_name += seg_tok.text;
      Advance();
    }
    if (out) {
      out->kind = DeclKind::ModuleHeader;
      out->module_header.name = std::move(module_name);
    }
    if (Match(TokenKind::Semicolon) || IsImplicitStmtTerminator()) return true;
    error_ = "expected end of module declaration";
    return false;
  }
  if (Match(TokenKind::KwImport)) {
    std::string import_path;
    const Token& path_tok = Peek();
    if (path_tok.kind == TokenKind::String) {
      import_path = path_tok.text;
      Advance();
    } else if (path_tok.kind == TokenKind::Identifier) {
      import_path = path_tok.text;
      Advance();
      while (Match(TokenKind::Dot)) {
        const Token& seg_tok = Peek();
        if (seg_tok.kind != TokenKind::Identifier) {
          error_ = "expected identifier after '.' in import path";
          return false;
        }
        import_path += ".";
        import_path += seg_tok.text;
        Advance();
      }
    } else {
      error_ = "expected string literal or module path after 'import'";
      return false;
    }
    if (out) {
      out->kind = DeclKind::Import;
      out->import_decl.path = import_path;
      out->import_decl.has_alias = false;
      out->import_decl.alias.clear();
      out->import_decl.is_using = false;
    }
    if (Match(TokenKind::KwAs)) {
      const Token& alias_tok = Peek();
      if (alias_tok.kind != TokenKind::Identifier) {
        error_ = "expected alias identifier after 'as'";
        return false;
      }
      Advance();
      if (out) {
        out->import_decl.has_alias = true;
        out->import_decl.alias = alias_tok.text;
      }
    }
    if (Match(TokenKind::Semicolon) || IsImplicitStmtTerminator()) {
      return true;
    }
    error_ = "expected end of import declaration";
    return false;
  }
  if (Match(TokenKind::KwUsing)) {
    std::string using_name;
    const Token& name_tok = Peek();
    if (name_tok.kind != TokenKind::Identifier) {
      error_ = "expected imported module name after 'using'";
      return false;
    }
    using_name = name_tok.text;
    Advance();
    while (Match(TokenKind::Dot)) {
      const Token& seg_tok = Peek();
      if (seg_tok.kind != TokenKind::Identifier) {
        error_ = "expected identifier after '.' in using name";
        return false;
      }
      using_name += ".";
      using_name += seg_tok.text;
      Advance();
    }
    if (out) {
      out->kind = DeclKind::Import;
      out->import_decl.path = using_name;
      out->import_decl.alias.clear();
      out->import_decl.has_alias = false;
      out->import_decl.is_using = true;
    }
    if (Match(TokenKind::Semicolon) || IsImplicitStmtTerminator()) return true;
    error_ = "expected end of using declaration";
    return false;
  }
  if (Match(TokenKind::KwExtern)) {
    const Token& name_tok = Peek();
    if (name_tok.kind != TokenKind::Identifier) {
      error_ = "expected extern name";
      return false;
    }
    Advance();
    std::string module_name;
    std::string extern_name = name_tok.text;
    bool has_module = false;
    if (Match(TokenKind::Dot)) {
      const Token& member_tok = Peek();
      if (member_tok.kind != TokenKind::Identifier) {
        error_ = "expected extern name after '.'";
        return false;
      }
      Advance();
      module_name = extern_name;
      extern_name = member_tok.text;
      has_module = true;
    }
    Mutability mut = Mutability::Immutable;
    if (Match(TokenKind::Colon)) {
      mut = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      mut = Mutability::Immutable;
    } else {
      error_ = "expected ':' or '::' after extern name";
      return false;
    }
    TypeRef return_type;
    std::vector<ParamDecl> params;
    if (!ParseCallableSignature(&return_type, &params)) return false;
    if (out) {
      out->kind = DeclKind::Extern;
      out->ext.name = extern_name;
      out->ext.module = module_name;
      out->ext.has_module = has_module;
      out->ext.return_mutability = mut;
      out->ext.return_type = std::move(return_type);
      out->ext.params = std::move(params);
    }
    if (Match(TokenKind::Semicolon) || IsImplicitStmtTerminator()) {
      return true;
    }
    error_ = "expected end of extern declaration";
    return false;
  }

  const Token& name_tok = Peek();
  if (name_tok.kind == TokenKind::End) return false;
  if (name_tok.kind != TokenKind::Identifier) {
    error_ = "expected identifier at top level";
    return false;
  }
  Advance();

  std::vector<std::string> generics;
  if (Match(TokenKind::Lt)) {
    if (!ParseGenerics(&generics)) return false;
  }

  if (Match(TokenKind::DoubleColon)) {
    if (Match(TokenKind::KwClass)) {
      return ParseAggregateDecl(name_tok, std::move(generics), false, out);
    }
    if (Match(TokenKind::KwStruct)) {
      return ParseAggregateDecl(name_tok, std::move(generics), true, out);
    }
    if (Match(TokenKind::KwNamespace)) {
      return ParseModuleDecl(name_tok, out);
    }
    if (Match(TokenKind::KwEnum)) {
      if (!generics.empty()) {
        error_ = "enum declarations do not support generics";
        return false;
      }
      return ParseEnumDecl(name_tok, out);
    }
    Mutability mut = Mutability::Immutable;
    const bool is_async = Match(TokenKind::KwAsync);
    if (Peek().kind == TokenKind::LParen) {
      if (out) {
        out->kind = DeclKind::Function;
        out->func.name = name_tok.text;
        out->func.generics = std::move(generics);
        out->func.is_async = is_async;
        out->func.return_mutability = mut;
      }
      if (!ParseCallableSignature(
              &out->func.return_type, &out->func.params)) return false;
      if (!ParseBlockStmts(&out->func.body)) return false;
      return true;
    }
    if (is_async) {
      error_ = "async is valid only on function declarations";
      return false;
    }
    TypeRef value_type;
    if (!ParseTypeInner(&value_type)) return false;
    if (Peek().kind == TokenKind::LParen) {
      error_ = kReturnFirstProcedureDiagnostic;
      return false;
    }
    if (out) {
      out->kind = DeclKind::Variable;
      out->var.name = name_tok.text;
      out->var.mutability = mut;
      out->var.type = std::move(value_type);
    }
    if (Match(TokenKind::Assign)) {
      Expr init;
      if (!ParseExpr(&init)) return false;
      if (!ConsumeStmtTerminator("variable declaration")) return false;
      if (out) {
        out->var.has_init_expr = true;
        out->var.init_expr = std::move(init);
      }
    } else if (Match(TokenKind::Semicolon) || IsImplicitStmtTerminator()) {
      // zero-initialized
    } else {
      error_ = "expected '=' or ';' in variable declaration";
      return false;
    }
    return true;
  }

  Mutability mut = Mutability::Mutable;
  if (!Match(TokenKind::Colon)) {
    error_ = "expected ':' or '::' after identifier";
    return false;
  }

  const bool is_async = Match(TokenKind::KwAsync);
  if (Peek().kind == TokenKind::LParen) {
    if (out) {
      out->kind = DeclKind::Function;
      out->func.name = name_tok.text;
      out->func.generics = std::move(generics);
      out->func.is_async = is_async;
      out->func.return_mutability = mut;
    }
    if (!ParseCallableSignature(
            &out->func.return_type, &out->func.params)) return false;
    if (!ParseBlockStmts(&out->func.body)) return false;
    return true;
  }

  if (is_async) {
    error_ = "async is valid only on function declarations";
    return false;
  }
  TypeRef value_type;
  if (!ParseTypeInner(&value_type)) return false;
  if (Peek().kind == TokenKind::LParen) {
    error_ = kReturnFirstProcedureDiagnostic;
    return false;
  }
  if (out) {
    out->kind = DeclKind::Variable;
    out->var.name = name_tok.text;
    out->var.mutability = mut;
    out->var.type = std::move(value_type);
  }
  if (Match(TokenKind::Assign)) {
    Expr init;
    if (!ParseExpr(&init)) return false;
    if (!ConsumeStmtTerminator("variable declaration")) return false;
    if (out) {
      out->var.has_init_expr = true;
      out->var.init_expr = std::move(init);
    }
  } else if (Match(TokenKind::Semicolon) || IsImplicitStmtTerminator()) {
    // zero-initialized
  } else {
    error_ = "expected '=' or ';' in variable declaration";
    return false;
  }
  return true;
}

bool Parser::ParseAggregateDecl(const Token& name_tok,
                               std::vector<std::string> generics,
                               bool is_struct,
                               Decl* out) {
  if (out) {
    out->kind = DeclKind::Aggregate;
    out->aggregate.name = name_tok.text;
    out->aggregate.generics = std::move(generics);
    out->aggregate.is_struct = is_struct;
  }
  if (!ParseAggregateBody(&out->aggregate)) return false;
  return true;
}

bool Parser::ParseModuleDecl(const Token& name_tok, Decl* out) {
  if (out) {
    out->kind = DeclKind::Module;
    out->module.name = name_tok.text;
  }
  if (!ParseModuleBody(&out->module)) return false;
  return true;
}

bool Parser::ParseEnumDecl(const Token& name_tok, Decl* out) {
  TypeRef underlying_type;
  underlying_type.name = "i32";
  underlying_type.line = name_tok.line;
  underlying_type.column = name_tok.column;
  if (Match(TokenKind::Colon)) {
    if (!ParseTypeInner(&underlying_type)) return false;
  }
  if (out) {
    out->kind = DeclKind::Enum;
    out->enm.name = name_tok.text;
    out->enm.underlying_type = std::move(underlying_type);
  }
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start enum body";
    return false;
  }
  if (Match(TokenKind::RBrace)) return true;
  while (!IsAtEnd()) {
    const Token& member_tok = Peek();
    if (member_tok.kind != TokenKind::Identifier) {
      error_ = "expected enum member name";
      return false;
    }
    Advance();
    EnumMember member;
    member.name = member_tok.text;
    if (Match(TokenKind::Assign)) {
      const bool negative = Match(TokenKind::Minus);
      const Token& value_tok = Peek();
      if (value_tok.kind != TokenKind::Integer) {
        error_ = "expected integer literal for enum value";
        return false;
      }
      member.has_value = true;
      member.value_text = negative ? "-" + value_tok.text : value_tok.text;
      Advance();
    }
    if (out) out->enm.members.push_back(std::move(member));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RBrace)) return true;
    error_ = "expected ',' or '}' after enum member";
    return false;
  }
  error_ = "unterminated enum body";
  return false;
}

bool Parser::ParseAggregateBody(AggregateDecl* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start aggregate body";
    return false;
  }
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) return true;
    if (!ParseAggregateMember(out)) return false;
  }
  error_ = "unterminated aggregate body";
  return false;
}

bool Parser::ParseModuleBody(ModuleDecl* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start module body";
    return false;
  }
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) return true;
    if (!ParseModuleMember(out)) return false;
  }
  error_ = "unterminated module body";
  return false;
}

bool Parser::ParseAggregateMember(AggregateDecl* out) {
  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    if (name_tok.kind == TokenKind::Comma) {
      error_ = "unexpected ',' in aggregate body; use newline or ';' between members";
      return false;
    }
    error_ = "expected aggregate member name";
    return false;
  }
  Advance();

  std::vector<std::string> generics;
  if (Match(TokenKind::Lt)) {
    if (!ParseGenerics(&generics)) return false;
  }

  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after member name";
    return false;
  }

  const bool is_async = Match(TokenKind::KwAsync);
  if (Peek().kind == TokenKind::LParen) {
    if (out && out->is_struct) {
      error_ = "struct declarations cannot contain methods";
      return false;
    }
    FuncDecl fn;
    fn.name = name_tok.text;
    fn.generics = std::move(generics);
    fn.is_async = is_async;
    fn.return_mutability = mut;
    if (!ParseCallableSignature(&fn.return_type, &fn.params)) return false;
    if (!ParseBlockStmts(&fn.body)) return false;
    if (out) out->methods.push_back(std::move(fn));
    return true;
  }

  if (is_async) {
    error_ = "async is valid only on function declarations";
    return false;
  }
  if (!generics.empty()) {
    error_ = "aggregate fields do not support generic parameters";
    return false;
  }

  TypeRef field_type;
  if (!ParseTypeInner(&field_type)) return false;
  if (Peek().kind == TokenKind::LParen) {
    error_ = kReturnFirstProcedureDiagnostic;
    return false;
  }
  VarDecl field;
  field.name = name_tok.text;
  field.mutability = mut;
  field.type = std::move(field_type);
  if (Match(TokenKind::Assign)) {
    Expr init;
    if (!ParseExpr(&init)) return false;
    if (!ConsumeStmtTerminator("aggregate field declaration")) return false;
    field.has_init_expr = true;
    field.init_expr = std::move(init);
  } else if (Match(TokenKind::Semicolon)) {
    // optional
  } else if (IsImplicitStmtTerminator()) {
    // optional
  } else {
    if (Peek().kind == TokenKind::Comma) {
      error_ = "unexpected ',' in aggregate body; use newline or ';' between members";
    } else {
      error_ = "expected '=' or ';' in aggregate field declaration";
    }
    return false;
  }
  if (out) out->fields.push_back(std::move(field));
  return true;
}

bool Parser::ParseModuleMember(ModuleDecl* out) {
  if (Match(TokenKind::KwExtern)) {
    const Token& name_tok = Peek();
    if (name_tok.kind != TokenKind::Identifier) {
      error_ = "expected extern name";
      return false;
    }
    Advance();
    std::string module_name;
    std::string extern_name = name_tok.text;
    bool has_module = false;
    if (Match(TokenKind::Dot)) {
      const Token& member_tok = Peek();
      if (member_tok.kind != TokenKind::Identifier) {
        error_ = "expected extern name after '.'";
        return false;
      }
      Advance();
      module_name = extern_name;
      extern_name = member_tok.text;
      has_module = true;
    }
    Mutability mut = Mutability::Immutable;
    if (Match(TokenKind::Colon)) {
      mut = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      mut = Mutability::Immutable;
    } else {
      error_ = "expected ':' or '::' after extern name";
      return false;
    }
    TypeRef return_type;
    std::vector<ParamDecl> params;
    if (!ParseCallableSignature(&return_type, &params)) return false;
    if (out) {
      ExternDecl ext;
      ext.name = extern_name;
      ext.module = has_module ? module_name : out->name;
      ext.has_module = true;
      ext.return_mutability = mut;
      ext.return_type = std::move(return_type);
      ext.params = std::move(params);
      out->externs.push_back(std::move(ext));
    }
    if (Match(TokenKind::Semicolon) || IsImplicitStmtTerminator()) return true;
    error_ = "expected end of extern declaration";
    return false;
  }

  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    error_ = "expected module member name";
    return false;
  }
  Advance();

  std::vector<std::string> generics;
  if (Match(TokenKind::Lt)) {
    if (!ParseGenerics(&generics)) return false;
  }

  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after member name";
    return false;
  }

  const bool is_async = Match(TokenKind::KwAsync);
  if (Peek().kind == TokenKind::LParen) {
    FuncDecl fn;
    fn.name = name_tok.text;
    fn.generics = std::move(generics);
    fn.is_async = is_async;
    fn.return_mutability = mut;
    if (!ParseCallableSignature(&fn.return_type, &fn.params)) return false;
    if (!ParseBlockStmts(&fn.body)) return false;
    if (out) out->functions.push_back(std::move(fn));
    return true;
  }

  if (is_async) {
    error_ = "async is valid only on function declarations";
    return false;
  }
  if (!generics.empty()) {
    error_ = "module variables do not support generic parameters";
    return false;
  }

  TypeRef value_type;
  if (!ParseTypeInner(&value_type)) return false;
  if (Peek().kind == TokenKind::LParen) {
    error_ = kReturnFirstProcedureDiagnostic;
    return false;
  }
  VarDecl var;
  var.name = name_tok.text;
  var.mutability = mut;
  var.type = std::move(value_type);
  if (Match(TokenKind::Assign)) {
    Expr init;
    if (!ParseExpr(&init)) return false;
    if (!ConsumeStmtTerminator("module variable declaration")) return false;
    var.has_init_expr = true;
    var.init_expr = std::move(init);
  } else if (Match(TokenKind::Semicolon)) {
    // zero-initialized
  } else if (IsImplicitStmtTerminator()) {
    // zero-initialized
  } else {
    error_ = "expected '=' or ';' in module variable declaration";
    return false;
  }
  if (out) out->variables.push_back(std::move(var));
  return true;
}

bool Parser::ParseGenerics(std::vector<std::string>* out) {
  if (!out) return false;
  if (Match(TokenKind::Gt)) {
    error_ = "empty generic parameter list";
    return false;
  }
  for (;;) {
    const Token& tok = Peek();
    if (tok.kind != TokenKind::Identifier) {
      error_ = "expected generic parameter name";
      return false;
    }
    out->push_back(tok.text);
    Advance();
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::Gt)) break;
    error_ = "expected ',' or '>' in generic parameter list";
    return false;
  }
  return true;
}

bool Parser::ParseParamList(std::vector<ParamDecl>* out) {
  if (!out) return false;
  if (Match(TokenKind::RParen)) return true;
  for (;;) {
    ParamDecl param;
    if (!ParseParam(&param)) return false;
    out->push_back(std::move(param));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RParen)) break;
    error_ = "expected ',' or ')' after parameter";
    return false;
  }
  return true;
}

bool Parser::ParseCallableSignature(TypeRef* return_type,
                                    std::vector<ParamDecl>* params) {
  if (!return_type || !params) return false;
  if (!Match(TokenKind::LParen)) {
    error_ = "expected parameter-first function signature '(params) -> return_type'";
    return false;
  }
  if (!ParseParamList(params)) return false;
  if (!Match(TokenKind::Arrow)) {
    error_ = "expected '->' after function parameter list";
    return false;
  }
  return ParseTypeInner(return_type);
}

bool Parser::ParseParam(ParamDecl* out) {
  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    if (IsKeywordToken(name_tok.kind)) {
      error_ = "expected parameter name (keyword '" + name_tok.text +
               "' cannot be used as identifier)";
      return false;
    }
    error_ = "expected parameter name";
    return false;
  }
  Advance();
  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after parameter name";
    return false;
  }
  TypeRef type;
  if (!ParseTypeInner(&type)) return false;
  out->name = name_tok.text;
  out->mutability = mut;
  out->type = std::move(type);
  return true;
}

bool Parser::ParseBlockTokens(std::vector<Token>* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start block";
    return false;
  }
  int depth = 1;
  while (!IsAtEnd()) {
    Token tok = Advance();
    if (tok.kind == TokenKind::LBrace) depth++;
    if (tok.kind == TokenKind::RBrace) {
      depth--;
      if (depth == 0) return true;
    }
    if (out) out->push_back(tok);
  }
  error_ = "unterminated block";
  return false;
}

bool Parser::ParseBlockStmts(std::vector<Stmt>* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start block";
    return false;
  }
  std::string first_error;
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) {
      if (!first_error.empty()) error_ = std::move(first_error);
      return true;
    }
    Stmt stmt;
    if (!ParseStmt(&stmt)) {
      if (first_error.empty()) first_error = error_;
      had_error_ = true;
      if (!RecoverStatementInBlock()) {
        if (!first_error.empty()) error_ = std::move(first_error);
        return false;
      }
      continue;
    }
    if (out) out->push_back(std::move(stmt));
  }
  if (first_error.empty()) error_ = "unterminated block";
  else error_ = std::move(first_error);
  return false;
}

bool Parser::RecoverStatementInBlock() {
  while (!IsAtEnd()) {
    if (Peek().kind == TokenKind::Semicolon) {
      Advance();
      return true;
    }
    if (Peek().kind == TokenKind::RBrace) {
      return true;
    }
    Advance();
  }
  if (error_.empty()) error_ = "unterminated block";
  return false;
}

bool Parser::IsTypeDimensionBrace() const {
  if (Peek().kind != TokenKind::LBrace) return false;
  if (Peek(1).kind == TokenKind::Integer) return true;
  return index_ > 0 && Peek().line == tokens_[index_ - 1].line &&
         Peek().column == tokens_[index_ - 1].column + tokens_[index_ - 1].text.size();
}

uint32_t Parser::LastTokenLine() const {
  if (index_ == 0) return 1;
  return tokens_[index_ - 1].line;
}

bool Parser::IsImplicitStmtTerminator() const {
  if (IsAtEnd()) return true;
  if (Peek().kind == TokenKind::RBrace) return true;
  return Peek().line > LastTokenLine();
}

bool Parser::ConsumeStmtTerminator(const char* ctx) {
  if (Match(TokenKind::Semicolon)) return true;
  if (IsImplicitStmtTerminator()) return true;
  error_ = std::string("expected ';' after ") + ctx;
  return false;
}

bool Parser::ParseInitTokens(std::vector<Token>* out) {
  while (!IsAtEnd()) {
    if (Peek().kind == TokenKind::Semicolon) {
      Advance();
      return true;
    }
    if (Peek().line > LastTokenLine()) {
      return true;
    }
    if (out) out->push_back(Advance());
  }
  error_ = "unterminated variable declaration";
  return false;
}

bool Parser::ParseStmt(Stmt* out) {
  if (Match(TokenKind::KwReturn)) {
    if (ConsumeStmtTerminator("return")) {
      if (out) {
        out->kind = StmtKind::Return;
        out->has_return_expr = false;
      }
      return true;
    }
    Expr expr;
    if (!ParseExpr(&expr)) return false;
    if (!ConsumeStmtTerminator("return")) return false;
    if (out) {
      out->kind = StmtKind::Return;
      out->has_return_expr = true;
      out->expr = std::move(expr);
    }
    return true;
  }

  if (Peek().kind == TokenKind::PipeGt) {
    return ParseIfChain(out);
  }

  if (Peek().kind == TokenKind::KwIf) {
    return ParseIfStmt(out);
  }

  if (Peek().kind == TokenKind::KwWhile) {
    return ParseWhile(out);
  }

  if (Peek().kind == TokenKind::KwFor) {
    return ParseFor(out);
  }

  if (Match(TokenKind::KwBreak)) {
    if (!ConsumeStmtTerminator("break")) return false;
    if (out) {
      out->kind = StmtKind::Break;
    }
    return true;
  }

  if (Match(TokenKind::KwSkip)) {
    if (!ConsumeStmtTerminator("skip")) return false;
    if (out) {
      out->kind = StmtKind::Skip;
    }
    return true;
  }

  if (Peek().kind == TokenKind::Identifier &&
      (Peek(1).kind == TokenKind::Colon || Peek(1).kind == TokenKind::DoubleColon)) {
    Token name_tok = Advance();
    Mutability mut = Mutability::Mutable;
    if (Match(TokenKind::Colon)) {
      mut = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      mut = Mutability::Immutable;
    }
    TypeRef type;
    if (!ParseTypeInner(&type)) return false;
    bool has_init = false;
    Expr init;
    if (Match(TokenKind::Assign)) {
      has_init = true;
      if (!ParseExpr(&init)) return false;
      if (!ConsumeStmtTerminator("variable declaration")) return false;
    } else if (Match(TokenKind::Semicolon)) {
      has_init = false;
    } else {
      if (!IsImplicitStmtTerminator()) {
        error_ = "expected '=' or ';' in variable declaration";
        return false;
      }
      has_init = false;
    }
    if (out) {
      out->kind = StmtKind::VarDecl;
      out->var_decl.name = name_tok.text;
      out->var_decl.mutability = mut;
      out->var_decl.type = std::move(type);
      out->var_decl.has_init_expr = has_init;
      if (has_init) out->var_decl.init_expr = std::move(init);
    }
    return true;
  }

  size_t save = index_;
  Expr target;
  if (ParseUnaryExpr(&target)) {
    const Token& op = Peek();
    bool is_assign = false;
    switch (op.kind) {
      case TokenKind::Assign:
      case TokenKind::PlusEq:
      case TokenKind::MinusEq:
      case TokenKind::StarEq:
      case TokenKind::SlashEq:
      case TokenKind::PercentEq:
      case TokenKind::AmpEq:
      case TokenKind::PipeEq:
      case TokenKind::CaretEq:
      case TokenKind::ShlEq:
      case TokenKind::ShrEq:
        is_assign = true;
        break;
      default:
        break;
    }
    if (is_assign) {
      Advance();
      Expr value;
      if (!ParseExpr(&value)) return false;
      if (!ConsumeStmtTerminator("assignment")) return false;
      if (out) {
        out->kind = StmtKind::Assign;
        out->target = std::move(target);
        out->assign_op = op.text;
        out->expr = std::move(value);
      }
      return true;
    }
  } else if (index_ != save) {
    return false;
  }
  index_ = save;

  Expr expr;
  if (!ParseExpr(&expr)) return false;
  if (!ConsumeStmtTerminator("expression")) return false;
  if (out) {
    out->kind = StmtKind::Expr;
    out->expr = std::move(expr);
  }
  return true;
}

bool Parser::ParseFor(Stmt* out) {
  if (!Match(TokenKind::KwFor)) {
    error_ = "expected 'for'";
    return false;
  }
  if (!Match(TokenKind::LParen)) {
    error_ = "expected '(' after 'for'";
    return false;
  }

  auto MakeIdent = [](const Token& tok) -> Expr {
    Expr expr;
    expr.kind = ExprKind::Identifier;
    expr.text = tok.text;
    expr.line = tok.line;
    expr.column = tok.column;
    return expr;
  };
  auto MakeBinary = [](const std::string& op, Expr lhs, Expr rhs) -> Expr {
    Expr expr;
    expr.kind = ExprKind::Binary;
    expr.op = op;
    expr.children.push_back(std::move(lhs));
    expr.children.push_back(std::move(rhs));
    return expr;
  };
  auto MakeIntLiteral = [](int64_t value) -> Expr {
    Expr expr;
    expr.kind = ExprKind::Literal;
    expr.literal_kind = LiteralKind::Integer;
    expr.text = std::to_string(value);
    return expr;
  };

  Expr init_expr;
  VarDecl loop_var;
  bool has_loop_var_decl = false;

  if (Peek().kind == TokenKind::Identifier && Peek(1).kind == TokenKind::Semicolon) {
    Token name_tok = Advance();
    loop_var.name = name_tok.text;
    loop_var.mutability = Mutability::Mutable;
    loop_var.type.name = "i32";
    loop_var.has_init_expr = true;
    loop_var.init_expr = MakeIntLiteral(0);
    has_loop_var_decl = true;
    init_expr = MakeBinary("=", MakeIdent(name_tok), MakeIntLiteral(0));
    Advance(); // ';'
  } else if (Peek().kind == TokenKind::Identifier &&
             (Peek(1).kind == TokenKind::Colon || Peek(1).kind == TokenKind::DoubleColon)) {
    Token name_tok = Advance();
    Mutability mut = Mutability::Mutable;
    if (Match(TokenKind::Colon)) {
      mut = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      mut = Mutability::Immutable;
    }
    TypeRef type;
    if (!ParseTypeInner(&type)) return false;
    if (!Match(TokenKind::Assign)) {
      error_ = "expected '=' in for initializer";
      return false;
    }
    Expr rhs;
    if (!ParseExpr(&rhs)) return false;
    loop_var.name = name_tok.text;
    loop_var.mutability = mut;
    loop_var.type = std::move(type);
    loop_var.has_init_expr = true;
    loop_var.init_expr = rhs;
    has_loop_var_decl = true;
    init_expr = MakeBinary("=", MakeIdent(name_tok), std::move(rhs));
    if (!Match(TokenKind::Semicolon)) {
      error_ = "expected ';' after for initializer";
      return false;
    }
  } else {
    if (!ParseExpr(&init_expr)) return false;
    if (!Match(TokenKind::Semicolon)) {
      error_ = "expected ';' after for initializer";
      return false;
    }
  }

  Expr cond;
  if (!ParseExpr(&cond)) return false;
  if (!Match(TokenKind::Semicolon)) {
    error_ = "expected ';' after for condition";
    return false;
  }
  Expr step;
  if (!ParseAssignmentExpr(&step)) return false;
  if (!Match(TokenKind::RParen)) {
    error_ = "expected ')' after for step";
    return false;
  }
  std::vector<Stmt> body;
  if (!ParseBlockStmts(&body)) return false;
  if (out) {
    out->kind = StmtKind::ForLoop;
    out->has_loop_var_decl = has_loop_var_decl;
    if (has_loop_var_decl) {
      out->loop_var_decl = std::move(loop_var);
    }
    out->loop_iter = std::move(init_expr);
    out->loop_cond = std::move(cond);
    out->loop_step = std::move(step);
    out->loop_body = std::move(body);
  }
  return true;
}

bool Parser::ParseIfChain(Stmt* out) {
  if (!Match(TokenKind::PipeGt)) {
    error_ = "expected '|>' to start if chain";
    return false;
  }
  if (!Match(TokenKind::LParen)) {
    error_ = "expected '(' after '|>'";
    return false;
  }
  Expr first_cond;
  if (!ParseExpr(&first_cond)) return false;
  if (!Match(TokenKind::RParen)) {
    error_ = "expected ')' after chain condition";
    return false;
  }
  std::vector<Stmt> then_body;
  if (!ParseBlockStmts(&then_body)) return false;
  if (out) {
    out->kind = StmtKind::IfChain;
    out->if_branches.push_back({std::move(first_cond), std::move(then_body)});
  }
  while (Match(TokenKind::PipeGt)) {
    if (Match(TokenKind::KwDefault)) {
      std::vector<Stmt> else_body;
      if (!ParseBlockStmts(&else_body)) return false;
      if (out) out->else_branch = std::move(else_body);
      break;
    }
    if (!Match(TokenKind::LParen)) {
      error_ = "expected '(' after '|>'";
      return false;
    }
    Expr cond;
    if (!ParseExpr(&cond)) return false;
    if (!Match(TokenKind::RParen)) {
      error_ = "expected ')' after chain condition";
      return false;
    }
    std::vector<Stmt> body;
    if (!ParseBlockStmts(&body)) return false;
    if (out) out->if_branches.push_back({std::move(cond), std::move(body)});
  }
  return true;
}

bool Parser::ParseIfStmt(Stmt* out) {
  if (!Match(TokenKind::KwIf)) {
    error_ = "expected 'if'";
    return false;
  }
  if (!Match(TokenKind::LParen)) {
    error_ = "expected '(' after 'if'";
    return false;
  }
  Expr cond;
  if (!ParseExpr(&cond)) return false;
  if (!Match(TokenKind::RParen)) {
    error_ = "expected ')' after if condition";
    return false;
  }
  std::vector<Stmt> then_body;
  if (!ParseBlockStmts(&then_body)) return false;
  std::vector<Stmt> else_body;
  if (Match(TokenKind::KwElse)) {
    if (Peek().kind == TokenKind::KwIf) {
      Stmt nested;
      if (!ParseIfStmt(&nested)) return false;
      else_body.push_back(std::move(nested));
    } else {
      if (!ParseBlockStmts(&else_body)) return false;
    }
  }
  if (out) {
    out->kind = StmtKind::IfStmt;
    out->if_cond = std::move(cond);
    out->if_then = std::move(then_body);
    out->if_else = std::move(else_body);
  }
  return true;
}

bool Parser::ParseWhile(Stmt* out) {
  if (!Match(TokenKind::KwWhile)) {
    error_ = "expected 'while'";
    return false;
  }
  if (!Match(TokenKind::LParen)) {
    error_ = "expected '(' after 'while'";
    return false;
  }
  Expr cond;
  if (!ParseExpr(&cond)) return false;
  if (!Match(TokenKind::RParen)) {
    error_ = "expected ')' after while condition";
    return false;
  }
  std::vector<Stmt> body;
  if (!ParseBlockStmts(&body)) return false;
  if (out) {
    out->kind = StmtKind::WhileLoop;
    out->loop_cond = std::move(cond);
    out->loop_body = std::move(body);
  }
  return true;
}

bool Parser::ParseExpr(Expr* out) {
  constexpr uint32_t kMaxExpressionDepth = 64;
  if (expression_budget_ == 0) {
    error_ = "expression parsing complexity limit exceeded";
    had_error_ = true;
    return false;
  }
  --expression_budget_;
  if (expression_depth_ >= kMaxExpressionDepth) {
    error_ = "expression nesting limit exceeded";
    had_error_ = true;
    return false;
  }
  struct DepthGuard {
    uint32_t* depth;
    explicit DepthGuard(uint32_t* value) : depth(value) { ++*depth; }
    ~DepthGuard() { --*depth; }
  } guard(&expression_depth_);
  return ParseAssignmentExpr(out);
}

bool Parser::ParseNonFormatExpr(Expr* out) {
  const bool previous = allow_format_expr_;
  allow_format_expr_ = false;
  const bool parsed = ParseExpr(out);
  allow_format_expr_ = previous;
  return parsed;
}

bool Parser::ParseAssignmentExpr(Expr* out) {
  size_t save = index_;
  Expr target;
  if (ParseUnaryExpr(&target)) {
    const Token& op = Peek();
    bool is_assign = false;
    switch (op.kind) {
      case TokenKind::Assign:
      case TokenKind::PlusEq:
      case TokenKind::MinusEq:
      case TokenKind::StarEq:
      case TokenKind::SlashEq:
      case TokenKind::PercentEq:
      case TokenKind::AmpEq:
      case TokenKind::PipeEq:
      case TokenKind::CaretEq:
      case TokenKind::ShlEq:
      case TokenKind::ShrEq:
        is_assign = true;
        break;
      default:
        break;
    }
    if (is_assign) {
      Advance();
      Expr value;
      if (!ParseAssignmentExpr(&value)) return false;
      Expr expr;
      expr.kind = ExprKind::Binary;
      expr.op = op.text;
      expr.children.push_back(std::move(target));
      expr.children.push_back(std::move(value));
      if (out) *out = std::move(expr);
      return true;
    }
  }
  index_ = save;
  return ParseBinaryExpr(0, out);
}

int Parser::GetBinaryPrecedence(const Token& tok) const {
  switch (tok.kind) {
    case TokenKind::OrOr:
      return 1;
    case TokenKind::AndAnd:
      return 2;
    case TokenKind::Pipe:
      return 3;
    case TokenKind::Caret:
      return 4;
    case TokenKind::Amp:
      return 5;
    case TokenKind::EqEq:
    case TokenKind::NotEq:
      return 6;
    case TokenKind::Lt:
    case TokenKind::Le:
    case TokenKind::Gt:
    case TokenKind::Ge:
      return 7;
    case TokenKind::Shl:
    case TokenKind::Shr:
      return 8;
    case TokenKind::Plus:
    case TokenKind::Minus:
      return 9;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
      return 10;
    default:
      return -1;
  }
}

bool Parser::ParseBinaryExpr(int min_prec, Expr* out) {
  Expr lhs;
  if (!ParseUnaryExpr(&lhs)) return false;

  while (true) {
    if (Peek().kind == TokenKind::Star && Peek().line > LastTokenLine()) {
      const size_t probe_index = index_;
      const std::string probe_error = error_;
      Expr pointer_target;
      const bool parsed_target = ParseUnaryExpr(&pointer_target);
      const TokenKind following = Peek().kind;
      index_ = probe_index;
      error_ = probe_error;
      const bool begins_assignment =
          parsed_target &&
          (following == TokenKind::Assign || following == TokenKind::PlusEq ||
           following == TokenKind::MinusEq || following == TokenKind::StarEq ||
           following == TokenKind::SlashEq || following == TokenKind::PercentEq ||
           following == TokenKind::AmpEq || following == TokenKind::PipeEq ||
           following == TokenKind::CaretEq || following == TokenKind::ShlEq ||
           following == TokenKind::ShrEq);
      if (begins_assignment) break;
    }
    const Token& op = Peek();
    int prec = GetBinaryPrecedence(op);
    if (prec < min_prec) break;
    Advance();
    Expr rhs;
    if (!ParseBinaryExpr(prec + 1, &rhs)) return false;
    Expr combined;
    combined.kind = ExprKind::Binary;
    combined.op = op.text;
    combined.children.push_back(std::move(lhs));
    combined.children.push_back(std::move(rhs));
    lhs = std::move(combined);
  }

  if (out) *out = std::move(lhs);
  return true;
}

bool Parser::ParseUnaryExpr(Expr* out) {
  const Token& tok = Peek();
  if (tok.kind == TokenKind::At) {
    Advance();
    TypeRef cast_type;
    if (!ParseTypeInner(&cast_type)) return false;
    if ((cast_type.is_proc && cast_type.pointer_depth == 0) ||
        !cast_type.type_args.empty() || !cast_type.dims.empty()) {
      error_ = "cast expects primitive or pointer type in @T(value)";
      return false;
    }
    if (!Match(TokenKind::LParen)) {
      error_ = "expected '(' after cast type";
      return false;
    }
    Expr arg;
    if (!ParseExpr(&arg)) return false;
    if (!Match(TokenKind::RParen)) {
      error_ = "expected ')' after cast expression";
      return false;
    }
    Expr callee;
    callee.kind = ExprKind::Identifier;
    callee.text = "@" + cast_type.name;
    callee.line = cast_type.line;
    callee.column = cast_type.column;
    Expr call;
    call.kind = ExprKind::Call;
    call.cast_type = std::move(cast_type);
    call.children.push_back(std::move(callee));
    call.args.push_back(std::move(arg));
    if (out) *out = std::move(call);
    return true;
  }
  if (tok.kind == TokenKind::Bang || tok.kind == TokenKind::Minus ||
      tok.kind == TokenKind::Amp || tok.kind == TokenKind::Star ||
      tok.kind == TokenKind::KwAwait ||
      tok.kind == TokenKind::PlusPlus || tok.kind == TokenKind::MinusMinus) {
    Advance();
    Expr operand;
    if (!ParseUnaryExpr(&operand)) return false;
    Expr expr;
    expr.kind = ExprKind::Unary;
    expr.op = tok.kind == TokenKind::KwAwait ? "await" : tok.text;
    if (tok.kind == TokenKind::KwAwait && operand.kind == ExprKind::Unary &&
        operand.op == "post?" && operand.children.size() == 1) {
      expr.children.push_back(std::move(operand.children[0]));
      operand.children[0] = std::move(expr);
      if (out) *out = std::move(operand);
      return true;
    }
    expr.children.push_back(std::move(operand));
    if (out) *out = std::move(expr);
    return true;
  }
  return ParsePostfixExpr(out);
}

bool Parser::ParsePostfixExpr(Expr* out) {
  Expr expr;
  if (!ParsePrimaryExpr(&expr)) return false;
  for (;;) {
    if (Peek().kind == TokenKind::Lt && LooksLikeTypeArgsForCall()) {
      std::vector<TypeRef> type_args;
      if (!Match(TokenKind::Lt)) return false;
      if (!ParseTypeArgs(&type_args)) return false;
      if (!Match(TokenKind::LParen)) {
        error_ = "expected '(' after type arguments";
        return false;
      }
      Expr call;
      call.kind = ExprKind::Call;
      call.children.push_back(std::move(expr));
      call.type_args = std::move(type_args);
      if (!ParseCallArgs(&call.args)) return false;
      expr = std::move(call);
      continue;
    }
    if (Match(TokenKind::LParen)) {
      Expr call;
      call.kind = ExprKind::Call;
      call.children.push_back(std::move(expr));
      if (!ParseCallArgs(&call.args)) return false;
      expr = std::move(call);
      continue;
    }
    if (Match(TokenKind::LBracket)) {
      Expr index;
      index.kind = ExprKind::Index;
      index.children.push_back(std::move(expr));
      Expr idx_expr;
      if (!ParseExpr(&idx_expr)) return false;
      if (!Match(TokenKind::RBracket)) {
        error_ = "expected ']' after index expression";
        return false;
      }
      index.children.push_back(std::move(idx_expr));
      expr = std::move(index);
      continue;
    }
    if (Match(TokenKind::Dot) || Match(TokenKind::Arrow)) {
      const std::string op = tokens_[index_ - 1].text;
      const Token& name = Peek();
      if (name.kind != TokenKind::Identifier && name.kind != TokenKind::KwAwait) {
        error_ = "expected member name after '" + op + "'";
        return false;
      }
      Advance();
      Expr member;
      member.kind = ExprKind::Member;
      member.op = op;
      member.text = name.text;
      member.line = name.line;
      member.column = name.column;
      member.children.push_back(std::move(expr));
      expr = std::move(member);
      continue;
    }
    if (Match(TokenKind::DoubleColon)) {
      error_ = "invalid member access '::' (use '.' for members)";
      return false;
    }
    if (Match(TokenKind::PlusPlus) || Match(TokenKind::MinusMinus)) {
      Expr unary;
      unary.kind = ExprKind::Unary;
      unary.op = "post" + tokens_[index_ - 1].text;
      unary.children.push_back(std::move(expr));
      expr = std::move(unary);
      continue;
    }
    if (Match(TokenKind::Question)) {
      const Token& question = tokens_[index_ - 1];
      Expr propagation;
      propagation.kind = ExprKind::Unary;
      propagation.op = "post?";
      propagation.line = question.line;
      propagation.column = question.column;
      propagation.children.push_back(std::move(expr));
      expr = std::move(propagation);
      continue;
    }
    break;
  }
  if (out) *out = std::move(expr);
  return true;
}

bool Parser::LooksLikeTypeArgsForCall() const {
  if (Peek().kind != TokenKind::Lt) return false;
  size_t i = index_;
  int depth = 0;
  for (; i < tokens_.size(); ++i) {
    const TokenKind kind = tokens_[i].kind;
    if (kind == TokenKind::Lt) {
      depth++;
      continue;
    }
    if (kind == TokenKind::Gt || kind == TokenKind::Shr) {
      depth -= kind == TokenKind::Shr ? 2 : 1;
      if (depth == 0) break;
      if (depth < 0) return false;
      continue;
    }
    if (kind == TokenKind::End) return false;
  }
  if (i >= tokens_.size() ||
      (tokens_[i].kind != TokenKind::Gt && tokens_[i].kind != TokenKind::Shr)) {
    return false;
  }
  if (i + 1 >= tokens_.size()) return false;
  return tokens_[i + 1].kind == TokenKind::LParen;
}

bool Parser::ParsePrimaryExpr(Expr* out) {
  const Token& tok = Peek();
  if (tok.kind == TokenKind::KwSwitch) {
    return ParseSwitchExpr(out);
  }
  if (tok.kind == TokenKind::Integer || tok.kind == TokenKind::Float ||
      tok.kind == TokenKind::String || tok.kind == TokenKind::Char ||
      tok.kind == TokenKind::KwTrue || tok.kind == TokenKind::KwFalse) {
    if (tok.kind == TokenKind::String && allow_format_expr_ && Peek(1).kind == TokenKind::Comma) {
      Expr expr;
      expr.kind = ExprKind::FormatString;
      expr.text = tok.text;
      expr.line = tok.line;
      expr.column = tok.column;
      Advance();
      bool saw_arg = false;
      while (Match(TokenKind::Comma)) {
        saw_arg = true;
        Expr value;
        if (!ParseExpr(&value)) return false;
        expr.args.push_back(std::move(value));
      }
      if (!saw_arg) {
        error_ = "format expression expects at least one value after string literal";
        return false;
      }
      if (out) *out = std::move(expr);
      return true;
    }
    Expr expr;
    expr.kind = ExprKind::Literal;
    expr.text = tok.text;
    if (tok.kind == TokenKind::Integer) expr.literal_kind = LiteralKind::Integer;
    else if (tok.kind == TokenKind::Float) expr.literal_kind = LiteralKind::Float;
    else if (tok.kind == TokenKind::String) expr.literal_kind = LiteralKind::String;
    else if (tok.kind == TokenKind::Char) expr.literal_kind = LiteralKind::Char;
    else expr.literal_kind = LiteralKind::Bool;
    expr.line = tok.line;
    expr.column = tok.column;
    Advance();
    if (out) *out = std::move(expr);
    return true;
  }
  if (tok.kind == TokenKind::LParen) {
    size_t save = index_;
    if (ParseFnLiteral(out)) return true;
    index_ = save;
  }
  if (tok.kind == TokenKind::Identifier || tok.kind == TokenKind::KwSelf) {
    Expr expr;
    expr.kind = ExprKind::Identifier;
    expr.text = tok.text;
    expr.line = tok.line;
    expr.column = tok.column;
    Advance();
    if (out) *out = std::move(expr);
    return true;
  }
  if (Match(TokenKind::LParen)) {
    Expr expr;
    if (!ParseExpr(&expr)) return false;
    if (!Match(TokenKind::RParen)) {
      error_ = "expected ')' after expression";
      return false;
    }
    if (out) *out = std::move(expr);
    return true;
  }
  if (Match(TokenKind::LBracket)) {
    std::vector<Expr> elements;
    if (!ParseBracketExprList(&elements)) return false;
    Expr expr;
    expr.kind = ExprKind::ListLiteral;
    expr.children = std::move(elements);
    if (out) *out = std::move(expr);
    return true;
  }
  if (Match(TokenKind::LBrace)) {
    Expr expr;
    expr.kind = ExprKind::AggregateLiteral;
    bool seen_named = false;
    bool seen_positional = false;
    if (Match(TokenKind::RBrace)) {
      if (out) *out = std::move(expr);
      return true;
    }
    while (!IsAtEnd()) {
      if (Match(TokenKind::Dot)) {
        if (seen_positional) {
          error_ = "cannot mix positional and named fields in aggregate literal";
          return false;
        }
        const Token& field_tok = Peek();
        if (field_tok.kind != TokenKind::Identifier) {
          error_ = "expected field name after '.' in aggregate literal";
          return false;
        }
        Advance();
        if (!Match(TokenKind::Assign)) {
          error_ = "expected '=' after aggregate field name";
          return false;
        }
        Expr value;
        if (!ParseNonFormatExpr(&value)) return false;
        expr.field_names.push_back(field_tok.text);
        expr.field_values.push_back(std::move(value));
        seen_named = true;
      } else if (Peek().kind == TokenKind::Identifier && Peek(1).kind == TokenKind::Colon) {
        if (seen_positional) {
          error_ = "cannot mix positional and named fields in aggregate literal";
          return false;
        }
        Token field_tok = Advance();
        Advance(); // ':'
        Expr value;
        if (!ParseNonFormatExpr(&value)) return false;
        expr.field_names.push_back(field_tok.text);
        expr.field_values.push_back(std::move(value));
        seen_named = true;
      } else {
        if (seen_named) {
          error_ = "cannot mix positional and named fields in aggregate literal";
          return false;
        }
        Expr value;
        if (!ParseNonFormatExpr(&value)) return false;
        expr.children.push_back(std::move(value));
        seen_positional = true;
      }
      if (Match(TokenKind::Comma)) continue;
      if (Match(TokenKind::RBrace)) break;
      error_ = "expected ',' or '}' in aggregate literal";
      return false;
    }
    if (out) *out = std::move(expr);
    return true;
  }
  error_ = "expected expression";
  return false;
}

bool Parser::ParseSwitchExpr(Expr* out) {
  if (!Match(TokenKind::KwSwitch)) return false;
  if (!Match(TokenKind::LParen)) {
    error_ = "expected '(' after switch";
    return false;
  }
  Expr subject;
  if (!ParseExpr(&subject)) return false;
  if (!Match(TokenKind::RParen)) {
    error_ = "expected ')' after switch expression";
    return false;
  }
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start switch body";
    return false;
  }
  Expr expr;
  expr.kind = ExprKind::Switch;
  expr.children.push_back(std::move(subject));
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) break;
    SwitchBranch branch;
    if (Match(TokenKind::KwDefault)) {
      branch.is_default = true;
    } else if (Match(TokenKind::LBrace)) {
      if (Match(TokenKind::RBrace)) {
        branch.pattern_kind = SwitchPatternKind::Absent;
      } else if (Match(TokenKind::Dot)) {
        const Token& field = Peek();
        if (field.kind != TokenKind::Identifier) {
          error_ = "expected tagged field name after '.' in switch pattern";
          return false;
        }
        Advance();
        if (!Match(TokenKind::Assign)) {
          error_ = "expected '=' after tagged field name in switch pattern";
          return false;
        }
        const Token& binding = Peek();
        if (binding.kind != TokenKind::Identifier) {
          error_ = "expected binding name in tagged switch pattern";
          return false;
        }
        Advance();
        if (!Match(TokenKind::RBrace)) {
          error_ = "expected '}' after tagged switch pattern";
          return false;
        }
        branch.pattern_kind = SwitchPatternKind::Tagged;
        branch.pattern_field = field.text;
        branch.pattern_binding = binding.text;
      } else {
        const Token& binding = Peek();
        if (binding.kind != TokenKind::Identifier) {
          error_ = "expected binding name or '}' in optional switch pattern";
          return false;
        }
        Advance();
        if (!Match(TokenKind::RBrace)) {
          error_ = "expected '}' after optional switch pattern";
          return false;
        }
        branch.pattern_kind = SwitchPatternKind::Present;
        branch.pattern_binding = binding.text;
      }
    } else {
      Expr cond;
      if (!ParseExpr(&cond)) return false;
      branch.condition = std::move(cond);
    }
    if (!Match(TokenKind::FatArrow)) {
      error_ = "expected '=>' after switch condition";
      return false;
    }
    if (Match(TokenKind::KwReturn)) {
      Expr value;
      if (!ParseExpr(&value)) return false;
      branch.has_inline_value = true;
      branch.is_explicit_return = true;
      branch.value = std::move(value);
    } else if (Peek().kind == TokenKind::LBrace) {
      branch.is_block = true;
      if (!ParseBlockStmts(&branch.block)) return false;
    } else {
      Expr value;
      if (!ParseExpr(&value)) return false;
      branch.has_inline_value = true;
      branch.is_explicit_return = false;
      branch.value = std::move(value);
    }
    expr.switch_branches.push_back(std::move(branch));
    if (Match(TokenKind::Semicolon)) continue;
    if (IsImplicitStmtTerminator()) continue;
    if (Peek().kind == TokenKind::RBrace) continue;
    error_ = "expected ';' or '}' after switch branch";
    return false;
  }
  if (out) *out = std::move(expr);
  return true;
}

bool Parser::ParseFnLiteral(Expr* out) {
  if (!Match(TokenKind::LParen)) return false;
  const Token lparen = tokens_[index_ - 1];
  std::vector<ParamDecl> params;
  if (!Match(TokenKind::RParen)) {
    for (;;) {
      const Token& name_tok = Peek();
      if (name_tok.kind != TokenKind::Identifier) {
        error_ = "expected parameter name in function literal";
        return false;
      }
      Advance();
      ParamDecl param;
      param.name = name_tok.text;
      if (Match(TokenKind::Colon) || Match(TokenKind::DoubleColon)) {
        param.mutability = tokens_[index_ - 1].kind == TokenKind::Colon
                               ? Mutability::Mutable
                               : Mutability::Immutable;
        if (!ParseTypeInner(&param.type)) return false;
      }
      params.push_back(std::move(param));
      if (Match(TokenKind::Comma)) continue;
      if (Match(TokenKind::RParen)) break;
      error_ = "expected ',' or ')' after parameter";
      return false;
    }
  }
  std::vector<Stmt> body;
  if (!ParseBlockStmts(&body)) return false;
  if (out) {
    out->kind = ExprKind::FnLiteral;
    out->fn_params = std::move(params);
    out->fn_body = std::move(body);
    out->line = lparen.line;
    out->column = lparen.column;
  }
  return true;
}

bool Parser::ParseCallArgs(std::vector<Expr>* out) {
  if (Match(TokenKind::RParen)) return true;
  for (;;) {
    if (Peek().kind == TokenKind::Identifier && Peek(1).kind == TokenKind::LBrace) {
      error_ = "unexpected type name before aggregate literal in call; use '{...}' and "
               "assign to a typed variable first";
      return false;
    }
    Expr arg;
    if (!ParseNonFormatExpr(&arg)) return false;
    if (out) out->push_back(std::move(arg));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RParen)) break;
    if (Peek().kind == TokenKind::LBrace) {
      error_ = "unexpected '{' after call argument; aggregate literal uses '{...}' and "
               "must be assigned to a typed variable";
      return false;
    }
    error_ = "expected ',' or ')' in call arguments";
    return false;
  }
  return true;
}

bool Parser::ParseBracketExprList(std::vector<Expr>* out) {
  if (Match(TokenKind::RBracket)) return true;
  for (;;) {
    Expr element;
    if (!ParseNonFormatExpr(&element)) return false;
    if (out) out->push_back(std::move(element));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RBracket)) break;
    error_ = "expected ',' or ']' in list";
    return false;
  }
  return true;
}

bool Parser::MatchTypeArgumentClose() {
  if (pending_type_argument_closes_ > 0) {
    --pending_type_argument_closes_;
    return true;
  }
  if (Match(TokenKind::Gt)) return true;
  if (Match(TokenKind::Shr)) {
    pending_type_argument_closes_ = 1;
    return true;
  }
  return false;
}

bool Parser::ParseTypeArgs(std::vector<TypeRef>* out) {
  if (!out) return false;
  if (Match(TokenKind::Gt)) {
    error_ = "empty type argument list";
    return false;
  }
  for (;;) {
    TypeRef arg;
    if (!ParseTypeInner(&arg)) return false;
    out->push_back(std::move(arg));
    if (Match(TokenKind::Comma)) {
      continue;
    }
    if (MatchTypeArgumentClose()) break;
    error_ = "expected ',' or '>' in type arguments";
    return false;
  }
  return true;
}

bool Parser::ParseTypeDims(TypeRef* out) {
  if (!out) return false;
  for (;;) {
    if (Match(TokenKind::LBracket)) {
      // [] is list-only in the new syntax.
      if (Match(TokenKind::RBracket)) {
        TypeDim dim;
        dim.is_list = true;
        dim.has_size = false;
        out->dims.push_back(dim);
        continue;
      }
      error_ = "static array types use '{N}' or '{}' (lists use '[]')";
      return false;
    }
    if (!IsTypeDimensionBrace() || !Match(TokenKind::LBrace)) break;
    TypeDim dim;
    dim.is_list = false;
    if (Match(TokenKind::RBrace)) {
      dim.has_size = false;
      out->dims.push_back(dim);
      continue;
    }
    const Token& size_tok = Peek();
    if (size_tok.kind != TokenKind::Integer) {
      error_ = "expected array size literal";
      return false;
    }
    dim.has_size = true;
    if (!ParseIntegerLiteral(size_tok.text, &dim.size)) {
      error_ = "invalid array size literal";
      return false;
    }
    Advance();
    if (!Match(TokenKind::RBrace)) {
      error_ = "expected '}' after array size";
      return false;
    }
    out->dims.push_back(dim);
  }
  return true;
}

bool ParseTypeFromString(const std::string& text, TypeRef* out, std::string* error) {
  Lexer lexer(text);
  if (!lexer.Lex()) {
    if (error) *error = lexer.Error();
    return false;
  }
  Parser parser(lexer.Tokens());
  if (!parser.ParseType(out)) {
    if (error) *error = parser.Error();
    return false;
  }
  return true;
}

bool ParseProgramFromString(const std::string& text, Program* out, std::string* error) {
  Lexer lexer(text);
  if (!lexer.Lex()) {
    if (error) *error = lexer.Error();
    return false;
  }
  Parser parser(lexer.Tokens());
  if (!parser.ParseProgram(out)) {
    if (error) *error = parser.ErrorWithLocation();
    return false;
  }
  return true;
}

} // namespace Simple::Lang::CAST
