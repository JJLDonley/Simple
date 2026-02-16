#include "lang_parser.h"

#include "lang_lexer.h"

#include <limits>
#include <stdexcept>

namespace {

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

namespace Simple::Lang {

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
    case TokenKind::KwSelf:
    case TokenKind::KwArtifact:
    case TokenKind::KwEnum:
    case TokenKind::KwModule:
    case TokenKind::KwImport:
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

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

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

bool Parser::ParseTypeInner(TypeRef* out) {
  if (!out) return false;

  if (Match(TokenKind::KwFn)) {
    TypeRef proc;
    proc.is_proc = true;
    proc.proc_params.clear();
    proc.proc_return.reset();
    proc.proc_return_mutability = Mutability::Mutable;
    proc.type_args.clear();
    proc.dims.clear();
    proc.pointer_depth = 0;

    if (Match(TokenKind::Lt)) {
      if (!ParseTypeArgs(&proc.type_args)) return false;
    }

    TypeRef ret;
    if (!ParseTypeInner(&ret)) return false;
    proc.proc_return = std::make_unique<TypeRef>(std::move(ret));

    if (!Match(TokenKind::LParen)) {
      error_ = "expected '(' after fn return type";
      return false;
    }
    if (!Match(TokenKind::RParen)) {
      for (;;) {
        // Allow optional parameter name and mutability; only the type is stored.
        if (Peek().kind == TokenKind::Identifier &&
            (Peek(1).kind == TokenKind::Colon || Peek(1).kind == TokenKind::DoubleColon)) {
          Advance(); // name
          Advance(); // ':' or '::'
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

    if (!ParseTypeDims(&proc)) return false;
    while (Match(TokenKind::Star)) {
      ++proc.pointer_depth;
    }
    *out = std::move(proc);
    return true;
  }

  const Token& tok = Peek();
  if (tok.kind != TokenKind::Identifier) {
    error_ = "expected type name";
    return false;
  }
  out->type_args.clear();
  out->dims.clear();
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

  if (!ParseTypeDims(out)) return false;
  while (Match(TokenKind::Star)) {
    ++out->pointer_depth;
  }
  return true;
}

bool Parser::ParseDecl(Decl* out) {
  if (Match(TokenKind::KwFn)) {
    const Token& name_tok = Peek();
    if (name_tok.kind != TokenKind::Identifier) {
      error_ = "expected function name after 'fn'";
      return false;
    }
    Advance();

    std::vector<std::string> generics;
    if (Match(TokenKind::Lt)) {
      if (!ParseGenerics(&generics)) return false;
    }

    Mutability mut = Mutability::Immutable;
    if (Match(TokenKind::Colon)) {
      mut = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      mut = Mutability::Immutable;
    } else {
      error_ = "expected ':' or '::' after function name";
      return false;
    }

    TypeRef return_type;
    if (!ParseTypeInner(&return_type)) return false;
    if (!Match(TokenKind::LParen)) {
      error_ = "expected '(' after function return type";
      return false;
    }
    if (out) {
      out->kind = DeclKind::Function;
      out->func.name = name_tok.text;
      out->func.generics = std::move(generics);
      out->func.return_mutability = mut;
      out->func.return_type = std::move(return_type);
    }
    if (!ParseParamList(&out->func.params)) return false;
    if (!ParseBlockStmts(&out->func.body)) return false;
    return true;
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
    if (!ParseTypeInner(&return_type)) return false;
    if (!Match(TokenKind::LParen)) {
      error_ = "expected '(' after extern return type";
      return false;
    }
    std::vector<ParamDecl> params;
    if (!ParseParamList(&params)) return false;
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
    if (Match(TokenKind::KwArtifact)) {
      return ParseArtifactDecl(name_tok, std::move(generics), out);
    }
    if (Match(TokenKind::KwModule)) {
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
    TypeRef return_or_type;
    if (!ParseTypeInner(&return_or_type)) return false;
    if (Match(TokenKind::LParen)) {
      if (out) {
        out->kind = DeclKind::Function;
        out->func.name = name_tok.text;
        out->func.generics = std::move(generics);
        out->func.return_mutability = mut;
        out->func.return_type = std::move(return_or_type);
      }
      if (!ParseParamList(&out->func.params)) return false;
      if (!ParseBlockStmts(&out->func.body)) return false;
      return true;
    }
    if (out) {
      out->kind = DeclKind::Variable;
      out->var.name = name_tok.text;
      out->var.mutability = mut;
      out->var.type = std::move(return_or_type);
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

  TypeRef return_or_type;
  if (!ParseTypeInner(&return_or_type)) return false;

  if (Match(TokenKind::LParen)) {
    if (out) {
      out->kind = DeclKind::Function;
      out->func.name = name_tok.text;
      out->func.generics = std::move(generics);
      out->func.return_mutability = mut;
      out->func.return_type = std::move(return_or_type);
    }
    if (!ParseParamList(&out->func.params)) return false;
    if (!ParseBlockStmts(&out->func.body)) return false;
    return true;
  }

  if (out) {
    out->kind = DeclKind::Variable;
    out->var.name = name_tok.text;
    out->var.mutability = mut;
    out->var.type = std::move(return_or_type);
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

bool Parser::ParseArtifactDecl(const Token& name_tok,
                               std::vector<std::string> generics,
                               Decl* out) {
  if (out) {
    out->kind = DeclKind::Artifact;
    out->artifact.name = name_tok.text;
    out->artifact.generics = std::move(generics);
  }
  if (!ParseArtifactBody(&out->artifact)) return false;
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
  if (out) {
    out->kind = DeclKind::Enum;
    out->enm.name = name_tok.text;
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
      const Token& value_tok = Peek();
      if (value_tok.kind != TokenKind::Integer) {
        error_ = "expected integer literal for enum value";
        return false;
      }
      member.has_value = true;
      member.value_text = value_tok.text;
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

bool Parser::ParseArtifactBody(ArtifactDecl* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start artifact body";
    return false;
  }
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) return true;
    if (!ParseArtifactMember(out)) return false;
  }
  error_ = "unterminated artifact body";
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

bool Parser::ParseArtifactMember(ArtifactDecl* out) {
  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    if (name_tok.kind == TokenKind::Comma) {
      error_ = "unexpected ',' in artifact body; use newline or ';' between members";
      return false;
    }
    error_ = "expected artifact member name";
    return false;
  }
  Advance();

  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after member name";
    return false;
  }

  TypeRef type;
  if (!ParseTypeInner(&type)) return false;

  if (Match(TokenKind::LParen)) {
    FuncDecl fn;
    fn.name = name_tok.text;
    fn.return_mutability = mut;
    fn.return_type = std::move(type);
    if (!ParseParamList(&fn.params)) return false;
    if (!ParseBlockStmts(&fn.body)) return false;
    if (out) out->methods.push_back(std::move(fn));
    return true;
  }

  VarDecl field;
  field.name = name_tok.text;
  field.mutability = mut;
  field.type = std::move(type);
  if (Match(TokenKind::Assign)) {
    Expr init;
    if (!ParseExpr(&init)) return false;
    if (!ConsumeStmtTerminator("artifact field declaration")) return false;
    field.has_init_expr = true;
    field.init_expr = std::move(init);
  } else if (Match(TokenKind::Semicolon)) {
    // optional
  } else if (IsImplicitStmtTerminator()) {
    // optional
  } else {
    if (Peek().kind == TokenKind::Comma) {
      error_ = "unexpected ',' in artifact body; use newline or ';' between members";
    } else {
      error_ = "expected '=' or ';' in artifact field declaration";
    }
    return false;
  }
  if (out) out->fields.push_back(std::move(field));
  return true;
}

bool Parser::ParseModuleMember(ModuleDecl* out) {
  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    error_ = "expected module member name";
    return false;
  }
  Advance();

  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after member name";
    return false;
  }

  TypeRef type;
  if (!ParseTypeInner(&type)) return false;

  if (Match(TokenKind::LParen)) {
    FuncDecl fn;
    fn.name = name_tok.text;
    fn.return_mutability = mut;
    fn.return_type = std::move(type);
    if (!ParseParamList(&fn.params)) return false;
    if (!ParseBlockStmts(&fn.body)) return false;
    if (out) out->functions.push_back(std::move(fn));
    return true;
  }

  VarDecl var;
  var.name = name_tok.text;
  var.mutability = mut;
  var.type = std::move(type);
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
  if (ParsePostfixExpr(&target)) {
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
  return ParseAssignmentExpr(out);
}

bool Parser::ParseAssignmentExpr(Expr* out) {
  size_t save = index_;
  Expr target;
  if (ParsePostfixExpr(&target)) {
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
    if (cast_type.is_proc || !cast_type.type_args.empty() || !cast_type.dims.empty()) {
      error_ = "cast expects primitive type name in @T(value)";
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
    call.children.push_back(std::move(callee));
    call.args.push_back(std::move(arg));
    if (out) *out = std::move(call);
    return true;
  }
  if (tok.kind == TokenKind::Bang || tok.kind == TokenKind::Minus ||
      tok.kind == TokenKind::Amp ||
      tok.kind == TokenKind::PlusPlus || tok.kind == TokenKind::MinusMinus) {
    Advance();
    Expr operand;
    if (!ParseUnaryExpr(&operand)) return false;
    Expr expr;
    expr.kind = ExprKind::Unary;
    expr.op = tok.text;
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
      if (name.kind != TokenKind::Identifier) {
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
    if (kind == TokenKind::Gt) {
      depth--;
      if (depth == 0) break;
      continue;
    }
    if (kind == TokenKind::End) return false;
  }
  if (i >= tokens_.size() || tokens_[i].kind != TokenKind::Gt) return false;
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
    expr.kind = ExprKind::ArtifactLiteral;
    bool seen_named = false;
    bool seen_positional = false;
    if (Match(TokenKind::RBrace)) {
      if (out) *out = std::move(expr);
      return true;
    }
    while (!IsAtEnd()) {
      if (Match(TokenKind::Dot)) {
        if (seen_positional) {
          error_ = "cannot mix positional and named fields in artifact literal";
          return false;
        }
        const Token& field_tok = Peek();
        if (field_tok.kind != TokenKind::Identifier) {
          error_ = "expected field name after '.' in artifact literal";
          return false;
        }
        Advance();
        if (!Match(TokenKind::Assign)) {
          error_ = "expected '=' after artifact field name";
          return false;
        }
        Expr value;
        if (!ParseExpr(&value)) return false;
        expr.field_names.push_back(field_tok.text);
        expr.field_values.push_back(std::move(value));
        seen_named = true;
      } else if (Peek().kind == TokenKind::Identifier && Peek(1).kind == TokenKind::Colon) {
        if (seen_positional) {
          error_ = "cannot mix positional and named fields in artifact literal";
          return false;
        }
        Token field_tok = Advance();
        Advance(); // ':'
        Expr value;
        if (!ParseExpr(&value)) return false;
        expr.field_names.push_back(field_tok.text);
        expr.field_values.push_back(std::move(value));
        seen_named = true;
      } else {
        if (seen_named) {
          error_ = "cannot mix positional and named fields in artifact literal";
          return false;
        }
        Expr value;
        if (!ParseExpr(&value)) return false;
        expr.children.push_back(std::move(value));
        seen_positional = true;
      }
      if (Match(TokenKind::Comma)) continue;
      if (Match(TokenKind::RBrace)) break;
      error_ = "expected ',' or '}' in artifact literal";
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
  size_t start_index = index_ - 1;
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
      params.push_back(std::move(param));
      if (Match(TokenKind::Comma)) continue;
      if (Match(TokenKind::RParen)) break;
      error_ = "expected ',' or ')' after parameter";
      return false;
    }
  }
  std::vector<Token> body_tokens;
  if (!ParseBlockTokens(&body_tokens)) return false;
  body_tokens.insert(body_tokens.begin(), tokens_[start_index]);
  if (out) {
    out->kind = ExprKind::FnLiteral;
    out->fn_params = std::move(params);
    out->fn_body_tokens = std::move(body_tokens);
  }
  return true;
}

bool Parser::ParseTypedFnLiteral(Expr* out, TypeRef* out_proc_type) {
  TypeRef return_type;
  if (!ParseTypeInner(&return_type)) return false;
  if (!Match(TokenKind::LParen)) {
    error_ = "expected '(' after function return type";
    return false;
  }
  Token lparen_tok = tokens_[index_ - 1];
  std::vector<ParamDecl> params;
  if (!Match(TokenKind::RParen)) {
    for (;;) {
      ParamDecl param;
      if (!ParseParam(&param)) return false;
      params.push_back(std::move(param));
      if (Match(TokenKind::Comma)) continue;
      if (Match(TokenKind::RParen)) break;
      error_ = "expected ',' or ')' after parameter";
      return false;
    }
  }

  std::vector<Token> body_tokens;
  if (!ParseBlockTokens(&body_tokens)) return false;
  body_tokens.insert(body_tokens.begin(), lparen_tok);

  if (out) {
    out->kind = ExprKind::FnLiteral;
    out->fn_params = params;
    out->fn_body_tokens = std::move(body_tokens);
  }

  if (out_proc_type) {
    out_proc_type->name.clear();
    out_proc_type->pointer_depth = 0;
    out_proc_type->type_args.clear();
    out_proc_type->dims.clear();
    out_proc_type->is_proc = true;
    out_proc_type->proc_return_mutability = Mutability::Mutable;
    out_proc_type->proc_params.clear();
    out_proc_type->proc_params.reserve(params.size());
    for (const auto& param : params) {
      out_proc_type->proc_params.push_back(param.type);
    }
    out_proc_type->proc_return = std::make_unique<TypeRef>(std::move(return_type));
  }
  return true;
}

bool Parser::ParseCallArgs(std::vector<Expr>* out) {
  if (Match(TokenKind::RParen)) return true;
  for (;;) {
    if (Peek().kind == TokenKind::Identifier && Peek(1).kind == TokenKind::LBrace) {
      error_ = "unexpected type name before artifact literal in call; use '{...}' and "
               "assign to a typed variable first";
      return false;
    }
    Expr arg;
    const bool prev_allow_format = allow_format_expr_;
    allow_format_expr_ = false;
    const bool parsed = ParseExpr(&arg);
    allow_format_expr_ = prev_allow_format;
    if (!parsed) return false;
    if (out) out->push_back(std::move(arg));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RParen)) break;
    if (Peek().kind == TokenKind::LBrace) {
      error_ = "unexpected '{' after call argument; artifact literal uses '{...}' and "
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
    if (!ParseExpr(&element)) return false;
    if (out) out->push_back(std::move(element));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RBracket)) break;
    error_ = "expected ',' or ']' in list";
    return false;
  }
  return true;
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
    if (Match(TokenKind::Gt)) {
      break;
    }
    error_ = "expected ',' or '>' in type arguments";
    return false;
  }
  return true;
}

bool Parser::ParseTypeDims(TypeRef* out) {
  if (!out) return false;
  for (;;) {
    if (Match(TokenKind::LBracket)) {
      if (out->is_proc) {
        error_ = "procedure types cannot have array/list dimensions";
        return false;
      }
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
    if (!Match(TokenKind::LBrace)) break;
    if (out->is_proc) {
      error_ = "procedure types cannot have array/list dimensions";
      return false;
    }
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

} // namespace Simple::Lang
