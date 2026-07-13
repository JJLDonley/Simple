#include "test_utils.h"

#include <filesystem>
#include <string>
#include <vector>

#include "cli/cli_test_utils.h"
#include "diagnostic_render.h"

namespace Simple::VM::Tests {
namespace {

std::string RunCliDiagnosticsCaptureStderr(const std::vector<std::string>& arguments,
                                           int* out_exit_code = nullptr) {
  return RunCliSvmCaptureStderr(arguments, "simple_cli_diagnostic_stderr.txt", out_exit_code);
}

bool CliCheckSimpleErrorFormat() {
  int exit_code = -1;
  const std::string contents =
      RunCliDiagnosticsCaptureStderr({"check", "tests/simple_bad/unknown_identifier.simple"}, &exit_code);
  return exit_code == 1 && contents.find("error[E3001]:") != std::string::npos &&
         contents.find("undeclared identifier") != std::string::npos &&
         contents.find(" --> ") != std::string::npos && contents.find('^') != std::string::npos;
}

bool CliCheckSimpleLexerErrorFormat() {
  int exit_code = -1;
  const std::string contents =
      RunCliDiagnosticsCaptureStderr({"check", "tests/simple_bad/lexer_invalid_char.simple"}, &exit_code);
  return exit_code == 1 && contents.find("error[E1001]:") != std::string::npos &&
         contents.find("unexpected character") != std::string::npos &&
         contents.find(" --> ") != std::string::npos && contents.find('^') != std::string::npos;
}

bool CliCheckSimpleParserErrorFormat() {
  int exit_code = -1;
  const std::string contents =
      RunCliDiagnosticsCaptureStderr({"check", "tests/simple_bad/parser_unterminated_block.simple"}, &exit_code);
  return exit_code == 1 && contents.find("error[E2001]:") != std::string::npos &&
         contents.find("unterminated block") != std::string::npos &&
         contents.find(" --> ") != std::string::npos && contents.find('^') != std::string::npos;
}

bool CliStderrDiagnosticContract() {
  int exit_code = -1;
  std::string stderr_text = RunCliDiagnosticsCaptureStderr({"check", "tests/simple/hello.simple"}, &exit_code);
  if (exit_code != 0 || !stderr_text.empty()) return false;

  stderr_text = RunCliDiagnosticsCaptureStderr({"check", "tests/simple_bad/type_mismatch.simple"}, &exit_code);
  return exit_code == 1 && stderr_text.find("error[E") == 0;
}

bool CliResolverDiagnosticCode() {
  int exit_code = -1;
  std::string stderr_text =
      RunCliDiagnosticsCaptureStderr({"check", "tests/simple_bad/module_unknown_member.simple"}, &exit_code);
  return exit_code == 1 && stderr_text.find("error[E3001]:") == 0 &&
         stderr_text.find("unknown module member") != std::string::npos;
}

bool CliMissingInputDiagnostics() {
  int exit_code = -1;
  std::string stderr_text = RunCliDiagnosticsCaptureStderr({"check"}, &exit_code);
  if (exit_code != 1 || stderr_text.find("error[E8001]: missing input file") != 0) return false;

  const auto missing_path = CliTempPath("simple_missing_input_contract.simple");
  std::filesystem::remove(missing_path);
  stderr_text = RunCliDiagnosticsCaptureStderr(
      {"check", missing_path.string()}, &exit_code);
  return exit_code == 1 &&
         stderr_text.find("error[E8001]: failed to open file: ") == 0 &&
         stderr_text.find(missing_path.filename().string()) != std::string::npos;
}

bool CliSplitDiagnosticsRendersErrorLine() {
  return Simple::CLI::RenderErrorLine("undeclared identifier: thing") ==
         "error[E3001]: undeclared identifier: thing";
}

const TestCase kCliDiagnosticsTests[] = {
  {"cli_check_simple_error_format", CliCheckSimpleErrorFormat},
  {"cli_check_simple_lexer_error_format", CliCheckSimpleLexerErrorFormat},
  {"cli_check_simple_parser_error_format", CliCheckSimpleParserErrorFormat},
  {"cli_stderr_diagnostic_contract", CliStderrDiagnosticContract},
  {"cli_missing_input_diagnostics", CliMissingInputDiagnostics},
  {"cli_resolver_diagnostic_code", CliResolverDiagnosticCode},
  {"cli_split_diagnostics_renders_error_line", CliSplitDiagnosticsRendersErrorLine},
};

const TestSection kCliDiagnosticsSections[] = {
  {"cli_diagnostics", kCliDiagnosticsTests, sizeof(kCliDiagnosticsTests) / sizeof(kCliDiagnosticsTests[0])},
};

} // namespace

const TestSection* GetCliDiagnosticsSections(size_t* count) {
  if (count) *count = sizeof(kCliDiagnosticsSections) / sizeof(kCliDiagnosticsSections[0]);
  return kCliDiagnosticsSections;
}

} // namespace Simple::VM::Tests
