#include "test_utils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>

#include "command_contract.h"
#include "command_dispatch.h"

namespace Simple::VM::Tests {
namespace {

std::filesystem::path CliContractTempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

bool RunCliContractCommand(const std::string& command) {
  return std::system((command + " >/dev/null 2>/dev/null").c_str()) == 0;
}

std::string RunCliContractCaptureStderr(const std::string& command, int* exit_code) {
  const auto path = CliContractTempPath("simple_cli_contract_stderr.txt");
  const int rc = std::system((command + " 1>/dev/null 2> " + path.string()).c_str());
  if (exit_code) *exit_code = rc == -1 ? -1 : WEXITSTATUS(rc);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  return text;
}

bool RunCliContractExpectFail(const std::string& command) {
  return !RunCliContractCommand(command);
}

std::string CliToolPath(const std::string& name) {
  const auto build_path = std::filesystem::path("build") / "bin" / name;
  if (std::filesystem::exists(build_path)) return build_path.string();
  return (std::filesystem::path("bin") / name).string();
}

bool CliSimpleStubWithoutPayloadFails() {
  int exit_code = -1;
  const std::string stderr_text = RunCliContractCaptureStderr(CliToolPath("simple"), &exit_code);
  return exit_code == 1 && stderr_text.find("no embedded SBC payload") != std::string::npos;
}

bool CliSimpleRejectsSimpleSource() {
  return RunCliContractExpectFail(CliToolPath("simple") + " run Tests/simple/hello.simple");
}

bool CliSimpleRejectsBuildCommand() {
  return RunCliContractExpectFail(CliToolPath("simple") + " build Tests/simple/hello.simple");
}

bool CliSimpleRejectsCompileCommand() {
  return RunCliContractExpectFail(CliToolPath("simple") + " compile Tests/simple/hello.simple");
}

bool CliSimpleRejectsCheckCommand() {
  return RunCliContractExpectFail(CliToolPath("simple") + " check Tests/simple/hello.simple");
}

bool CliSimpleRejectsSir() {
  return RunCliContractExpectFail(CliToolPath("simple") + " run Tests/sir/fib_iter.sir");
}

bool CliSvmRunSimple() {
  return RunCliContractCommand(CliToolPath("svm") + " run Tests/simple/hello.simple");
}

bool CliExitCodeContract() {
  int exit_code = -1;
  RunCliContractCaptureStderr(CliToolPath("svm") + " --version", &exit_code);
  if (exit_code != 0) return false;

  RunCliContractCaptureStderr(CliToolPath("svm") + " check Tests/simple/hello.simple", &exit_code);
  if (exit_code != 0) return false;

  RunCliContractCaptureStderr(CliToolPath("svm") + " run Tests/simple/add_fn.simple", &exit_code);
  if (exit_code != 42) return false;

  RunCliContractCaptureStderr(CliToolPath("svm") + " check Tests/simple_bad/type_mismatch.simple", &exit_code);
  return exit_code == 1;
}

bool CliJitStatsPrintFunctionNames() {
  int exit_code = -1;
  const std::string stderr_text = RunCliContractCaptureStderr(
      CliToolPath("svm") + " run Tests/simple/add_fn.simple -jit --jit-stats", &exit_code);
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
