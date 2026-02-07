#include "lsp_server.h"

#include <cctype>
#include <cstring>
#include <unordered_map>
#include <string>

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
      value.push_back(json[i++]);
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

    if (has_id) {
      WriteLspMessage(out, "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
                               ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    }
  }
}

} // namespace Simple::LSP
