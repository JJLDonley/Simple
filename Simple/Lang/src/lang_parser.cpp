#include "lang_parser.h"

#include "lang_lexer.h"

namespace Simple::Lang {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

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
  while (!IsAtEnd()) {
    Decl decl;
    if (!ParseDecl(&decl)) return false;
    out->decls.push_back(std::move(decl));
  }
  return true;
}

bool Parser::ParseTypeInner(TypeRef* out) {
  const Token& tok = Peek();
  if (tok.kind != TokenKind::Identifier) {
    error_ = "expected type name";
    return false;
  }
  out->name = tok.text;
  Advance();

  if (Match(TokenKind::Lt)) {
    if (!ParseTypeArgs(&out->type_args)) return false;
  }

  if (!ParseTypeDims(&out->dims)) return false;
  return true;
}

bool Parser::ParseDecl(Decl* out) {
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

  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
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
    if (!ParseBlockTokens(&out->func.body_tokens)) return false;
    return true;
  }

  if (out) {
    out->kind = DeclKind::Variable;
    out->var.name = name_tok.text;
    out->var.mutability = mut;
    out->var.type = std::move(return_or_type);
  }
  if (!Match(TokenKind::Assign)) {
    error_ = "expected '=' in variable declaration";
    return false;
  }
  if (!ParseInitTokens(&out->var.init_tokens)) return false;
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

bool Parser::ParseInitTokens(std::vector<Token>* out) {
  while (!IsAtEnd()) {
    if (Peek().kind == TokenKind::Semicolon) {
      Advance();
      return true;
    }
    if (out) out->push_back(Advance());
  }
  error_ = "unterminated variable declaration";
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
    if (Match(TokenKind::Gt)) {
      break;
    }
    error_ = "expected ',' or '>' in type arguments";
    return false;
  }
  return true;
}

bool Parser::ParseTypeDims(std::vector<TypeDim>* out) {
  while (Match(TokenKind::LBracket)) {
    TypeDim dim;
    if (Match(TokenKind::RBracket)) {
      dim.is_list = true;
      dim.has_size = false;
      out->push_back(dim);
      continue;
    }
    const Token& size_tok = Peek();
    if (size_tok.kind != TokenKind::Integer) {
      error_ = "expected array size literal";
      return false;
    }
    dim.has_size = true;
    dim.is_list = false;
    dim.size = static_cast<uint64_t>(std::stoull(size_tok.text));
    Advance();
    if (!Match(TokenKind::RBracket)) {
      error_ = "expected ']' after array size";
      return false;
    }
    out->push_back(dim);
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
    if (error) *error = parser.Error();
    return false;
  }
  return true;
}

} // namespace Simple::Lang
