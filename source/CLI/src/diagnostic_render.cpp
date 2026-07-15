#include "diagnostic_render.h"

namespace Simple::CLI {
namespace {

std::string TrimCopy(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r')) ++start;
  size_t end = text.size();
  while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' || text[end - 1] == '\r')) --end;
  return text.substr(start, end - start);
}

} // namespace

std::string DiagnosticCodeFor(const std::string& message) {
  const std::string text = TrimCopy(message);
  auto has = [&](const char* needle) { return text.find(needle) != std::string::npos; };
  if (has("unexpected character") || has("invalid hex escape") || has("invalid string escape") ||
      has("invalid char escape") || has("unterminated string") || has("unterminated char")) {
    return "E1001";
  }
  if (has("unterminated block") || has("expected expression") || has("expected") || has("parse failed")) {
    return "E2001";
  }
  if (has("undeclared identifier") || has("unknown module member") || has("unknown aggregate member") ||
      has("unknown enum member") || has("self outside aggregate")) {
    return "E3001";
  }
  if (has("IR text") || has("lower failed") || has("emit failed") || has("lowering")) return "E5001";
  if (has("load failed") || has("verify failed") || has("bad magic") || has("unsupported version")) return "E6001";
  if (has("runtime trap")) return "E7001";
  if (has("missing input file") || has("failed to open file") || has("simple expects") ||
      has("import file not found") || has("import not found") || has("ambiguous import path") ||
      has("cyclic import") || has("unsupported import path")) {
    return "E8001";
  }
  return "E4001";
}

std::string DiagnosticHelpFor(const std::string& message) {
  if (message.find("unexpected character") != std::string::npos) {
    return "remove unsupported characters or escape them if inside literals";
  }
  if (message.find("unsupported import path") != std::string::npos) {
    return "use a reserved stdlib import, a relative/absolute path, or a unique bare filename under project root";
  }
  if (message.find("import not found in project root") != std::string::npos) {
    return "add the target .simple file under project root or use an explicit relative path";
  }
  if (message.find("ambiguous import path") != std::string::npos) {
    return "rename duplicate files or use an explicit relative path to disambiguate";
  }
  if (message.find("undeclared identifier") != std::string::npos) {
    return "declare the symbol in scope, or fix a typo in the identifier name";
  }
  if (message.find("unterminated block") != std::string::npos) {
    return "add the missing closing '}' for this block";
  }
  if (message.find("expected") != std::string::npos) {
    return "check surrounding syntax near the highlighted token";
  }
  return {};
}

std::string RenderErrorLine(const std::string& message) {
  const std::string trimmed = TrimCopy(message);
  return "error[" + DiagnosticCodeFor(trimmed) + "]: " + trimmed;
}

} // namespace Simple::CLI
