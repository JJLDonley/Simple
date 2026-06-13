#include "test_utils.h"

#include "diagnostic_render.h"

namespace Simple::VM::Tests {
namespace {

bool CliSplitDiagnosticsRendersErrorLine() {
  return Simple::CLI::RenderErrorLine("undeclared identifier: thing") ==
         "error[E3001]: undeclared identifier: thing";
}

const TestCase kCliDiagnosticsTests[] = {
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
