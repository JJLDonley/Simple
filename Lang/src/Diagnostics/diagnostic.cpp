#include "Diagnostics/diagnostic.h"

namespace Simple::Lang::Diagnostics {

std::string DiagnosticPhaseName(DiagnosticPhase phase) {
  switch (phase) {
    case DiagnosticPhase::Lexer: return "lexer";
    case DiagnosticPhase::CAST: return "cast";
    case DiagnosticPhase::AST: return "ast";
    case DiagnosticPhase::RAST: return "rast";
    case DiagnosticPhase::TAST: return "tast";
    case DiagnosticPhase::IRB: return "irb";
    case DiagnosticPhase::IRE: return "ire";
    case DiagnosticPhase::CLI: return "cli";
    case DiagnosticPhase::LSP: return "lsp";
  }
  return "unknown";
}

std::string FormatDiagnostic(const Diagnostic& diagnostic) {
  std::string text = diagnostic.code + "[" + DiagnosticPhaseName(diagnostic.phase) + "]";
  if (diagnostic.span.line != 0 || diagnostic.span.column != 0) {
    text += " " + std::to_string(diagnostic.span.line) + ":" +
            std::to_string(diagnostic.span.column);
  }
  text += ": " + diagnostic.message;
  if (!diagnostic.help.empty()) text += "\nhelp: " + diagnostic.help;
  return text;
}

} // namespace Simple::Lang::Diagnostics
