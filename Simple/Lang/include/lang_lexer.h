#pragma once

#include <string>
#include <vector>

#include "lang_token.h"

namespace Simple::Lang {

class Lexer {
public:
  explicit Lexer(std::string source);

  const std::vector<Token>& Tokens() const { return tokens_; }
  const std::string& Error() const { return error_; }

  bool Lex();

private:
  char Peek(size_t offset = 0) const;
  char Advance();
  bool Match(char expected);
  bool IsAtEnd() const;

  void SkipWhitespaceAndComments();
  void AddToken(TokenKind kind, const std::string& text);
  void AddSimpleToken(TokenKind kind);

  bool LexIdentifierOrKeyword();
  bool LexNumber();
  bool LexString();
  bool LexChar();

  std::string source_;
  size_t index_ = 0;
  uint32_t line_ = 1;
  uint32_t column_ = 1;
  std::vector<Token> tokens_;
  std::string error_;
};

} // namespace Simple::Lang
