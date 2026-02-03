#pragma once

#include <cstdint>
#include <string>

namespace Simple::Lang {

enum class TokenKind : uint8_t {
  End,
  Invalid,

  Identifier,
  Integer,
  Float,
  String,
  Char,

  KwWhile,
  KwFor,
  KwBreak,
  KwSkip,
  KwReturn,
  KwIf,
  KwElse,
  KwDefault,
  KwFn,
  KwSelf,
  KwArtifact,
  KwEnum,
  KwModule,
  KwUnion,
  KwTrue,
  KwFalse,

  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Comma,
  Dot,
  Semicolon,

  Colon,
  DoubleColon,
  Assign,

  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  PlusPlus,
  MinusMinus,

  Amp,
  Pipe,
  Caret,
  Shl,
  Shr,

  EqEq,
  NotEq,
  Lt,
  Le,
  Gt,
  Ge,

  AndAnd,
  OrOr,
  Bang,

  PlusEq,
  MinusEq,
  StarEq,
  SlashEq,
  PercentEq,
  AmpEq,
  PipeEq,
  CaretEq,
  ShlEq,
  ShrEq,

  PipeGt,
  At,
};

struct Token {
  TokenKind kind = TokenKind::Invalid;
  std::string text;
  uint32_t line = 1;
  uint32_t column = 1;
};

const char* ToString(TokenKind kind);

} // namespace Simple::Lang
