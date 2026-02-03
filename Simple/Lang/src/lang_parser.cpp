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

} // namespace Simple::Lang
