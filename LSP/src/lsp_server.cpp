#include "lsp_server.h"

#include <cctype>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

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

std::vector<std::string> SortedOpenDocUris(
    const std::unordered_map<std::string, std::string>& open_docs,
    const std::string& exclude_uri = {}) {
  std::vector<std::string> uris;
  uris.reserve(open_docs.size());
  for (const auto& [uri, _] : open_docs) {
    if (!exclude_uri.empty() && uri == exclude_uri) continue;
    uris.push_back(uri);
  }
  std::sort(uris.begin(), uris.end());
  return uris;
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

bool ExtractJsonBoolField(const std::string& json, const std::string& field, bool* out) {
  if (!out) return false;
  const std::string key = "\"" + field + "\"";
  const size_t key_pos = json.find(key);
  if (key_pos == std::string::npos) return false;
  const size_t colon = json.find(':', key_pos + key.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  if (i + 4 <= json.size() && json.compare(i, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (i + 5 <= json.size() && json.compare(i, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool CodeActionContextAllowsQuickFix(const std::string& json) {
  const size_t only_key = json.find("\"only\"");
  if (only_key == std::string::npos) return true;
  const size_t lbracket = json.find('[', only_key);
  if (lbracket == std::string::npos) return true;
  const size_t rbracket = json.find(']', lbracket + 1);
  if (rbracket == std::string::npos) return true;
  const std::string only_body = json.substr(lbracket + 1, rbracket - (lbracket + 1));
  return only_body.find("\"quickfix\"") != std::string::npos;
}

bool CodeActionContextMentionsCode(const std::string& json, const std::string& code) {
  const size_t ctx_key = json.find("\"context\"");
  if (ctx_key == std::string::npos) return true;
  const size_t diag_key = json.find("\"diagnostics\"", ctx_key);
  if (diag_key == std::string::npos) return true;
  const size_t code_key = json.find("\"code\"", diag_key);
  if (code_key == std::string::npos) return true;
  const std::string needle = "\"" + code + "\"";
  return json.find(needle, code_key) != std::string::npos;
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

bool ExtractJsonIdRawFromOffset(const std::string& json, size_t start_offset, std::string* out_raw) {
  if (!out_raw) return false;
  if (start_offset >= json.size()) return false;
  const std::string key = "\"id\"";
  const size_t key_pos = json.find(key, start_offset);
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
  uint32_t span_len = 1;
  {
    static const std::string marker = "undeclared identifier:";
    const size_t marker_pos = msg.find(marker);
    if (marker_pos != std::string::npos) {
      std::string ident = TrimCopy(msg.substr(marker_pos + marker.size()));
      size_t ident_len = 0;
      while (ident_len < ident.size()) {
        const char c = ident[ident_len];
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        if (!ok) break;
        ++ident_len;
      }
      if (ident_len > 0) {
        span_len = static_cast<uint32_t>(ident_len);
      }
    }
  }
  const uint32_t end_char = start_char + span_len;
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

std::string TrimLeftAscii(const std::string& text) {
  size_t i = 0;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
  return text.substr(i);
}

bool StartsWithImportLine(const std::string& line_text) {
  const std::string trimmed = TrimLeftAscii(line_text);
  return trimmed.rfind("import", 0) == 0 &&
         (trimmed.size() == 6 || std::isspace(static_cast<unsigned char>(trimmed[6])) ||
          trimmed[6] == '"');
}

uint32_t PreferredDeclarationInsertLine(const std::string& text) {
  uint32_t line_index = 0;
  size_t start = 0;
  bool seen_nonempty = false;
  bool in_import_header = true;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') continue;
    const std::string line = text.substr(start, i - start);
    const std::string trimmed = TrimLeftAscii(line);
    if (!trimmed.empty()) seen_nonempty = true;
    if (in_import_header) {
      if (!seen_nonempty || StartsWithImportLine(line)) {
        ++line_index;
        start = i + 1;
        continue;
      }
      return line_index;
    }
    start = i + 1;
  }
  return line_index;
}

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsCallNameChar(char c) {
  return IsIdentChar(c) || c == '.' || c == '@';
}

bool IsAtCastCallName(const std::string& name);
bool IsValidIdentifierName(const std::string& name);
bool ImportPrefixAtPosition(const std::string& text,
                            uint32_t line,
                            uint32_t character,
                            std::string* out_prefix);
std::vector<std::string> CollectImportCandidates(
    const std::unordered_map<std::string, std::string>& open_docs);
std::vector<std::string> CollectReservedModuleMemberLabels(const std::string& text);
std::unordered_map<std::string, std::string> CollectImportAliasMap(const std::string& text);
std::string NormalizeCoreDlMember(const std::string& member);
std::string QualifiedMemberAtPosition(const std::string& text, uint32_t line, uint32_t character);
struct TokenRef;

struct ReservedSignature {
  std::vector<std::string> params;
  std::string return_type;
};

bool ResolveReservedModuleSignature(const std::string& call_name,
                                    const std::string& text,
                                    ReservedSignature* out);
bool ResolveImportedModuleAndMember(const std::string& call_name,
                                    const std::string& text,
                                    std::string* out_module,
                                    std::string* out_member);
bool IsProtectedReservedMemberToken(const std::vector<TokenRef>& refs,
                                    size_t index,
                                    const std::string& text);

std::string LowerAscii(const std::string& text) {
  std::string out = text;
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string ExtractWorkspaceSymbolQuery(const std::string& body) {
  const size_t method_pos = body.find("\"method\":\"workspace/symbol\"");
  if (method_pos == std::string::npos) return {};
  const size_t query_key = body.find("\"query\"", method_pos);
  if (query_key == std::string::npos) return {};
  const size_t colon = body.find(':', query_key);
  if (colon == std::string::npos) return {};
  size_t i = colon + 1;
  while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
  if (i >= body.size() || body[i] != '"') return {};
  ++i;
  std::string query;
  while (i < body.size()) {
    char c = body[i++];
    if (c == '\\') {
      if (i >= body.size()) return {};
      const char esc = body[i++];
      switch (esc) {
        case '"': query.push_back('"'); break;
        case '\\': query.push_back('\\'); break;
        case '/': query.push_back('/'); break;
        case 'b': query.push_back('\b'); break;
        case 'f': query.push_back('\f'); break;
        case 'n': query.push_back('\n'); break;
        case 'r': query.push_back('\r'); break;
        case 't': query.push_back('\t'); break;
        default: query.push_back(esc); break;
      }
      continue;
    }
    if (c == '"') return query;
    query.push_back(c);
  }
  return {};
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

struct TokenRef {
  size_t index = 0;
  Simple::Lang::Token token;
  uint32_t depth = 0;
};

std::vector<TokenRef> LexTokenRefs(const std::string& text);
bool IsDeclNameAt(const std::vector<TokenRef>& refs, size_t i);

std::string CallNameAtPosition(const std::string& text, uint32_t line, uint32_t character) {
  const std::string line_text = GetLineText(text, line);
  if (line_text.empty()) return {};
  const size_t cursor = std::min<size_t>(character, line_text.size());
  size_t paren = std::string::npos;
  for (size_t i = cursor; i > 0; --i) {
    const size_t idx = i - 1;
    if (line_text[idx] == '(') {
      paren = idx;
      break;
    }
  }
  if (paren == std::string::npos) return {};
  size_t end = paren;
  while (end > 0 && std::isspace(static_cast<unsigned char>(line_text[end - 1]))) --end;
  if (end == 0) return {};
  size_t begin = end;
  while (begin > 0 && IsCallNameChar(line_text[begin - 1])) --begin;
  if (begin == end) return {};
  return line_text.substr(begin, end - begin);
}

uint32_t ActiveParameterAtPosition(const std::string& text, uint32_t line, uint32_t character) {
  const std::string line_text = GetLineText(text, line);
  if (line_text.empty()) return 0;
  const size_t cursor = std::min<size_t>(character, line_text.size());
  size_t paren = std::string::npos;
  for (size_t i = cursor; i > 0; --i) {
    const size_t idx = i - 1;
    if (line_text[idx] == '(') {
      paren = idx;
      break;
    }
  }
  if (paren == std::string::npos || paren + 1 >= cursor) return 0;
  uint32_t commas = 0;
  for (size_t i = paren + 1; i < cursor; ++i) {
    if (line_text[i] == ',') ++commas;
  }
  return commas;
}

std::string CompletionPrefixAtPosition(const std::string& text, uint32_t line, uint32_t character) {
  const std::string line_text = GetLineText(text, line);
  if (line_text.empty()) return {};
  size_t end = std::min<size_t>(character, line_text.size());
  size_t begin = end;
  while (begin > 0 && IsIdentChar(line_text[begin - 1])) --begin;
  if (begin == end) return {};
  return line_text.substr(begin, end - begin);
}

std::string CompletionMemberReceiverAtPosition(const std::string& text,
                                               uint32_t line,
                                               uint32_t character) {
  const std::string line_text = GetLineText(text, line);
  if (line_text.empty()) return {};
  size_t end = std::min<size_t>(character, line_text.size());
  size_t begin = end;
  while (begin > 0 && IsIdentChar(line_text[begin - 1])) --begin;
  if (begin == 0 || line_text[begin - 1] != '.') return {};
  size_t recv_end = begin - 1;
  size_t recv_begin = recv_end;
  while (recv_begin > 0 && IsIdentChar(line_text[recv_begin - 1])) --recv_begin;
  if (recv_begin == recv_end) return {};
  return line_text.substr(recv_begin, recv_end - recv_begin);
}

bool ImportPrefixAtPosition(const std::string& text,
                            uint32_t line,
                            uint32_t character,
                            std::string* out_prefix) {
  if (!out_prefix) return false;
  const std::string line_text = GetLineText(text, line);
  if (line_text.empty()) return false;
  const size_t cursor = std::min<size_t>(character, line_text.size());
  if (cursor == 0) return false;
  const size_t quote = line_text.rfind('"', cursor - 1);
  if (quote == std::string::npos) return false;
  const size_t close_quote = line_text.find('"', quote + 1);
  if (close_quote != std::string::npos && close_quote < cursor) return false;

  size_t token_end = quote;
  while (token_end > 0 && std::isspace(static_cast<unsigned char>(line_text[token_end - 1]))) --token_end;
  size_t token_begin = token_end;
  while (token_begin > 0 && IsIdentChar(line_text[token_begin - 1])) --token_begin;
  if (token_end <= token_begin || line_text.substr(token_begin, token_end - token_begin) != "import") {
    return false;
  }
  *out_prefix = line_text.substr(quote + 1, cursor - (quote + 1));
  return true;
}

std::vector<std::string> CollectImportCandidates(
    const std::unordered_map<std::string, std::string>& open_docs) {
  static const std::vector<std::string> kReservedImports = {
      "IO", "Math", "Time", "File", "Core.DL", "Core.Os", "Core.Fs", "Core.Log"};
  std::vector<std::string> labels = kReservedImports;
  std::unordered_set<std::string> seen(labels.begin(), labels.end());
  for (const auto& [uri, _] : open_docs) {
    constexpr const char* kSuffix = ".simple";
    if (uri.size() <= std::strlen(kSuffix) ||
        uri.compare(uri.size() - std::strlen(kSuffix), std::strlen(kSuffix), kSuffix) != 0) {
      continue;
    }
    const size_t slash = uri.find_last_of('/');
    const size_t base = (slash == std::string::npos) ? 0 : slash + 1;
    const size_t stem_end = uri.size() - std::strlen(kSuffix);
    if (base >= stem_end) continue;
    const std::string stem = uri.substr(base, stem_end - base);
    if (seen.insert(stem).second) labels.push_back(stem);
  }
  std::sort(labels.begin(), labels.end());
  return labels;
}

std::string DefaultImportAlias(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  size_t end = path.size();
  if (dot != std::string::npos && dot > start && path.compare(dot, 7, ".simple") == 0) {
    end = dot;
  }
  std::string base = path.substr(start, end - start);
  const size_t module_dot = base.find_last_of('.');
  if (module_dot != std::string::npos && module_dot + 1 < base.size()) {
    base = base.substr(module_dot + 1);
  }
  return base;
}

std::vector<std::string> CollectReservedModuleMemberLabels(const std::string& text) {
  static const std::unordered_map<std::string, std::vector<std::string>> kModuleMembers = {
      {"IO", {"print", "println"}},
      {"Math", {"abs", "min", "max", "pi"}},
      {"Time", {"mono_ns", "wall_ns"}},
      {"File", {"open", "close", "read", "write"}},
      {"Core.DL",
       {"open", "sym", "close", "last_error", "call_i32", "call_i64", "call_f32", "call_f64",
        "call_str0"}},
      {"Core.Os", {"args_count", "args_get", "env_get", "cwd_get", "time_mono_ns", "time_wall_ns", "sleep_ms"}},
      {"Core.Fs", {"open", "close", "read", "write"}},
      {"Core.Log", {"log"}},
  };

  std::unordered_set<std::string> labels;
  uint32_t line_index = 0;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') continue;
    const std::string line = text.substr(start, i - start);
    std::string trimmed = TrimLeftAscii(line);
    if (trimmed.rfind("import", 0) == 0) {
      size_t pos = 6;
      while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
      if (pos < trimmed.size() && trimmed[pos] == '"') {
        ++pos;
        const size_t end_quote = trimmed.find('"', pos);
        if (end_quote != std::string::npos) {
          const std::string import_path = trimmed.substr(pos, end_quote - pos);
          std::string alias = DefaultImportAlias(import_path);
          size_t tail = end_quote + 1;
          while (tail < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[tail]))) ++tail;
          if (tail + 2 <= trimmed.size() && trimmed.compare(tail, 2, "as") == 0) {
            tail += 2;
            while (tail < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[tail]))) ++tail;
            size_t alias_end = tail;
            while (alias_end < trimmed.size() && IsIdentChar(trimmed[alias_end])) ++alias_end;
            if (alias_end > tail) alias = trimmed.substr(tail, alias_end - tail);
          }
          const auto mod_it = kModuleMembers.find(import_path);
          if (mod_it != kModuleMembers.end() && IsValidIdentifierName(alias)) {
            for (const auto& member : mod_it->second) {
              labels.insert(alias + "." + member);
            }
          }
        }
      }
    }
    ++line_index;
    start = i + 1;
  }

  std::vector<std::string> out(labels.begin(), labels.end());
  std::sort(out.begin(), out.end());
  return out;
}

std::string QualifiedMemberAtPosition(const std::string& text, uint32_t line, uint32_t character) {
  const std::string line_text = GetLineText(text, line);
  if (line_text.empty()) return {};
  size_t pos = std::min<size_t>(character, line_text.size() ? line_text.size() - 1 : 0);
  if (!IsIdentChar(line_text[pos])) {
    if (pos > 0 && IsIdentChar(line_text[pos - 1])) --pos;
    else return {};
  }
  size_t member_begin = pos;
  while (member_begin > 0 && IsIdentChar(line_text[member_begin - 1])) --member_begin;
  size_t member_end = pos + 1;
  while (member_end < line_text.size() && IsIdentChar(line_text[member_end])) ++member_end;
  if (member_begin == 0 || line_text[member_begin - 1] != '.') return {};
  size_t recv_end = member_begin - 1;
  size_t recv_begin = recv_end;
  while (recv_begin > 0 && IsIdentChar(line_text[recv_begin - 1])) --recv_begin;
  if (recv_begin == recv_end) return {};
  return line_text.substr(recv_begin, recv_end - recv_begin) + "." +
         line_text.substr(member_begin, member_end - member_begin);
}

std::unordered_map<std::string, std::string> CollectImportAliasMap(const std::string& text) {
  std::unordered_map<std::string, std::string> aliases;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') continue;
    const std::string line = text.substr(start, i - start);
    const std::string trimmed = TrimLeftAscii(line);
    if (trimmed.rfind("import", 0) == 0) {
      size_t pos = 6;
      while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
      if (pos < trimmed.size() && trimmed[pos] == '"') {
        ++pos;
        const size_t end_quote = trimmed.find('"', pos);
        if (end_quote != std::string::npos) {
          const std::string import_path = trimmed.substr(pos, end_quote - pos);
          std::string alias = DefaultImportAlias(import_path);
          size_t tail = end_quote + 1;
          while (tail < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[tail]))) ++tail;
          if (tail + 2 <= trimmed.size() && trimmed.compare(tail, 2, "as") == 0) {
            tail += 2;
            while (tail < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[tail]))) ++tail;
            size_t alias_end = tail;
            while (alias_end < trimmed.size() && IsIdentChar(trimmed[alias_end])) ++alias_end;
            if (alias_end > tail) alias = trimmed.substr(tail, alias_end - tail);
          }
          if (IsValidIdentifierName(alias)) aliases[alias] = import_path;
        }
      }
    }
    start = i + 1;
  }
  return aliases;
}

std::string NormalizeCoreDlMember(const std::string& member) {
  if (member == "Open") return "open";
  if (member == "Sym") return "sym";
  if (member == "Close") return "close";
  if (member == "LastError") return "last_error";
  if (member == "CallI32") return "call_i32";
  if (member == "CallI64") return "call_i64";
  if (member == "CallF32") return "call_f32";
  if (member == "CallF64") return "call_f64";
  if (member == "CallStr0") return "call_str0";
  return member;
}

bool ResolveReservedModuleSignature(const std::string& call_name,
                                    const std::string& text,
                                    ReservedSignature* out) {
  if (!out) return false;
  out->params.clear();
  out->return_type.clear();
  std::string module;
  std::string member;
  if (!ResolveImportedModuleAndMember(call_name, text, &module, &member)) return false;
  if (module == "Math") {
    if (member == "abs") {
      out->params = {"value"};
      out->return_type = "i32|i64";
      return true;
    }
    if (member == "min" || member == "max") {
      out->params = {"lhs", "rhs"};
      out->return_type = "numeric";
      return true;
    }
    return false;
  }
  if (module == "Time") {
    if (member == "mono_ns" || member == "wall_ns") {
      out->return_type = "i64";
      return true;
    }
    return false;
  }
  if (module == "File" || module == "Core.Fs") {
    if (member == "open") {
      out->params = {"path", "flags"};
      out->return_type = "i32";
      return true;
    }
    if (member == "close") {
      out->params = {"fd"};
      out->return_type = "void";
      return true;
    }
    if (member == "read" || member == "write") {
      out->params = {"fd", "buffer", "count"};
      out->return_type = "i32";
      return true;
    }
    return false;
  }
  if (module == "Core.Os") {
    if (member == "args_count" || member == "cwd_get" || member == "time_mono_ns" ||
        member == "time_wall_ns") {
      out->return_type =
          (member == "args_count") ? "i32" : ((member == "cwd_get") ? "string" : "i64");
      return true;
    }
    if (member == "args_get") {
      out->params = {"index"};
      out->return_type = "string";
      return true;
    }
    if (member == "env_get") {
      out->params = {"key"};
      out->return_type = "string";
      return true;
    }
    if (member == "sleep_ms") {
      out->params = {"milliseconds"};
      out->return_type = "void";
      return true;
    }
    return false;
  }
  if (module == "Core.Log") {
    if (member == "log") {
      out->params = {"message", "level"};
      out->return_type = "void";
      return true;
    }
    return false;
  }
  if (module == "Core.DL") {
    member = NormalizeCoreDlMember(member);
    if (member == "open") {
      out->params = {"path"};
      out->return_type = "i64";
      return true;
    }
    if (member == "sym") {
      out->params = {"handle", "name"};
      out->return_type = "i64";
      return true;
    }
    if (member == "close" || member == "call_str0") {
      out->params = {"handle"};
      out->return_type = (member == "close") ? "i32" : "string";
      return true;
    }
    if (member == "last_error") {
      out->return_type = "string";
      return true;
    }
    if (member == "call_i32" || member == "call_i64" || member == "call_f32" || member == "call_f64") {
      out->params = {"fn_ptr", "a0", "a1"};
      out->return_type = member.substr(5);
      return true;
    }
    return false;
  }
  return false;
}

bool ResolveImportedModuleAndMember(const std::string& call_name,
                                    const std::string& text,
                                    std::string* out_module,
                                    std::string* out_member) {
  if (!out_module || !out_member) return false;
  const size_t dot = call_name.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= call_name.size()) return false;
  const std::string alias = call_name.substr(0, dot);
  std::string member = call_name.substr(dot + 1);
  const auto aliases = CollectImportAliasMap(text);
  const auto alias_it = aliases.find(alias);
  if (alias_it == aliases.end()) return false;
  std::string module = alias_it->second;
  if (module == "Core.DL") member = NormalizeCoreDlMember(member);
  *out_module = std::move(module);
  *out_member = std::move(member);
  return true;
}

bool IsProtectedReservedMemberToken(const std::vector<TokenRef>& refs,
                                    size_t index,
                                    const std::string& text) {
  if (index >= refs.size()) return false;
  if (refs[index].token.kind != Simple::Lang::TokenKind::Identifier) return false;
  if (index < 2) return false;
  if (refs[index - 1].token.kind != Simple::Lang::TokenKind::Dot) return false;
  if (refs[index - 2].token.kind != Simple::Lang::TokenKind::Identifier) return false;
  std::string module;
  std::string member;
  const std::string call_name = refs[index - 2].token.text + "." + refs[index].token.text;
  return ResolveImportedModuleAndMember(call_name, text, &module, &member);
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
  std::string hover_text = ident;
  auto resolve_decl_type = [&](const std::string& text, std::string* out_type) -> bool {
    if (!out_type) return false;
    const auto refs = LexTokenRefs(text);
    for (const auto& ref : refs) {
      if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
      if (ref.token.text != ident) continue;
      if (!IsDeclNameAt(refs, ref.index)) continue;
      if (ref.index + 2 < refs.size() &&
          refs[ref.index + 1].token.kind == Simple::Lang::TokenKind::Colon &&
          refs[ref.index + 2].token.kind == Simple::Lang::TokenKind::Identifier) {
        *out_type = refs[ref.index + 2].token.text;
        return true;
      }
    }
    return false;
  };
  std::string decl_type;
  if (!resolve_decl_type(it->second, &decl_type)) {
    const auto sorted_uris = SortedOpenDocUris(open_docs, uri);
    for (const auto& other_uri : sorted_uris) {
      const auto other_it = open_docs.find(other_uri);
      if (other_it == open_docs.end()) continue;
      const std::string& other_text = other_it->second;
      if (resolve_decl_type(other_text, &decl_type)) break;
    }
  }
  if (!decl_type.empty()) {
    hover_text = ident + " : " + decl_type;
  } else {
    const std::string call_name = QualifiedMemberAtPosition(it->second, line, character);
    ReservedSignature reserved_sig;
    if (!call_name.empty() && ResolveReservedModuleSignature(call_name, it->second, &reserved_sig)) {
      std::string params;
      for (size_t i = 0; i < reserved_sig.params.size(); ++i) {
        if (!params.empty()) params += ", ";
        params += reserved_sig.params[i];
      }
      hover_text = call_name + "(" + params + ")";
      if (!reserved_sig.return_type.empty()) hover_text += " -> " + reserved_sig.return_type;
    }
  }
  WriteLspMessage(
      out,
      "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" +
          JsonEscape(hover_text) + "`\"}}}");
}

void ReplyCompletion(std::ostream& out,
                     const std::string& id_raw,
                     const std::string& uri,
                     uint32_t line,
                     uint32_t character,
                     const std::unordered_map<std::string, std::string>& open_docs) {
  static const std::vector<std::string> kKeywords = {
      "fn", "import", "extern", "if", "else", "while", "for", "return", "break", "skip"};
  std::vector<std::string> labels;

  auto doc_it = open_docs.find(uri);
  bool import_context = false;
  std::string prefix_lc;
  std::string receiver_lc;
  if (doc_it != open_docs.end()) {
    std::string import_prefix;
    import_context = ImportPrefixAtPosition(doc_it->second, line, character, &import_prefix);
    prefix_lc =
        LowerAscii(import_context ? import_prefix : CompletionPrefixAtPosition(doc_it->second, line, character));
    receiver_lc = LowerAscii(CompletionMemberReceiverAtPosition(doc_it->second, line, character));
  }
  if (import_context) {
    labels = CollectImportCandidates(open_docs);
  } else {
    labels = kKeywords;
    auto add_label = [&](const std::string& label, std::unordered_set<std::string>* seen) {
      if (!seen) return;
      if (seen->insert(label).second) labels.push_back(label);
    };
    std::unordered_set<std::string> seen(labels.begin(), labels.end());
    add_label("IO.println", &seen);
    add_label("IO.print", &seen);
    if (doc_it != open_docs.end()) {
      const auto reserved_labels = CollectReservedModuleMemberLabels(doc_it->second);
      for (const auto& label : reserved_labels) add_label(label, &seen);
    }
    auto add_doc_decls = [&](const std::string& text) {
      const auto refs = LexTokenRefs(text);
      for (const auto& ref : refs) {
        if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
        if (!IsDeclNameAt(refs, ref.index)) continue;
        add_label(ref.token.text, &seen);
      }
    };
    if (doc_it != open_docs.end()) {
      add_doc_decls(doc_it->second);
    }
    for (const auto& [other_uri, other_text] : open_docs) {
      if (other_uri == uri) continue;
      add_doc_decls(other_text);
    }
    std::sort(labels.begin(), labels.end());
  }

  std::string items;
  bool first_item = true;
  for (size_t i = 0; i < labels.size(); ++i) {
    const std::string& label = labels[i];
    if (!receiver_lc.empty()) {
      const size_t dot = label.find('.');
      if (dot == std::string::npos) continue;
      const std::string left = LowerAscii(label.substr(0, dot));
      const std::string right = LowerAscii(label.substr(dot + 1));
      if (left != receiver_lc) continue;
      if (!prefix_lc.empty() && right.rfind(prefix_lc, 0) != 0) continue;
    } else if (!prefix_lc.empty()) {
      const std::string label_lc = LowerAscii(label);
      if (label_lc.rfind(prefix_lc, 0) != 0) continue;
    }
    if (!first_item) items += ",";
    first_item = false;
    const bool is_builtin = label.find('.') != std::string::npos;
    const bool is_keyword = std::find(kKeywords.begin(), kKeywords.end(), label) != kKeywords.end();
    const int kind = import_context ? 9 : (is_builtin ? 3 : (is_keyword ? 14 : 6));
    items += "{\"label\":\"" + JsonEscape(label) + "\",\"kind\":" + std::to_string(kind) + "}";
  }
  WriteLspMessage(
      out,
      "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"result\":{\"isIncomplete\":false,\"items\":[" + items + "]}}");
}

void ReplySignatureHelp(std::ostream& out,
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
  const std::string call_name = CallNameAtPosition(it->second, line, character);
  const uint32_t active_parameter = ActiveParameterAtPosition(it->second, line, character);
  if (call_name == "IO.println" || call_name == "IO.print") {
    const uint32_t active_signature = active_parameter == 0 ? 0 : 1;
    const uint32_t active_param_for_sig = active_parameter == 0 ? 0 : 1;
    WriteLspMessage(
        out,
        "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
            ",\"result\":{\"signatures\":["
            "{\"label\":\"" + call_name +
            "(value)\",\"parameters\":[{\"label\":\"value\"}]},"
            "{\"label\":\"" + call_name +
            "(format, values...)\",\"parameters\":[{\"label\":\"format\"},{\"label\":\"values...\"}]}"
            "],\"activeSignature\":" + std::to_string(active_signature) +
            ",\"activeParameter\":" + std::to_string(active_param_for_sig) + "}}");
    return;
  }

  std::string imported_module;
  std::string imported_member;
  if (ResolveImportedModuleAndMember(call_name, it->second, &imported_module, &imported_member) &&
      imported_module == "IO" && (imported_member == "print" || imported_member == "println")) {
    const uint32_t active_signature = active_parameter == 0 ? 0 : 1;
    const uint32_t active_param_for_sig = active_parameter == 0 ? 0 : 1;
    WriteLspMessage(
        out,
        "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
            ",\"result\":{\"signatures\":["
            "{\"label\":\"" + call_name +
            "(value)\",\"parameters\":[{\"label\":\"value\"}]},"
            "{\"label\":\"" + call_name +
            "(format, values...)\",\"parameters\":[{\"label\":\"format\"},{\"label\":\"values...\"}]}"
            "],\"activeSignature\":" + std::to_string(active_signature) +
            ",\"activeParameter\":" + std::to_string(active_param_for_sig) + "}}");
    return;
  }

  if (ResolveImportedModuleAndMember(call_name, it->second, &imported_module, &imported_member) &&
      imported_module == "Core.DL" && imported_member == "open") {
    const uint32_t active_signature = active_parameter == 0 ? 0 : 1;
    const uint32_t active_param_for_sig = active_parameter == 0 ? 0 : 1;
    WriteLspMessage(
        out,
        "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
            ",\"result\":{\"signatures\":["
            "{\"label\":\"" + call_name +
            "(path)\",\"parameters\":[{\"label\":\"path\"}]},"
            "{\"label\":\"" + call_name +
            "(path, manifest)\",\"parameters\":[{\"label\":\"path\"},{\"label\":\"manifest\"}]}"
            "],\"activeSignature\":" + std::to_string(active_signature) +
            ",\"activeParameter\":" + std::to_string(active_param_for_sig) + "}}");
    return;
  }

  if (IsAtCastCallName(call_name)) {
    WriteLspMessage(
        out,
        "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
            ",\"result\":{\"signatures\":[{\"label\":\"" + call_name +
            "(value)\",\"parameters\":[{\"label\":\"value\"}]}],"
            "\"activeSignature\":0,\"activeParameter\":0}}");
    return;
  }

  ReservedSignature reserved_sig;
  if (ResolveReservedModuleSignature(call_name, it->second, &reserved_sig)) {
    std::string params;
    std::string parameters_json;
    for (size_t i = 0; i < reserved_sig.params.size(); ++i) {
      if (!params.empty()) params += ", ";
      params += reserved_sig.params[i];
      if (!parameters_json.empty()) parameters_json += ",";
      parameters_json += "{\"label\":\"" + JsonEscape(reserved_sig.params[i]) + "\"}";
    }
    const uint32_t clamped_active =
        reserved_sig.params.empty() ? 0
                                    : std::min(active_parameter,
                                               static_cast<uint32_t>(reserved_sig.params.size() - 1));
    WriteLspMessage(
        out,
        "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
            ",\"result\":{\"signatures\":[{\"label\":\"" +
            JsonEscape(call_name + "(" + params + ")") +
            "\",\"parameters\":[" + parameters_json + "]}],"
            "\"activeSignature\":0,\"activeParameter\":" +
            std::to_string(clamped_active) + "}}");
    return;
  }

  const auto refs = LexTokenRefs(it->second);
  for (size_t i = 0; i + 3 < refs.size(); ++i) {
    if (!IsDeclNameAt(refs, i)) continue;
    if (refs[i].token.text != call_name) continue;
    if (refs[i + 1].token.kind != Simple::Lang::TokenKind::Colon) continue;
    if (refs[i + 3].token.kind != Simple::Lang::TokenKind::LParen) continue;
    std::string params;
    std::string parameters_json;
    uint32_t param_count = 0;
    size_t p = i + 4;
    while (p < refs.size() && refs[p].token.kind != Simple::Lang::TokenKind::RParen) {
      if (refs[p].token.kind == Simple::Lang::TokenKind::Identifier &&
          p + 2 < refs.size() &&
          refs[p + 1].token.kind == Simple::Lang::TokenKind::Colon &&
          refs[p + 2].token.kind == Simple::Lang::TokenKind::Identifier) {
        const std::string param_label = refs[p].token.text + " : " + refs[p + 2].token.text;
        if (!params.empty()) params += ", ";
        params += param_label;
        if (!parameters_json.empty()) parameters_json += ",";
        parameters_json += "{\"label\":\"" + JsonEscape(param_label) + "\"}";
        ++param_count;
        p += 3;
        continue;
      }
      ++p;
    }
    const uint32_t clamped_active =
        param_count == 0 ? 0 : std::min(active_parameter, param_count - 1);
    WriteLspMessage(
        out,
        "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
            ",\"result\":{\"signatures\":[{\"label\":\"" +
            JsonEscape(call_name + "(" + params + ")") +
            "\",\"parameters\":[" + parameters_json + "]}],"
            "\"activeSignature\":0,\"activeParameter\":" +
            std::to_string(clamped_active) + "}}");
    return;
  }

  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
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

bool IsAtCastCallName(const std::string& name) {
  if (name.size() < 2 || name[0] != '@') return false;
  const std::string target = name.substr(1);
  if (target == "void" || target == "string") return false;
  return IsPrimitiveTypeName(target);
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

uint32_t SemanticTokenTypeIndexForRef(const std::vector<TokenRef>& refs, size_t i) {
  using TK = Simple::Lang::TokenKind;
  if (i >= refs.size()) return 3;
  const auto& token = refs[i].token;
  if (IsKeywordToken(token.kind)) return 0; // keyword
  if (token.kind == TK::String || token.kind == TK::Char) return 8; // string
  if (token.kind == TK::Integer || token.kind == TK::Float) return 9; // number
  if (IsOperatorToken(token.kind)) return 10; // operator
  if (token.kind == TK::Identifier) {
    if (i > 0 && refs[i - 1].token.kind == TK::Colon) return 1; // type position
    if (IsDeclNameAt(refs, i)) {
      if (i + 3 < refs.size() &&
          refs[i + 1].token.kind == TK::Colon &&
          refs[i + 3].token.kind == TK::LParen) {
        return 2; // function declaration
      }
      return 3; // variable-like declaration
    }
    if (IsPrimitiveTypeName(token.text)) return 1;
  }
  return SemanticTokenTypeIndex(token);
}

uint32_t SemanticTokenModifiersForRef(const std::vector<TokenRef>& refs, size_t i) {
  using TK = Simple::Lang::TokenKind;
  if (i >= refs.size()) return 0;
  if (refs[i].token.kind == TK::Identifier && IsDeclNameAt(refs, i)) {
    return 1u << 0; // declaration
  }
  return 0;
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
  const auto refs = LexTokenRefs(it->second);
  std::string data;
  uint32_t prev_line = 0;
  uint32_t prev_col = 0;
  bool first = true;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const auto& token = tokens[i];
    if (token.kind == Simple::Lang::TokenKind::End ||
        token.kind == Simple::Lang::TokenKind::Invalid) {
      continue;
    }
    const uint32_t line = token.line > 0 ? (token.line - 1) : 0;
    const uint32_t col = token.column > 0 ? (token.column - 1) : 0;
    const uint32_t len = static_cast<uint32_t>(token.text.size() > 0 ? token.text.size() : 1);
    const uint32_t token_type = SemanticTokenTypeIndexForRef(refs, i);
    const uint32_t modifiers = SemanticTokenModifiersForRef(refs, i);
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

bool IsWriteUsageAt(const std::vector<TokenRef>& refs, size_t i) {
  using TK = Simple::Lang::TokenKind;
  if (i >= refs.size()) return false;
  if (refs[i].token.kind != TK::Identifier) return false;
  if (IsDeclNameAt(refs, i)) return true;
  if (i + 1 < refs.size() && refs[i + 1].token.kind == TK::Assign) return true;
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

std::string TextEditJson(const Simple::Lang::Token& tk, const std::string& new_text) {
  const uint32_t line = tk.line > 0 ? (tk.line - 1) : 0;
  const uint32_t col = tk.column > 0 ? (tk.column - 1) : 0;
  const uint32_t len = static_cast<uint32_t>(tk.text.empty() ? 1 : tk.text.size());
  return "{\"range\":{\"start\":{\"line\":" + std::to_string(line) +
         ",\"character\":" + std::to_string(col) + "},\"end\":{\"line\":" +
         std::to_string(line) + ",\"character\":" + std::to_string(col + len) +
         "}},\"newText\":\"" + JsonEscape(new_text) + "\"}";
}

std::string DocumentHighlightJson(const Simple::Lang::Token& tk, uint32_t kind) {
  const uint32_t line = tk.line > 0 ? (tk.line - 1) : 0;
  const uint32_t col = tk.column > 0 ? (tk.column - 1) : 0;
  const uint32_t len = static_cast<uint32_t>(tk.text.empty() ? 1 : tk.text.size());
  return "{\"range\":{\"start\":{\"line\":" + std::to_string(line) +
         ",\"character\":" + std::to_string(col) + "},\"end\":{\"line\":" +
         std::to_string(line) + ",\"character\":" + std::to_string(col + len) +
         "}},\"kind\":" + std::to_string(kind) + "}";
}

bool IsValidIdentifierName(const std::string& name) {
  static const std::unordered_set<std::string> kReserved = {
      "while", "for", "break", "skip", "return", "if", "else", "default",
      "fn", "self", "artifact", "enum", "module", "import", "extern", "as",
      "true", "false",
  };
  if (name.empty()) return false;
  if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
  for (size_t i = 1; i < name.size(); ++i) {
    if (!IsIdentChar(name[i])) return false;
  }
  if (kReserved.find(name) != kReserved.end()) return false;
  return true;
}

bool ExtractUndeclaredIdentifierName(const std::string& error, std::string* out_name) {
  if (!out_name) return false;
  static const std::string marker = "undeclared identifier:";
  const size_t pos = error.find(marker);
  if (pos == std::string::npos) return false;
  std::string tail = TrimCopy(error.substr(pos + marker.size()));
  if (tail.empty()) return false;
  size_t end = 0;
  while (end < tail.size() && IsIdentChar(tail[end])) ++end;
  if (end == 0) return false;
  const std::string name = tail.substr(0, end);
  if (!IsValidIdentifierName(name)) return false;
  *out_name = name;
  return true;
}

std::string InferNumericDeclarationType(const std::string& text, const std::string& ident) {
  if (ident.empty()) return "i32";
  size_t search_from = 0;
  while (search_from < text.size()) {
    const size_t found = text.find(ident, search_from);
    if (found == std::string::npos) break;
    const bool left_ok = found == 0 || !IsIdentChar(text[found - 1]);
    const size_t after = found + ident.size();
    const bool right_ok = after >= text.size() || !IsIdentChar(text[after]);
    if (!left_ok || !right_ok) {
      search_from = found + ident.size();
      continue;
    }
    size_t i = after;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size() || text[i] != '=') {
      search_from = found + ident.size();
      continue;
    }
    ++i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size()) break;
    auto boundary_ok = [&](size_t pos) {
      return pos >= text.size() || !IsIdentChar(text[pos]);
    };
    if (text.compare(i, 4, "true") == 0 && boundary_ok(i + 4)) return "bool";
    if (text.compare(i, 5, "false") == 0 && boundary_ok(i + 5)) return "bool";
    if (text[i] == '"') return "string";
    if (text[i] == '\'') return "char";
    bool seen_digit = false;
    bool seen_dot = false;
    if (text[i] == '-') ++i;
    while (i < text.size()) {
      const char c = text[i];
      if (std::isdigit(static_cast<unsigned char>(c))) {
        seen_digit = true;
        ++i;
        continue;
      }
      if (c == '.') {
        seen_dot = true;
        ++i;
        continue;
      }
      break;
    }
    if (seen_digit) return seen_dot ? "f64" : "i32";
    search_from = found + ident.size();
  }
  return "i32";
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
  std::string best_uri = uri;
  Simple::Lang::Token best_token;
  bool has_best = false;

  auto choose_best_from_doc = [&](const std::string& doc_uri, const std::vector<TokenRef>& doc_refs) {
    for (const auto& ref : doc_refs) {
      if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
      if (ref.token.text != name) continue;
      if (!IsDeclNameAt(doc_refs, ref.index)) continue;
      if (!has_best || doc_uri == uri) {
        best_token = ref.token;
        best_uri = doc_uri;
        has_best = true;
      }
      if (doc_uri == uri) break;
    }
  };

  choose_best_from_doc(uri, refs);
  if (!has_best) {
    const auto sorted_uris = SortedOpenDocUris(open_docs, uri);
    for (const auto& other_uri : sorted_uris) {
      const auto other_it = open_docs.find(other_uri);
      if (other_it == open_docs.end()) continue;
      const auto other_refs = LexTokenRefs(other_it->second);
      choose_best_from_doc(other_uri, other_refs);
      if (has_best) break;
    }
  }
  if (!has_best) {
    best_token = target->token;
    best_uri = uri;
  }
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[" +
                           LocationJson(best_uri, best_token) + "]}");
}

void ReplyReferences(std::ostream& out,
                     const std::string& id_raw,
                     const std::string& uri,
                     uint32_t line,
                     uint32_t character,
                     bool include_declaration,
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
  struct RefHit {
    std::string doc_uri;
    Simple::Lang::Token token;
  };
  std::vector<RefHit> hits;

  auto collect_hits = [&](const std::string& doc_uri, const std::vector<TokenRef>& doc_refs) {
    for (const auto& ref : doc_refs) {
      if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
      if (ref.token.text != name) continue;
      if (!include_declaration && IsDeclNameAt(doc_refs, ref.index)) continue;
      hits.push_back(RefHit{doc_uri, ref.token});
    }
  };

  collect_hits(uri, refs);
  for (const auto& [other_uri, other_text] : open_docs) {
    if (other_uri == uri) continue;
    const auto other_refs = LexTokenRefs(other_text);
    collect_hits(other_uri, other_refs);
  }

  std::sort(hits.begin(), hits.end(), [](const RefHit& a, const RefHit& b) {
    if (a.doc_uri != b.doc_uri) return a.doc_uri < b.doc_uri;
    if (a.token.line != b.token.line) return a.token.line < b.token.line;
    if (a.token.column != b.token.column) return a.token.column < b.token.column;
    return a.token.text < b.token.text;
  });

  std::string result;
  for (const auto& hit : hits) {
    if (!result.empty()) result += ",";
    result += LocationJson(hit.doc_uri, hit.token);
  }
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[" + result + "]}");
}

void ReplyDocumentHighlight(std::ostream& out,
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
    const uint32_t kind = IsWriteUsageAt(refs, ref.index) ? 3u : 2u;
    if (!result.empty()) result += ",";
    result += DocumentHighlightJson(ref.token, kind);
  }
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[" + result + "]}");
}

uint32_t SymbolKindFor(const std::vector<TokenRef>& refs, size_t i) {
  using TK = Simple::Lang::TokenKind;
  if (i > 0 && refs[i - 1].token.kind == TK::KwFn) return 12;
  if (i + 3 < refs.size() &&
      refs[i + 1].token.kind == TK::Colon &&
      refs[i + 2].token.kind == TK::Identifier &&
      refs[i + 3].token.kind == TK::LParen) {
    return 12;
  }
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

void ReplyWorkspaceSymbols(std::ostream& out,
                           const std::string& id_raw,
                           const std::string& query,
                           const std::unordered_map<std::string, std::string>& open_docs) {
  struct SymbolInfo {
    std::string uri;
    std::string name;
    uint32_t kind = 13;
    uint32_t line = 0;
    uint32_t col = 0;
    uint32_t len = 1;
  };

  const std::string query_lc = LowerAscii(query);
  std::vector<SymbolInfo> symbols;
  for (const auto& [uri, text] : open_docs) {
    const auto refs = LexTokenRefs(text);
    for (const auto& ref : refs) {
      if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
      if (ref.depth != 0) continue;
      if (!IsDeclNameAt(refs, ref.index)) continue;
      if (!query_lc.empty()) {
        const std::string name_lc = LowerAscii(ref.token.text);
        if (name_lc.rfind(query_lc, 0) != 0) continue;
      }
      SymbolInfo info;
      info.uri = uri;
      info.name = ref.token.text;
      info.kind = SymbolKindFor(refs, ref.index);
      info.line = ref.token.line > 0 ? (ref.token.line - 1) : 0;
      info.col = ref.token.column > 0 ? (ref.token.column - 1) : 0;
      info.len = static_cast<uint32_t>(ref.token.text.empty() ? 1 : ref.token.text.size());
      symbols.push_back(std::move(info));
    }
  }
  std::sort(symbols.begin(), symbols.end(), [](const SymbolInfo& a, const SymbolInfo& b) {
    if (a.uri != b.uri) return a.uri < b.uri;
    if (a.line != b.line) return a.line < b.line;
    if (a.col != b.col) return a.col < b.col;
    return a.name < b.name;
  });

  std::string result;
  for (const auto& symbol : symbols) {
    if (!result.empty()) result += ",";
    result += "{\"name\":\"" + JsonEscape(symbol.name) + "\",\"kind\":" +
              std::to_string(symbol.kind) + ",\"location\":{\"uri\":\"" +
              JsonEscape(symbol.uri) + "\",\"range\":{\"start\":{\"line\":" +
              std::to_string(symbol.line) + ",\"character\":" + std::to_string(symbol.col) +
              "},\"end\":{\"line\":" + std::to_string(symbol.line) + ",\"character\":" +
              std::to_string(symbol.col + symbol.len) + "}}}}";
  }
  WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[" + result + "]}");
}

void ReplyRename(std::ostream& out,
                 const std::string& id_raw,
                 const std::string& uri,
                 uint32_t line,
                 uint32_t character,
                 const std::string& new_name,
                 const std::unordered_map<std::string, std::string>& open_docs) {
  if (!IsValidIdentifierName(new_name)) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  auto doc_it = open_docs.find(uri);
  if (doc_it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  const auto refs = LexTokenRefs(doc_it->second);
  const TokenRef* target = FindIdentifierAt(refs, line, character);
  if (!target) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  if (IsProtectedReservedMemberToken(refs, target->index, doc_it->second)) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  const std::string old_name = target->token.text;

  struct RenameHit {
    std::string doc_uri;
    Simple::Lang::Token token;
  };
  std::vector<RenameHit> hits;
  auto collect_hits = [&](const std::string& doc_uri, const std::string& text) {
    const auto doc_refs = LexTokenRefs(text);
    for (const auto& ref : doc_refs) {
      if (ref.token.kind != Simple::Lang::TokenKind::Identifier) continue;
      if (ref.token.text != old_name) continue;
      hits.push_back(RenameHit{doc_uri, ref.token});
    }
  };

  collect_hits(uri, doc_it->second);
  for (const auto& [other_uri, other_text] : open_docs) {
    if (other_uri == uri) continue;
    collect_hits(other_uri, other_text);
  }

  std::sort(hits.begin(), hits.end(), [](const RenameHit& a, const RenameHit& b) {
    if (a.doc_uri != b.doc_uri) return a.doc_uri < b.doc_uri;
    if (a.token.line != b.token.line) return a.token.line < b.token.line;
    if (a.token.column != b.token.column) return a.token.column < b.token.column;
    return a.token.text < b.token.text;
  });

  std::string changes_json;
  std::string current_uri;
  std::string current_edits;
  for (const auto& hit : hits) {
    if (current_uri.empty()) current_uri = hit.doc_uri;
    if (hit.doc_uri != current_uri) {
      if (!changes_json.empty()) changes_json += ",";
      changes_json += "\"" + JsonEscape(current_uri) + "\":[" + current_edits + "]";
      current_uri = hit.doc_uri;
      current_edits.clear();
    }
    if (!current_edits.empty()) current_edits += ",";
    current_edits += TextEditJson(hit.token, new_name);
  }
  if (!current_uri.empty()) {
    if (!changes_json.empty()) changes_json += ",";
    changes_json += "\"" + JsonEscape(current_uri) + "\":[" + current_edits + "]";
  }

  WriteLspMessage(out,
                  "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
                      ",\"result\":{\"changes\":{" + changes_json + "}}}");
}

void ReplyPrepareRename(std::ostream& out,
                        const std::string& id_raw,
                        const std::string& uri,
                        uint32_t line,
                        uint32_t character,
                        const std::unordered_map<std::string, std::string>& open_docs) {
  auto doc_it = open_docs.find(uri);
  if (doc_it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  const auto refs = LexTokenRefs(doc_it->second);
  const TokenRef* target = FindIdentifierAt(refs, line, character);
  if (!target || !IsValidIdentifierName(target->token.text)) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  if (IsProtectedReservedMemberToken(refs, target->index, doc_it->second)) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
    return;
  }
  const uint32_t tk_line = target->token.line > 0 ? (target->token.line - 1) : 0;
  const uint32_t tk_col = target->token.column > 0 ? (target->token.column - 1) : 0;
  const uint32_t tk_len = static_cast<uint32_t>(
      target->token.text.empty() ? 1 : target->token.text.size());
  WriteLspMessage(
      out,
      "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"result\":{\"range\":{\"start\":{\"line\":" + std::to_string(tk_line) +
          ",\"character\":" + std::to_string(tk_col) + "},\"end\":{\"line\":" +
          std::to_string(tk_line) + ",\"character\":" +
          std::to_string(tk_col + tk_len) + "}},\"placeholder\":\"" +
          JsonEscape(target->token.text) + "\"}}");
}

void ReplyCodeAction(std::ostream& out,
                     const std::string& id_raw,
                     const std::string& uri,
                     bool allow_quickfix,
                     bool allow_e0001_quickfix,
                     const std::unordered_map<std::string, std::string>& open_docs) {
  if (!allow_quickfix || !allow_e0001_quickfix) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  auto doc_it = open_docs.find(uri);
  if (doc_it == open_docs.end()) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(doc_it->second, &error)) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  std::string ident;
  if (!ExtractUndeclaredIdentifierName(error, &ident)) {
    WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
    return;
  }
  const std::string inferred_type = InferNumericDeclarationType(doc_it->second, ident);
  std::string inferred_init = "0";
  if (inferred_type == "f64") inferred_init = "0.0";
  else if (inferred_type == "bool") inferred_init = "false";
  else if (inferred_type == "string") inferred_init = "\"\"";
  else if (inferred_type == "char") inferred_init = "'\\0'";
  const uint32_t insert_line = PreferredDeclarationInsertLine(doc_it->second);
  const std::string declaration = ident + " : " + inferred_type + " = " + inferred_init + ";\n";
  WriteLspMessage(
      out,
      "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"result\":[{\"title\":\"Declare '" + JsonEscape(ident) +
          "' as " + inferred_type + "\",\"kind\":\"quickfix\",\"edit\":{\"changes\":{\"" +
          JsonEscape(uri) + "\":[{\"range\":{\"start\":{\"line\":" + std::to_string(insert_line) +
          ",\"character\":0},\"end\":{\"line\":" + std::to_string(insert_line) +
          ",\"character\":0}},\"newText\":\"" + JsonEscape(declaration) +
          "\"}]}}}]}");
}

} // namespace

int RunServer(std::istream& in, std::ostream& out) {
  bool saw_shutdown = false;
  std::unordered_map<std::string, std::string> open_docs;
  std::unordered_map<std::string, uint32_t> open_doc_versions;
  std::unordered_set<std::string> canceled_request_ids;
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
                "\"hoverProvider\":true,\"definitionProvider\":true,\"declarationProvider\":true,"
                "\"documentHighlightProvider\":true,"
                "\"referencesProvider\":true,\"documentSymbolProvider\":true,"
                "\"workspaceSymbolProvider\":true,"
                "\"renameProvider\":{\"prepareProvider\":true},"
                "\"codeActionProvider\":true,"
                "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\",\"@\"]},"
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

    if (method == "$/cancelRequest") {
      const size_t params_pos = body.find("\"params\"");
      std::string cancel_id;
      if (params_pos != std::string::npos &&
          ExtractJsonIdRawFromOffset(body, params_pos, &cancel_id)) {
        canceled_request_ids.insert(cancel_id);
      }
      continue;
    }

    if (method == "initialized") {
      continue;
    }

    if (has_id && method != "initialize" && method != "shutdown") {
      if (canceled_request_ids.find(id_raw) != canceled_request_ids.end()) {
        canceled_request_ids.erase(id_raw);
        continue;
      }
    }

    if (method == "$/cancelRequest") {
      continue;
    }

    if (method == "textDocument/didOpen") {
      std::string uri;
      std::string text;
      uint32_t version = 0;
      if (ExtractJsonStringField(body, "uri", &uri) &&
          ExtractJsonStringField(body, "text", &text)) {
        open_docs[uri] = text;
        if (ExtractJsonUintField(body, "version", &version)) {
          open_doc_versions[uri] = version;
        } else {
          open_doc_versions[uri] = 0;
        }
        PublishDiagnostics(out, uri, text);
      }
      continue;
    }

    if (method == "textDocument/didChange") {
      std::string uri;
      std::string text;
      uint32_t version = 0;
      const bool has_version = ExtractJsonUintField(body, "version", &version);
      if (ExtractJsonStringField(body, "uri", &uri) &&
          ExtractJsonStringField(body, "text", &text)) {
        if (open_docs.find(uri) == open_docs.end()) {
          continue; // Ignore changes for unopened documents.
        }
        if (has_version) {
          const auto it = open_doc_versions.find(uri);
          if (it != open_doc_versions.end() && version <= it->second) {
            continue; // Ignore out-of-order or duplicate-version updates.
          }
          open_doc_versions[uri] = version;
        }
        open_docs[uri] = text;
        PublishDiagnostics(out, uri, text);
      }
      continue;
    }

    if (method == "textDocument/didClose") {
      std::string uri;
      if (ExtractJsonStringField(body, "uri", &uri)) {
        open_docs.erase(uri);
        open_doc_versions.erase(uri);
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
      if (has_id) {
        std::string uri;
        uint32_t line = 0;
        uint32_t character = 0;
        ExtractJsonStringField(body, "uri", &uri);
        ExtractJsonUintField(body, "line", &line);
        ExtractJsonUintField(body, "character", &character);
        ReplyCompletion(out, id_raw, uri, line, character, open_docs);
      }
      continue;
    }

    if (method == "textDocument/signatureHelp") {
      if (has_id) {
        std::string uri;
        uint32_t line = 0;
        uint32_t character = 0;
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplySignatureHelp(out, id_raw, uri, line, character, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
        }
      }
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

    if (method == "textDocument/declaration") {
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
        bool include_declaration = true;
        ExtractJsonBoolField(body, "includeDeclaration", &include_declaration);
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplyReferences(out, id_raw, uri, line, character, include_declaration, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
        }
      }
      continue;
    }

    if (method == "textDocument/documentHighlight") {
      if (has_id) {
        std::string uri;
        uint32_t line = 0;
        uint32_t character = 0;
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplyDocumentHighlight(out, id_raw, uri, line, character, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}");
        }
      }
      continue;
    }

    if (method == "textDocument/rename") {
      if (has_id) {
        std::string uri;
        std::string new_name;
        uint32_t line = 0;
        uint32_t character = 0;
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonStringField(body, "newName", &new_name) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplyRename(out, id_raw, uri, line, character, new_name, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
        }
      }
      continue;
    }

    if (method == "textDocument/prepareRename") {
      if (has_id) {
        std::string uri;
        uint32_t line = 0;
        uint32_t character = 0;
        if (ExtractJsonStringField(body, "uri", &uri) &&
            ExtractJsonUintField(body, "line", &line) &&
            ExtractJsonUintField(body, "character", &character)) {
          ReplyPrepareRename(out, id_raw, uri, line, character, open_docs);
        } else {
          WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}");
        }
      }
      continue;
    }

    if (method == "textDocument/codeAction") {
      if (has_id) {
        std::string uri;
        const bool allow_quickfix = CodeActionContextAllowsQuickFix(body);
        const bool allow_e0001_quickfix = CodeActionContextMentionsCode(body, "E0001");
        if (ExtractJsonStringField(body, "uri", &uri)) {
          ReplyCodeAction(out, id_raw, uri, allow_quickfix, allow_e0001_quickfix, open_docs);
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

    if (method == "workspace/symbol") {
      if (has_id) {
        const std::string query = ExtractWorkspaceSymbolQuery(body);
        ReplyWorkspaceSymbols(out, id_raw, query, open_docs);
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
