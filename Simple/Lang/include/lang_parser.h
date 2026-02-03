#pragma once

#include <string>
#include <vector>

#include "lang_ast.h"
#include "lang_token.h"

namespace Simple::Lang {

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);

  const std::string& Error() const { return error_; }

  bool ParseType(TypeRef* out);

private:
  const Token& Peek(size_t offset = 0) const;
  const Token& Advance();
  bool Match(TokenKind kind);
  bool IsAtEnd() const;

  bool ParseTypeInner(TypeRef* out);
  bool ParseTypeArgs(std::vector<TypeRef>* out);
  bool ParseTypeDims(std::vector<TypeDim>* out);

  std::vector<Token> tokens_;
  size_t index_ = 0;
  std::string error_;
};

bool ParseTypeFromString(const std::string& text, TypeRef* out, std::string* error);

} // namespace Simple::Lang
