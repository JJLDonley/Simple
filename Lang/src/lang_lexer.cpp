#include "lang_lexer.h"

#include <cctype>
#include <unordered_map>

namespace Simple::Lang {

namespace {

const std::unordered_map<std::string, TokenKind> kKeywords = {
  {"while", TokenKind::KwWhile},
  {"for", TokenKind::KwFor},
  {"break", TokenKind::KwBreak},
  {"skip", TokenKind::KwSkip},
  {"return", TokenKind::KwReturn},
  {"if", TokenKind::KwIf},
  {"else", TokenKind::KwElse},
  {"default", TokenKind::KwDefault},
  {"switch", TokenKind::KwSwitch},
  {"fn", TokenKind::KwFn},
  {"self", TokenKind::KwSelf},
  {"artifact", TokenKind::KwArtifact},
  {"Artifact", TokenKind::KwArtifact},
  {"enum", TokenKind::KwEnum},
  {"Enum", TokenKind::KwEnum},
  {"module", TokenKind::KwModule},
  {"Module", TokenKind::KwModule},
  {"import", TokenKind::KwImport},
  {"extern", TokenKind::KwExtern},
  {"as", TokenKind::KwAs},
  {"true", TokenKind::KwTrue},
  {"false", TokenKind::KwFalse},
};

bool IsIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentPart(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

const char* ToString(TokenKind kind) {
  switch (kind) {
    case TokenKind::End: return "end";
    case TokenKind::Invalid: return "invalid";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Integer: return "integer";
    case TokenKind::Float: return "float";
    case TokenKind::String: return "string";
    case TokenKind::Char: return "char";
    case TokenKind::KwWhile: return "while";
    case TokenKind::KwFor: return "for";
    case TokenKind::KwBreak: return "break";
    case TokenKind::KwSkip: return "skip";
    case TokenKind::KwReturn: return "return";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwElse: return "else";
    case TokenKind::KwDefault: return "default";
    case TokenKind::KwSwitch: return "switch";
    case TokenKind::KwFn: return "fn";
    case TokenKind::KwSelf: return "self";
    case TokenKind::KwArtifact: return "artifact";
    case TokenKind::KwEnum: return "enum";
    case TokenKind::KwModule: return "module";
    case TokenKind::KwImport: return "import";
    case TokenKind::KwExtern: return "extern";
    case TokenKind::KwAs: return "as";
    case TokenKind::KwTrue: return "true";
    case TokenKind::KwFalse: return "false";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::Comma: return ",";
    case TokenKind::Dot: return ".";
    case TokenKind::Arrow: return "->";
    case TokenKind::FatArrow: return "=>";
    case TokenKind::DotDot: return "..";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Colon: return ":";
    case TokenKind::DoubleColon: return "::";
    case TokenKind::Assign: return "=";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::PlusPlus: return "++";
    case TokenKind::MinusMinus: return "--";
    case TokenKind::Amp: return "&";
    case TokenKind::Pipe: return "|";
    case TokenKind::Caret: return "^";
    case TokenKind::Shl: return "<<";
    case TokenKind::Shr: return ">>";
    case TokenKind::EqEq: return "==";
    case TokenKind::NotEq: return "!=";
    case TokenKind::Lt: return "<";
    case TokenKind::Le: return "<=";
    case TokenKind::Gt: return ">";
    case TokenKind::Ge: return ">=";
    case TokenKind::AndAnd: return "&&";
    case TokenKind::OrOr: return "||";
    case TokenKind::Bang: return "!";
    case TokenKind::PlusEq: return "+=";
    case TokenKind::MinusEq: return "-=";
    case TokenKind::StarEq: return "*=";
    case TokenKind::SlashEq: return "/=";
    case TokenKind::PercentEq: return "%=";
    case TokenKind::AmpEq: return "&=";
    case TokenKind::PipeEq: return "|=";
    case TokenKind::CaretEq: return "^=";
    case TokenKind::ShlEq: return "<<=";
    case TokenKind::ShrEq: return ">>=";
    case TokenKind::PipeGt: return "|>";
    case TokenKind::At: return "@";
  }
  return "unknown";
}

bool Lexer::Lex() {
  tokens_.clear();
  error_.clear();

  while (!IsAtEnd()) {
    SkipWhitespaceAndComments();
    if (IsAtEnd()) break;

    char c = Peek();
    if (IsIdentStart(c)) {
      if (!LexIdentifierOrKeyword()) return false;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      if (!LexNumber()) return false;
      continue;
    }

    switch (c) {
      case '(': AddSimpleToken(TokenKind::LParen); break;
      case ')': AddSimpleToken(TokenKind::RParen); break;
      case '{': AddSimpleToken(TokenKind::LBrace); break;
      case '}': AddSimpleToken(TokenKind::RBrace); break;
      case '[': AddSimpleToken(TokenKind::LBracket); break;
      case ']': AddSimpleToken(TokenKind::RBracket); break;
      case ',': AddSimpleToken(TokenKind::Comma); break;
      case '.':
        if (Peek(1) == '.') {
          Advance();
          Advance();
          AddToken(TokenKind::DotDot, "..");
        } else {
          AddSimpleToken(TokenKind::Dot);
        }
        break;
      case ';': AddSimpleToken(TokenKind::Semicolon); break;
      case ':':
        Advance();
        if (Match(':')) {
          AddToken(TokenKind::DoubleColon, "::");
        } else {
          AddToken(TokenKind::Colon, ":");
        }
        break;
      case '=':
        Advance();
        if (Match('=')) {
          AddToken(TokenKind::EqEq, "==");
        } else if (Match('>')) {
          AddToken(TokenKind::FatArrow, "=>");
        } else {
          AddToken(TokenKind::Assign, "=");
        }
        break;
      case '+':
        Advance();
        if (Match('+')) {
          AddToken(TokenKind::PlusPlus, "++");
        } else if (Match('=')) {
          AddToken(TokenKind::PlusEq, "+=");
        } else {
          AddToken(TokenKind::Plus, "+");
        }
        break;
      case '-':
        Advance();
        if (Match('>')) {
          AddToken(TokenKind::Arrow, "->");
        } else if (Match('-')) {
          AddToken(TokenKind::MinusMinus, "--");
        } else if (Match('=')) {
          AddToken(TokenKind::MinusEq, "-=");
        } else {
          AddToken(TokenKind::Minus, "-");
        }
        break;
      case '*':
        Advance();
        if (Match('=')) {
          AddToken(TokenKind::StarEq, "*=");
        } else {
          AddToken(TokenKind::Star, "*");
        }
        break;
      case '/':
        Advance();
        if (Match('=')) {
          AddToken(TokenKind::SlashEq, "/=");
        } else {
          AddToken(TokenKind::Slash, "/");
        }
        break;
      case '%':
        Advance();
        if (Match('=')) {
          AddToken(TokenKind::PercentEq, "%=");
        } else {
          AddToken(TokenKind::Percent, "%");
        }
        break;
      case '&':
        Advance();
        if (Match('&')) {
          AddToken(TokenKind::AndAnd, "&&");
        } else if (Match('=')) {
          AddToken(TokenKind::AmpEq, "&=");
        } else {
          AddToken(TokenKind::Amp, "&");
        }
        break;
      case '|':
        Advance();
        if (Match('|')) {
          AddToken(TokenKind::OrOr, "||");
        } else if (Match('=')) {
          AddToken(TokenKind::PipeEq, "|=");
        } else if (Match('>')) {
          AddToken(TokenKind::PipeGt, "|>");
        } else {
          AddToken(TokenKind::Pipe, "|");
        }
        break;
      case '^':
        Advance();
        if (Match('=')) {
          AddToken(TokenKind::CaretEq, "^=");
        } else {
          AddToken(TokenKind::Caret, "^");
        }
        break;
      case '<':
        Advance();
        if (Match('<')) {
          if (Match('=')) {
            AddToken(TokenKind::ShlEq, "<<=");
          } else {
            AddToken(TokenKind::Shl, "<<");
          }
        } else if (Match('=')) {
          AddToken(TokenKind::Le, "<=");
        } else {
          AddToken(TokenKind::Lt, "<");
        }
        break;
      case '>':
        Advance();
        if (Match('>')) {
          if (Match('=')) {
            AddToken(TokenKind::ShrEq, ">>=");
          } else {
            AddToken(TokenKind::Shr, ">>");
          }
        } else if (Match('=')) {
          AddToken(TokenKind::Ge, ">=");
        } else {
          AddToken(TokenKind::Gt, ">");
        }
        break;
      case '!':
        Advance();
        if (Match('=')) {
          AddToken(TokenKind::NotEq, "!=");
        } else {
          AddToken(TokenKind::Bang, "!");
        }
        break;
      case '"':
        if (!LexString()) return false;
        break;
      case '\'':
        if (!LexChar()) return false;
        break;
      case '@':
        AddSimpleToken(TokenKind::At);
        break;
      default:
        SetErrorHere("unexpected character '" + std::string(1, c) + "'");
        return false;
    }
  }

  Token end;
  end.kind = TokenKind::End;
  end.text = "";
  end.line = line_;
  end.column = column_;
  tokens_.push_back(end);
  return true;
}

char Lexer::Peek(size_t offset) const {
  if (index_ + offset >= source_.size()) return '\0';
  return source_[index_ + offset];
}

char Lexer::Advance() {
  if (IsAtEnd()) return '\0';
  char c = source_[index_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

bool Lexer::Match(char expected) {
  if (IsAtEnd()) return false;
  if (source_[index_] != expected) return false;
  Advance();
  return true;
}

bool Lexer::IsAtEnd() const {
  return index_ >= source_.size();
}

void Lexer::SkipWhitespaceAndComments() {
  for (;;) {
    char c = Peek();
    if (std::isspace(static_cast<unsigned char>(c))) {
      Advance();
      continue;
    }
    if (c == '/' && Peek(1) == '/') {
      while (!IsAtEnd() && Peek() != '\n') {
        Advance();
      }
      continue;
    }
    if (c == '/' && Peek(1) == '*') {
      Advance();
      Advance();
      while (!IsAtEnd()) {
        if (Peek() == '*' && Peek(1) == '/') {
          Advance();
          Advance();
          break;
        }
        Advance();
      }
      continue;
    }
    break;
  }
}

void Lexer::AddToken(TokenKind kind, const std::string& text) {
  Token tok;
  tok.kind = kind;
  tok.text = text;
  tok.line = line_;
  tok.column = column_ - static_cast<uint32_t>(text.size());
  tokens_.push_back(tok);
}

void Lexer::AddSimpleToken(TokenKind kind) {
  char c = Advance();
  AddToken(kind, std::string(1, c));
}

void Lexer::SetErrorHere(const std::string& msg) {
  SetErrorAt(msg, line_, column_ > 0 ? column_ : 1u);
}

void Lexer::SetErrorAt(const std::string& msg, uint32_t line, uint32_t col) {
  error_ = std::to_string(line) + ":" + std::to_string(col) + ": " + msg;
}

bool Lexer::LexIdentifierOrKeyword() {
  size_t start = index_;
  while (IsIdentPart(Peek())) {
    Advance();
  }
  std::string text = source_.substr(start, index_ - start);
  auto it = kKeywords.find(text);
  if (it != kKeywords.end()) {
    AddToken(it->second, text);
  } else {
    AddToken(TokenKind::Identifier, text);
  }
  return true;
}

bool Lexer::LexNumber() {
  size_t start = index_;
  bool is_float = false;

  if (Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X')) {
    Advance();
    Advance();
    if (!std::isxdigit(static_cast<unsigned char>(Peek()))) {
      SetErrorHere("invalid hex literal");
      return false;
    }
    while (std::isxdigit(static_cast<unsigned char>(Peek()))) {
      Advance();
    }
    std::string text = source_.substr(start, index_ - start);
    AddToken(TokenKind::Integer, text);
    return true;
  }

  if (Peek() == '0' && (Peek(1) == 'b' || Peek(1) == 'B')) {
    Advance();
    Advance();
    if (Peek() != '0' && Peek() != '1') {
      SetErrorHere("invalid binary literal");
      return false;
    }
    while (Peek() == '0' || Peek() == '1') {
      Advance();
    }
    std::string text = source_.substr(start, index_ - start);
    AddToken(TokenKind::Integer, text);
    return true;
  }

  while (std::isdigit(static_cast<unsigned char>(Peek()))) {
    Advance();
  }
  if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(Peek(1)))) {
    is_float = true;
    Advance();
    while (std::isdigit(static_cast<unsigned char>(Peek()))) {
      Advance();
    }
  }
  if (Peek() == 'e' || Peek() == 'E') {
    is_float = true;
    Advance();
    if (Peek() == '+' || Peek() == '-') {
      Advance();
    }
    if (!std::isdigit(static_cast<unsigned char>(Peek()))) {
      SetErrorHere("invalid float literal");
      return false;
    }
    while (std::isdigit(static_cast<unsigned char>(Peek()))) {
      Advance();
    }
  }

  std::string text = source_.substr(start, index_ - start);
  AddToken(is_float ? TokenKind::Float : TokenKind::Integer, text);
  return true;
}

bool Lexer::LexString() {
  size_t start_line = line_;
  size_t start_col = column_;
  Advance();
  std::string value;
  while (!IsAtEnd()) {
    char c = Advance();
    if (c == '"') {
      AddToken(TokenKind::String, value);
      return true;
    }
    if (c == '\\') {
      char esc = Advance();
      switch (esc) {
        case 'n': value.push_back('\n'); break;
        case 't': value.push_back('\t'); break;
        case 'r': value.push_back('\r'); break;
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case 'x': {
          auto HexDigit = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
            if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
            return -1;
          };
          if (IsAtEnd()) {
            SetErrorAt("invalid hex escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
            return false;
          }
          char h1 = Advance();
          if (IsAtEnd()) {
            SetErrorAt("invalid hex escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
            return false;
          }
          char h2 = Advance();
          int v1 = HexDigit(h1);
          int v2 = HexDigit(h2);
          if (v1 < 0 || v2 < 0) {
            SetErrorAt("invalid hex escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
            return false;
          }
          value.push_back(static_cast<char>((v1 << 4) | v2));
          break;
        }
        default:
          SetErrorAt("invalid string escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
          return false;
      }
    } else {
      value.push_back(c);
    }
  }
  SetErrorHere("unterminated string literal");
  return false;
}

bool Lexer::LexChar() {
  size_t start_line = line_;
  size_t start_col = column_;
  Advance();
  char value = '\0';
  if (IsAtEnd()) {
    SetErrorHere("unterminated char literal");
    return false;
  }
  char c = Advance();
  if (c == '\\') {
    char esc = Advance();
    switch (esc) {
      case 'n': value = '\n'; break;
      case 't': value = '\t'; break;
      case 'r': value = '\r'; break;
      case '\'': value = '\''; break;
      case '\\': value = '\\'; break;
      case 'x': {
        auto HexDigit = [](char ch) -> int {
          if (ch >= '0' && ch <= '9') return ch - '0';
          if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
          if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
          return -1;
        };
        if (IsAtEnd()) {
          SetErrorAt("invalid hex escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
          return false;
        }
        char h1 = Advance();
        if (IsAtEnd()) {
          SetErrorAt("invalid hex escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
          return false;
        }
        char h2 = Advance();
        int v1 = HexDigit(h1);
        int v2 = HexDigit(h2);
        if (v1 < 0 || v2 < 0) {
          SetErrorAt("invalid hex escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
          return false;
        }
        value = static_cast<char>((v1 << 4) | v2);
        break;
      }
      default:
        SetErrorAt("invalid char escape", static_cast<uint32_t>(start_line), static_cast<uint32_t>(start_col));
        return false;
    }
  } else {
    value = c;
  }
  if (Peek() != '\'') {
    SetErrorHere("unterminated char literal");
    return false;
  }
  Advance();
  AddToken(TokenKind::Char, std::string(1, value));
  return true;
}

} // namespace Simple::Lang
