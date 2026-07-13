#include "diagnostic_bridge.h"

namespace Simple::LSP {
namespace {

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
      default: out.push_back(c); break;
    }
  }
  return out;
}

} // namespace

std::string DiagnosticToLspJson(const Simple::Lang::Diagnostics::Diagnostic& diagnostic) {
  const uint32_t start_line = diagnostic.span.line > 0 ? diagnostic.span.line - 1 : 0;
  const uint32_t start_char = diagnostic.span.column > 0 ? diagnostic.span.column - 1 : 0;
  const uint32_t length = diagnostic.span.length == 0 ? 1 : diagnostic.span.length;
  const uint32_t end_char = start_char + length;
  return "{\"range\":{\"start\":{\"line\":" + std::to_string(start_line) +
         ",\"character\":" + std::to_string(start_char) + "},\"end\":{\"line\":" +
         std::to_string(start_line) + ",\"character\":" + std::to_string(end_char) +
         "}},\"severity\":1,\"code\":\"" + JsonEscape(diagnostic.code) +
         "\",\"source\":\"simple-lsp\",\"message\":\"" + JsonEscape(diagnostic.message) + "\"}";
}

std::string PublishDiagnosticsMessage(const std::string& uri,
                                      const Simple::Lang::Diagnostics::Diagnostic* diagnostic) {
  const std::string diagnostics = diagnostic ? DiagnosticToLspJson(*diagnostic) : std::string();
  return "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
         "\"params\":{\"uri\":\"" + JsonEscape(uri) + "\",\"diagnostics\":[" +
         diagnostics + "]}}";
}

} // namespace Simple::LSP
