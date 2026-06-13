#pragma once

#include <cstdint>
#include <string>

namespace Simple::Lang::Diagnostics {

enum class DiagnosticPhase : uint8_t {
  Lexer,
  CAST,
  AST,
  RAST,
  TAST,
  IRB,
  IRE,
  CLI,
  LSP,
};

struct SourceSpan {
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t length = 0;
};

struct Diagnostic {
  std::string code;
  SourceSpan span;
  DiagnosticPhase phase = DiagnosticPhase::TAST;
  std::string message;
  std::string help;
};

std::string DiagnosticPhaseName(DiagnosticPhase phase);
Diagnostic MakeDiagnostic(std::string code,
                          DiagnosticPhase phase,
                          std::string message,
                          SourceSpan span = {},
                          std::string help = {});
std::string FormatDiagnostic(const Diagnostic& diagnostic);

} // namespace Simple::Lang::Diagnostics
