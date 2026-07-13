#include "test_utils.h"

#include <string>
#include <vector>

#include "cli/cli_test_utils.h"
#include "command_contract.h"
#include "command_dispatch.h"

namespace Simple::VM::Tests {
namespace {

bool RunCliContractExpectFail(const std::string& tool,
                              const std::vector<std::string>& arguments) {
  return !RunCliToolQuiet(tool, arguments);
}

bool CliSimpleStubWithoutPayloadFails() {
  int exit_code = -1;
  const std::string stderr_text = RunCliToolCaptureStderr("simple", {}, "simple_cli_contract_stderr.txt", &exit_code);
  return exit_code == 1 && stderr_text.find("no embedded SBC payload") != std::string::npos;
}

bool CliSimpleRejectsSimpleSource() {
  return RunCliContractExpectFail("simple", {"run", "tests/simple/hello.simple"});
}

bool CliSimpleRejectsBuildCommand() {
  return RunCliContractExpectFail("simple", {"build", "tests/simple/hello.simple"});
}

bool CliSimpleRejectsCompileCommand() {
  return RunCliContractExpectFail("simple", {"compile", "tests/simple/hello.simple"});
}

bool CliSimpleRejectsCheckCommand() {
  return RunCliContractExpectFail("simple", {"check", "tests/simple/hello.simple"});
}

bool CliSimpleRejectsSir() {
  return RunCliContractExpectFail("simple", {"run", "tests/sir/fib_iter.sir"});
}

bool CliSvmRunSimple() {
  return RunCliSvmQuiet({"run", "tests/simple/hello.simple"});
}

bool CliExitCodeContract() {
  int exit_code = -1;
  RunCliSvmCaptureStderr({"--version"}, "simple_cli_contract_stderr.txt", &exit_code);
  if (exit_code != 0) return false;

  RunCliSvmCaptureStderr({"check", "tests/simple/hello.simple"}, "simple_cli_contract_stderr.txt", &exit_code);
  if (exit_code != 0) return false;

  RunCliSvmCaptureStderr({"run", "tests/simple/add_fn.simple"}, "simple_cli_contract_stderr.txt", &exit_code);
  if (exit_code != 42) return false;

  RunCliSvmCaptureStderr({"check", "tests/simple_bad/type_mismatch.simple"}, "simple_cli_contract_stderr.txt", &exit_code);
  return exit_code == 1;
}

bool CliJitStatsPrintFunctionNames() {
  int exit_code = -1;
  const std::string stderr_text = RunCliSvmCaptureStderr(
      {"run", "tests/simple/add_fn.simple", "-jit", "--jit-stats"},
      "simple_cli_contract_stderr.txt", &exit_code);
  return exit_code == 42 && stderr_text.find("[jit] func#") != std::string::npos &&
         stderr_text.find("name=\"main\"") != std::string::npos;
}

bool CliSplitContractDetectsToolModesAndCommands() {
  const auto simple = Simple::CLI::DetectToolMode("simple");
  const auto svm = Simple::CLI::DetectToolMode("svm");
  return simple.simple_only && !simple.compiler_frontend &&
         svm.svm_mode && svm.compiler_frontend &&
         Simple::CLI::IsBuildCommand("compile") &&
         Simple::CLI::IsKnownCommand("lsp") &&
         !Simple::CLI::IsKnownCommand("unknown");
}

bool CliSplitContractClassifiesInputExtensions() {
  return Simple::CLI::IsSimpleSourcePath("game.simple") &&
         Simple::CLI::IsSirPath("build/out.sir") &&
         Simple::CLI::IsSbcPath("module.sbc") &&
         !Simple::CLI::IsSimpleSourcePath("module.sbc");
}

const TestCase kCliContractTests[] = {
  {"cli_simple_stub_without_payload_fails", CliSimpleStubWithoutPayloadFails},
  {"cli_simple_rejects_simple_source", CliSimpleRejectsSimpleSource},
  {"cli_simple_rejects_build_command", CliSimpleRejectsBuildCommand},
  {"cli_simple_rejects_compile_command", CliSimpleRejectsCompileCommand},
  {"cli_simple_rejects_check_command", CliSimpleRejectsCheckCommand},
  {"cli_simple_rejects_sir", CliSimpleRejectsSir},
  {"cli_svm_run_simple", CliSvmRunSimple},
  {"cli_exit_code_contract", CliExitCodeContract},
  {"cli_jit_stats_print_function_names", CliJitStatsPrintFunctionNames},
  {"cli_split_contract_detects_tool_modes_and_commands", CliSplitContractDetectsToolModesAndCommands},
  {"cli_split_contract_classifies_input_extensions", CliSplitContractClassifiesInputExtensions},
};

const TestSection kCliContractSections[] = {
  {"cli_contract", kCliContractTests, sizeof(kCliContractTests) / sizeof(kCliContractTests[0])},
};

} // namespace

const TestSection* GetCliContractSections(size_t* count) {
  if (count) *count = sizeof(kCliContractSections) / sizeof(kCliContractSections[0]);
  return kCliContractSections;
}

} // namespace Simple::VM::Tests
