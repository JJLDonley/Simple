#include "lsp_server.h"

#include <cctype>
#include <cstring>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

#include "lang_lexer.h"
#include "lang_token.h"
#include "lang_validate.h"

namespace Simple::LSP {
namespace {

std::string TrimCopy(const std::string& input) {
  size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) ++start;
  size_t end = input.size();
  while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
  return input.substr(start, end - start);
}

bool StartsWithCaseInsensitive(const std::string& text, const std::string& prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(text[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool ExtractJsonStringField(const std::string& json, const std::string& field, std::string* out) {
  if (!out) return false;
  const std::string key = "\"" + field + "\"";
  const size_t key_pos = json.find(key);
  if (key_pos == std::string::npos) return false;
  const size_t colon = json.find(':', key_pos + key.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  if (i >= json.size() || json[i] != '"') return false;
  ++i;
  std::string value;
  while (i < json.size()) {
    char c = json[i++];
    if (c == '\\') {
      if (i >= json.size()) return false;
      char esc = json[i++];
      switch (esc) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u': {
          // Minimal JSON \uXXXX handling for ASCII subset.
          if (i + 4 > json.size()) return false;
          uint32_t code = 0;
          for (int k = 0; k < 4; ++k) {
            char h = json[i++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<uint32_t>(10 + (h - 'a'));
            else if (h >= 'A' && h <= 'F') code |= static_cast<uint32_t>(10 + (h - 'A'));
            else return false;
          }
          if (code <= 0x7F) {
            value.push_back(static_cast<char>(code));
          } else {
            value.push_back('?');
          }
          break;
        }
        default:
          value.push_back(esc);
          break;
      }
      continue;
    }
    if (c == '"') {
      *out = std::move(value);
      return true;
    }
    value.push_back(c);
  }
  return false;
}

bool ExtractJsonUintField(const std::string& json, const std::string& field, uint32_t* out) {
  if (!out) return false;
  const std::string key = "\"" + field + "\"";
  const size_t key_pos = json.find(key);
  if (key_pos == std::string::npos) return false;
  const size_t colon = json.find(':', key_pos + key.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  if (i >= json.size() || !std::isdigit(static_cast<unsigned char>(json[i]))) return false;
  size_t end = i;
  while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) ++end;
  try {
    *out = static_cast<uint32_t>(std::stoul(json.substr(i, end - i)));
  } catch (...) {
    return false;
  }
  return true;
}

bool ExtractJsonIdRaw(const std::string& json, std::string* out_raw) {
  if (!out_raw) return false;
  const std::string key = "\"id\"";
  const size_t key_pos = json.find(key);
  if (key_pos == std::string::npos) return false;
  const size_t colon = json.find(':', key_pos + key.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  if (i >= json.size()) return false;
  size_t end = i;
  if (json[i] == '"') {
    ++end;
    while (end < json.size()) {
      if (json[end] == '\\') {
        end += 2;
        continue;
      }
      if (json[end] == '"') {
        ++end;
        break;
      }
      ++end;
    }
    if (end > json.size()) return false;
  } else {
    while (end < json.size() && json[end] != ',' && json[end] != '}' &&
           !std::isspace(static_cast<unsigned char>(json[end]))) {
      ++end;
    }
  }
  *out_raw = TrimCopy(json.substr(i, end - i));
  return !out_raw->empty();
}

void WriteLspMessage(std::ostream& out, const std::string& payload) {
  out << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
  out.flush();
}

bool ParseLineAndColumnPrefix(const std::string& message,
                              uint32_t* out_line,
                              uint32_t* out_col,
                              std::string* out_message) {
  if (!out_line || !out_col || !out_message) return false;
  *out_line = 0;
  *out_col = 0;
  *out_message = message;
  size_t first = message.find(':');
  if (first == std::string::npos || first == 0) return false;
  size_t second = message.find(':', first + 1);
  if (second == std::string::npos || second <= first + 1) return false;
  for (size_t i = 0; i < first; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(message[i]))) return false;
  }
  for (size_t i = first + 1; i < second; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(message[i]))) return false;
  }
  try {
    *out_line = static_cast<uint32_t>(std::stoul(message.substr(0, first)));
    *out_col = static_cast<uint32_t>(std::stoul(message.substr(first + 1, second - first - 1)));
  } catch (...) {
    return false;
  }
  *out_message = TrimCopy(message.substr(second + 1));
  return true;
}

std::string JsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

void PublishDiagnostics(std::ostream& out,
                        const std::string& uri,
                        const std::string& source_text) {
  std::string error;
  const bool ok = Simple::Lang::ValidateProgramFromString(source_text, &error);
  if (ok) {
    WriteLspMessage(
        out,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
        "\"params\":{\"uri\":\"" + JsonEscape(uri) + "\",\"diagnostics\":[]}}");
    return;
  }

  uint32_t line = 1;
  uint32_t col = 1;
  std::string msg = error;
  uint32_t parsed_line = 0;
  uint32_t parsed_col = 0;
  std::string parsed_msg;
  if (ParseLineAndColumnPrefix(error, &parsed_line, &parsed_col, &parsed_msg)) {
    line = parsed_line == 0 ? 1 : parsed_line;
    col = parsed_col == 0 ? 1 : parsed_col;
    msg = parsed_msg;
  }
  const uint32_t start_line = line > 0 ? (line - 1) : 0;
  const uint32_t start_char = col > 0 ? (col - 1) : 0;
  const uint32_t end_char = start_char + 1;
  WriteLspMessage(
      out,
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
      "\"params\":{\"uri\":\"" + JsonEscape(uri) + "\",\"diagnostics\":[{"
      "\"range\":{\"start\":{\"line\":" + std::to_string(start_line) +
      ",\"character\":" + std::to_string(start_char) + "},"
      "\"end\":{\"line\":" + std::to_string(start_line) +
      ",\"character\":" + std::to_string(end_char) + "}},"
      "\"severity\":1,\"code\":\"E0001\","
      "\"source\":\"simple-lsp\","
      "\"message\":\"" + JsonEscape(msg) + "\"}]}}");
}

std::string GetLineText(const std::string& text, uint32_t line_index) {
  uint32_t current = 0;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (current == line_index) {
        return text.substr(start, i - start);
      }
      ++current;
      start = i + 1;
    }
  }
  return {};
}

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string IdentifierAtPosition(const std::string& text, uint32_t line, uint32_t character) {
  const std::string line_text = GetLineText(text, line);
  if (line_text.empty()) return {};
  size_t pos = std::min<size_t>(character, line_text.size() ? line_text.size() - 1 : 0);
  if (!IsIdentChar(line_text[pos])) {
    if (pos > 0 && IsIdentChar(line_text[pos - 1])) {
      --pos;
    } else {
      return {};
    }
  }
  size_t begin = pos;
  while (begin > 0 && IsIdentChar(line_text[begin - 1])) --begin;
  size_t end = pos + 1;
  while (end < line_text.size() && IsIdentChar(line_text[end])) ++end;
  return line_text.substr(begin, end - begin);
}

void ReplyHover(std::ostream& out,
                const std::string& id_raw,
                const std::string& uri,
                uint32_t line,
                uint32_t character,
                const std::unordered_map<std::string, std::string>& open_docs) {
  auto it = open_docs.find(uri);
  if (it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  const std::string ident = IdentifierAtPosition(it->second, line, character);
  if (ident.empty()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  WriteLspMessage(
      out,
      "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" +
          JsonEscape(ident) + "`\"}}}");
}

void ReplyCompletion(std::ostream& out, const std::string& id_raw) {
  static const std::vector<std::string> kKeywords = {
      "fn", "import", "extern", "if", "else", "while", "for", "return", "break", "skip"};
  std::string items;
  for (size_t i = 0; i < kKeywords.size(); ++i) {
    if (i) items += ",";
    items += "{\"label\":\"" + kKeywords[i] + "\",\"kind\":14}";
  }
  items += ",{\"label\":\"IO.println\",\"kind\":3}";
  items += ",{\"label\":\"IO.print\",\"kind\":3}";
  WriteLspMessage(
      out,
      "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"result\":{\"isIncomplete\":false,\"items\":[" + items + "]}}");
}

bool IsKeywordToken(Simple::Lang::TokenKind kind) {
  using TK = Simple::Lang::TokenKind;
  switch (kind) {
    case TK::KwWhile:
    case TK::KwFor:
    case TK::KwBreak:
    case TK::KwSkip:
    case TK::KwReturn:
    case TK::KwIf:
    case TK::KwElse:
    case TK::KwDefault:
    case TK::KwFn:
    case TK::KwSelf:
    case TK::KwArtifact:
    case TK::KwEnum:
    case TK::KwModule:
    case TK::KwImport:
    case TK::KwExtern:
    case TK::KwAs:
    case TK::KwTrue:
    case TK::KwFalse:
      return true;
    default:
      return false;
  }
}

bool IsOperatorToken(Simple::Lang::TokenKind kind) {
  using TK = Simple::Lang::TokenKind;
  switch (kind) {
    case TK::Colon:
    case TK::DoubleColon:
    case TK::Assign:
    case TK::Plus:
    case TK::Minus:
    case TK::Star:
    case TK::Slash:
    case TK::Percent:
    case TK::PlusPlus:
    case TK::MinusMinus:
    case TK::Amp:
    case TK::Pipe:
    case TK::Caret:
    case TK::Shl:
    case TK::Shr:
    case TK::EqEq:
    case TK::NotEq:
    case TK::Lt:
    case TK::Le:
    case TK::Gt:
    case TK::Ge:
    case TK::AndAnd:
    case TK::OrOr:
    case TK::Bang:
    case TK::PlusEq:
    case TK::MinusEq:
    case TK::StarEq:
    case TK::SlashEq:
    case TK::PercentEq:
    case TK::AmpEq:
    case TK::PipeEq:
    case TK::CaretEq:
    case TK::ShlEq:
    case TK::ShrEq:
    case TK::PipeGt:
    case TK::At:
      return true;
    default:
      return false;
  }
}

bool IsPrimitiveTypeName(const std::string& name) {
  static const std::unordered_map<std::string, int> kTypeNames = {
      {"i8", 1},   {"i16", 1},  {"i32", 1},  {"i64", 1},  {"i128", 1},
      {"u8", 1},   {"u16", 1},  {"u32", 1},  {"u64", 1},  {"u128", 1},
      {"f32", 1},  {"f64", 1},  {"bool", 1}, {"char", 1}, {"string", 1},
      {"void", 1},
  };
  return kTypeNames.find(name) != kTypeNames.end();
}

uint32_t SemanticTokenTypeIndex(const Simple::Lang::Token& token) {
  using TK = Simple::Lang::TokenKind;
  if (IsKeywordToken(token.kind)) return 0;      // keyword
  if (token.kind == TK::String || token.kind == TK::Char) return 8;  // string
  if (token.kind == TK::Integer || token.kind == TK::Float) return 9;  // number
  if (IsOperatorToken(token.kind)) return 10;    // operator
  if (token.kind == TK::Identifier && IsPrimitiveTypeName(token.text)) return 1;  // type
  return 3;                                      // variable
}

void ReplySemanticTokensFull(std::ostream& out,
                             const std::string& id_raw,
                             const std::string& uri,
                             const std::unordered_map<std::string, std::string>& open_docs) {
  auto it = open_docs.find(uri);
  if (it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":{\"data\":[]}}");
    return;
  }
  Simple::Lang::Lexer lexer(it->second);
  if (!lexer.Lex()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":{\"data\":[]}}");
    return;
  }
  const auto& tokens = lexer.Tokens();
  std::string data;
  uint32_t prev_line = 0;
  uint32_t prev_col = 0;
  bool first = true;
  for (const auto& token : tokens) {
    if (token.kind == Simple::Lang::TokenKind::End ||
        token.kind == Simple::Lang::TokenKind::Invalid) {
      continue;
    }
    const uint32_t line = token.line > 0 ? (token.line - 1) : 0;
    const uint32_t col = token.column > 0 ? (token.column - 1) : 0;
    const uint32_t len = static_cast<uint32_t>(token.text.size() > 0 ? token.text.size() : 1);
    const uint32_t token_type = SemanticTokenTypeIndex(token);
    const uint32_t modifiers = 0;
    const uint32_t delta_line = first ? line : (line - prev_line);
    const uint32_t delta_start = first ? col : (line == prev_line ? (col - prev_col) : col);
    if (!data.empty()) data += ",";
    data += std::to_string(delta_line) + "," + std::to_string(delta_start) + "," +
            std::to_string(len) + "," + std::to_string(token_type) + "," +
            std::to_string(modifiers);
    prev_line = line;
    prev_col = col;
    first = false;
  }
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
                           ",\"result\":{\"data\":[" + data + "]}}");
}

struct TokenRef {
  size_t index = 0;
  Simple::Lang::Token token;
  uint32_t depth = 0;
};

std::vector<TokenRef> LexTokenRefs(const std::string& text) {
  std::vector<TokenRef> out;
  Simple::Lang::Lexer lexer(text);
  if (!lexer.Lex()) return out;
  const auto& tokens = lexer.Tokens();
  uint32_t depth = 0;
  out.reserve(tokens.size());
  for (size_t i = 0; i < tokens.size(); ++i) {
    const auto& tk = tokens[i];
    TokenRef ref;
    ref.index = i;
    ref.token = tk;
    ref.depth = depth;
    out.push_back(ref);
    if (tk.kind == Simple::Lang::TokenKind::LBrace) ++depth;
    if (tk.kind == Simple::Lang::TokenKind::RBrace && depth > 0) --depth;
  }
  return out;
}

bool TokenContainsPosition(const Simple::Lang::Token& tk, uint32_t line, uint32_t character) {
  const uint32_t tk_line = tk.line > 0 ? (tk.line - 1) : 0;
  if (tk_line != line) return false;
  const uint32_t start = tk.column > 0 ? (tk.column - 1) : 0;
  const uint32_t len = static_cast<uint32_t>(tk.text.empty() ? 1 : tk.text.size());
  const uint32_t end = start + len;
  return character >= start && character < end;
}

const TokenRef* FindIdentifierAt(const std::vector<TokenRef>& refs, uint32_t line, uint32_t character) {
  for (const auto& ref : refs) {
    if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
    if (TokenContainsPosition(ref.token, line, character)) return &ref;
  }
  return nullptr;
}

bool IsDeclNameAt(const std::vector<TokenRef>& refs, size_t i) {
  using TK = Simple::Lang::TokenKind;
  if (i >= refs.size()) return false;
  if (refs[i].token.kind != TK::Identifier) return false;
  if (i > 0 && refs[i - 1].token.kind == TK::KwFn) return true;
  if (i + 2 < refs.size() &&
      refs[i + 1].token.kind == TK::DoubleColon &&
      (refs[i + 2].token.kind == TK::KwArtifact ||
       refs[i + 2].token.kind == TK::KwModule ||
       refs[i + 2].token.kind == TK::KwEnum)) {
    return true;
  }
  if (i + 1 < refs.size() &&
      (refs[i + 1].token.kind == TK::Colon || refs[i + 1].token.kind == TK::DoubleColon)) {
    return true;
  }
  return false;
}

std::string LocationJson(const std::string& uri, const Simple::Lang::Token& tk) {
  const uint32_t line = tk.line > 0 ? (tk.line - 1) : 0;
  const uint32_t col = tk.column > 0 ? (tk.column - 1) : 0;
  const uint32_t len = static_cast<uint32_t>(tk.text.empty() ? 1 : tk.text.size());
  return "{\"uri\":\"" + JsonEscape(uri) + "\",\"range\":{\"start\":{\"line\":" +
         std::to_string(line) + ",\"character\":" + std::to_string(col) +
         "},\"end\":{\"line\":" + std::to_string(line) + ",\"character\":" +
         std::to_string(col + len) + "}}}";
}

void ReplyDefinition(std::ostream& out,
                     const std::string& id_raw,
                     const std::string& uri,
                     uint32_t line,
                     uint32_t character,
                     const std::unordered_map<std::string, std::string>& open_docs) {
  auto doc_it = open_docs.find(uri);
  if (doc_it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  const auto refs = LexTokenRefs(doc_it->second);
  const TokenRef* target = FindIdentifierAt(refs, line, character);
  if (!target) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  const std::string name = target->token.text;
  const TokenRef* best = nullptr;
  for (const auto& ref : refs) {
    if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
    if (ref.token.text != name) continue;
    if (!IsDeclNameAt(refs, ref.index)) continue;
    if (!best || ref.index < best->index) best = &ref;
  }
  if (!best) best = target;
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[" +
                           LocationJson(uri, best->token) + "]}");
}

void ReplyReferences(std::ostream& out,
                     const std::string& id_raw,
                     const std::string& uri,
                     uint32_t line,
                     uint32_t character,
                     const std::unordered_map<std::string, std::string>& open_docs) {
  auto doc_it = open_docs.find(uri);
  if (doc_it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  const auto refs = LexTokenRefs(doc_it->second);
  const TokenRef* target = FindIdentifierAt(refs, line, character);
  if (!target) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  const std::string name = target->token.text;
  std::string result;
  for (const auto& ref : refs) {
    if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
    if (ref.token.text != name) continue;
    if (!result.empty()) result += ",";
    result += LocationJson(uri, ref.token);
  }
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[" + result + "]}");
}

uint32_t SymbolKindFor(const std::vector<TokenRef>& refs, size_t i) {
  using TK = Simple::Lang::TokenKind;
  if (i > 0 && refs[i - 1].token.kind == TK::KwFn) return 12;
  if (i + 2 < refs.size() && refs[i + 1].token.kind == TK::DoubleColon) {
    if (refs[i + 2].token.kind == TK::KwModule) return 2;
    if (refs[i + 2].token.kind == TK::KwEnum) return 10;
    if (refs[i + 2].token.kind == TK::KwArtifact) return 23;
  }
  return 13;
}

void ReplyDocumentSymbols(std::ostream& out,
                          const std::string& id_raw,
                          const std::string& uri,
                          const std::unordered_map<std::string, std::string>& open_docs) {
  auto doc_it = open_docs.find(uri);
  if (doc_it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  const auto refs = LexTokenRefs(doc_it->second);
  std::string result;
  for (const auto& ref : refs) {
    if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
    if (ref.depth != 0) continue;
    if (!IsDeclNameAt(refs, ref.index)) continue;
    const uint32_t line = ref.token.line > 0 ? (ref.token.line - 1) : 0;
    const uint32_t col = ref.token.column > 0 ? (ref.token.column - 1) : 0;
    const uint32_t len = static_cast<uint32_t>(ref.token.text.empty() ? 1 : ref.token.text.size());
    const uint32_t kind = SymbolKindFor(refs, ref.index);
    if (!result.empty()) result += ",";
    result += "{\"name\":\"" + JsonEscape(ref.token.text) + "\",\"kind\":" + std::to_string(kind) +
              ",\"range\":{\"start\":{\"line\":" + std::to_string(line) +
              ",\"character\":" + std::to_string(col) + "},\"end\":{\"line\":" +
              std::to_string(line) + ",\"character\":" + std::to_string(col + len) +
              "}},\"selectionRange\":{\"start\":{\"line\":" + std::to_string(line) +
              ",\"character\":" + std::to_string(col) + "},\"end\":{\"line\":" +
              std::to_string(line) + ",\"character\":" + std::to_string(col + len) + "}}}";
  }
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[" + result + "]}");
}

} // namespace

int RunServer(std::istream& in, std::ostream& out) {
  bool saw_shutdown = false;
  std::unordered_map<std::string, std::string> open_docs;
  for (;;) {
    std::string line;
    int content_length = -1;
    bool saw_any_header = false;
    for (;;) {
      if (!std::getline(in, line)) {
        return 0;
      }
      saw_any_header = true;
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) break;
      if (StartsWithCaseInsensitive(line, "Content-Length:")) {
        const std::string value = TrimCopy(line.substr(std::strlen("Content-Length:")));
        try {
          content_length = std::stoi(value);
        } catch (...) {
          return 1;
        }
      }
    }
    if (!saw_any_header || content_length < 0) return 1;
    std::string body(static_cast<size_t>(content_length), '\0');
    in.read(&body[0], content_length);
    if (in.gcount() != content_length) return 1;

    std::string method;
    ExtractJsonStringField(body, "method", &method);
    std::string id_raw;
    const bool has_id = ExtractJsonIdRaw(body, &id_raw);

    if (method == "initialize") {
      if (has_id) {
        WriteLspMessage(
            out,
            "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
                ",\"result\":{\"capabilities\":{\"textDocumentSync\":2,"
                "\"hoverProvider\":true,\"definitionProvider\":true,"
                "\"referencesProvider\":true,\"documentSymbolProvider\":true,"
                "\"completionProvider\":{\"triggerCharacters\":[\".\",\":\"]},"
                "\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":["
                "\"keyword\",\"type\",\"function\",\"variable\",\"parameter\","
                "\"property\",\"enumMember\",\"namespace\",\"string\",\"number\","
                "\"operator\"],\"tokenModifiers\":[\"declaration\",\"readonly\","
                "\"defaultLibrary\"]},\"full\":true}}}}");
      }
      continue;
    }

    if (method == "shutdown") {
      saw_shutdown = true;
      if (has_id) {
        WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
      }
      continue;
    }

    if (method == "exit") {
      return saw_shutdown ? 0 : 1;
    }

    if (method == "initialized" ||
        method == "$/cancelRequest") {
      continue;
    }

    if (method == "textDocument/didOpen") {
      std::string uri;
      std::string text;
      if (ExtractJsonStringField(body, "uri", &uri) &&
          ExtractJsonStringField(body, "text", &text)) {
        open_docs[uri] = text;
        PublishDiagnostics(out, uri, text);
      }
      continue;
    }

    if (method == "textDocument/didChange") {
      std::string uri;
      std::string text;
      if (ExtractJsonStringField(body, "uri", &uri) &&
          ExtractJsonStringField(body, "text", &text)) {
        open_docs[uri] = text;
        PublishDiagnostics(out, uri, text);
      }
      continue;
    }

    if (method == "textDocument/didClose") {
      std::string uri;
      if (ExtractJsonStringField(body, "uri", &uri)) {
        open_docs.erase(uri);
        WriteLspMessage(
            out,
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
            "\"params\":{\"uri\":\"" + JsonEscape(uri) + "\",\"diagnostics\":[]}}");
      }
      continue;
    }

    if (method == "textDocument/hover") {
      if (has_id) {
        std::string uri;
        uint32_t line = 0;
        uint32_t character = 0;
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplyHover(out, id_raw, uri, line, character, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
        }
      }
      continue;
    }

    if (method == "textDocument/completion") {
      if (has_id) ReplyCompletion(out, id_raw);
      continue;
    }

    if (method == "textDocument/definition") {
      if (has_id) {
        std::string uri;
        uint32_t line = 0;
        uint32_t character = 0;
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplyDefinition(out, id_raw, uri, line, character, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
        }
      }
      continue;
    }

    if (method == "textDocument/references") {
      if (has_id) {
        std::string uri;
        uint32_t line = 0;
        uint32_t character = 0;
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplyReferences(out, id_raw, uri, line, character, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
        }
      }
      continue;
    }

    if (method == "textDocument/documentSymbol") {
      if (has_id) {
        std::string uri;
        if (ExtractJsonStringField(body, "uri", &uri)) {
          ReplyDocumentSymbols(out, id_raw, uri, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
        }
      }
      continue;
    }

    if (method == "textDocument/semanticTokens/full") {
      if (has_id) {
        std::string uri;
        if (ExtractJsonStringField(body, "uri", &uri)) {
          ReplySemanticTokensFull(out, id_raw, uri, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":{\"data\":[]}}");
        }
      }
      continue;
    }

    if (has_id) {
      WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
                               ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    }
  }
}

} // namespace Simple::LSP
