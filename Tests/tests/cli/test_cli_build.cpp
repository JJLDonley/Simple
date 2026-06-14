#include "test_utils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>

#include "build_contract.h"
#include "command_contract.h"

namespace Simple::VM::Tests {
namespace {

std::filesystem::path CliBuildTempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

bool RunCliBuildCommand(const std::string& command) {
  return std::system(command.c_str()) == 0;
}

std::string RunCliBuildCaptureStdout(const std::string& command) {
  const auto path = CliBuildTempPath("simple_cli_build_stdout.txt");
  const int rc = std::system((command + " > " + path.string()).c_str());
  if (rc == -1 || WEXITSTATUS(rc) != 0) return "";
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  return text;
}

bool CliCompileSvmDefaultsToExeAndInfersSimpleExt() {
  const auto out_path = CliBuildTempPath("svm_compile_hello_exec");
  const std::string cmd = "bin/svm compile Tests/simple/hello --out " + out_path.string();
  if (!RunCliBuildCommand(cmd)) return false;
  const bool ok = RunCliBuildCommand(out_path.string());
  std::filesystem::remove(out_path);
  return ok;
}

bool CliCompileSvmOutSbcStaysBytecode() {
  const auto out_path = CliBuildTempPath("svm_compile_hello.sbc");
  const std::string cmd = "bin/svm compile Tests/simple/hello.simple --out " + out_path.string();
  if (!RunCliBuildCommand(cmd)) return false;
  std::ifstream in(out_path, std::ios::binary);
  const bool ok = in.good() && in.peek() != std::ifstream::traits_type::eof();
  std::filesystem::remove(out_path);
  return ok;
}

bool CliBuildDynamicExe() {
  const auto out_path = CliBuildTempPath("simple_build_hello_exec");
  const std::string cmd = "bin/svm build -d Tests/simple/hello.simple --out " + out_path.string();
  if (!RunCliBuildCommand(cmd)) return false;
  if (!RunCliBuildCommand(out_path.string())) return false;
#if defined(__linux__)
  const std::string deps = RunCliBuildCaptureStdout("ldd " + out_path.string());
  if (deps.empty()) return false;
  if (deps.find("libsimplevm_runtime.so") == std::string::npos) return false;
#endif
  std::filesystem::remove(out_path);
  return true;
}

bool CliBuildStaticExe() {
  const auto out_path = CliBuildTempPath("simple_build_hello_exec_static");
  const std::string cmd = "bin/svm build -s Tests/simple/hello.simple --out " + out_path.string();
  if (!RunCliBuildCommand(cmd)) return false;
  if (!RunCliBuildCommand(out_path.string())) return false;
#if defined(__linux__)
  const std::string deps = RunCliBuildCaptureStdout("ldd " + out_path.string());
  if (deps.empty()) return false;
  if (deps.find("libsimplevm_runtime.so") != std::string::npos) return false;
#endif
  std::filesystem::remove(out_path);
  return true;
}

bool CliBuildEmitIr() {
  const auto out_path = CliBuildTempPath("simple_cli_emit_ir.sir");
  const std::string cmd = "bin/svm emit -ir Tests/simple/hello.simple --out " + out_path.string();
  if (!RunCliBuildCommand(cmd)) return false;
  std::ifstream in(out_path);
  if (!in) return false;
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove(out_path);
  return contents.find("func") != std::string::npos;
}

bool CliBuildEmitSbc() {
  const auto out_path = CliBuildTempPath("simple_cli_emit_sbc.sbc");
  const std::string cmd = "bin/svm emit -sbc Tests/simple/hello.simple --out " + out_path.string();
  if (!RunCliBuildCommand(cmd)) return false;
  std::ifstream in(out_path, std::ios::binary);
  const bool ok = in.good() && in.peek() != std::ifstream::traits_type::eof();
  std::filesystem::remove(out_path);
  return ok;
}

bool CliBuildCheckSimple() {
  return RunCliBuildCommand("bin/svm check Tests/simple/hello.simple");
}

bool CliBuildCheckSir() {
  return RunCliBuildCommand("bin/svm check Tests/sir/fib_iter.sir");
}

bool CliBuildCheckSbc() {
  return RunCliBuildCommand("bin/svm check Tests/tests/fixtures/add_i32.sbc");
}

bool CliBuildSimpleToSbc() {
  const auto out_path = CliBuildTempPath("simple_cli_build_hello.sbc");
  const std::string cmd = "bin/svm build Tests/simple/hello.simple --out " + out_path.string();
  if (!RunCliBuildCommand(cmd)) return false;
  std::ifstream in(out_path, std::ios::binary);
  const bool ok = in.good() && in.peek() != std::ifstream::traits_type::eof();
  std::filesystem::remove(out_path);
  return ok;
}

bool CliSplitBuildWritesEmbeddedRunner() {
  const auto path = std::filesystem::temp_directory_path() / "simple_cli_embedded_runner_test.cpp";
  std::string error;
  const bool ok = Simple::CLI::WriteEmbeddedRunner(path.string(), {0x01, 0x02, 0x03}, &error);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  return ok && text.find("kSbcData") != std::string::npos && text.find("0x1") != std::string::npos;
}

bool CliSplitBuildComputesDefaultOutputs() {
  return Simple::CLI::DefaultBuildOutputPath("src/main.simple", false) == "src/main.sbc" &&
         Simple::CLI::DefaultBuildOutputPath("src/main.simple", true) == "src/main" &&
         Simple::CLI::ReplaceExtension("build/out.sir", ".sbc") == "build/out.sbc";
}

const TestCase kCliBuildTests[] = {
  {"cli_compile_svm_defaults_to_exe_and_infers_simple_ext", CliCompileSvmDefaultsToExeAndInfersSimpleExt},
  {"cli_compile_svm_out_sbc_stays_bytecode", CliCompileSvmOutSbcStaysBytecode},
  {"cli_build_dynamic_exe", CliBuildDynamicExe},
  {"cli_build_static_exe", CliBuildStaticExe},
  {"cli_build_emit_ir", CliBuildEmitIr},
  {"cli_build_emit_sbc", CliBuildEmitSbc},
  {"cli_build_check_simple", CliBuildCheckSimple},
  {"cli_build_check_sir", CliBuildCheckSir},
  {"cli_build_check_sbc", CliBuildCheckSbc},
  {"cli_build_simple_to_sbc", CliBuildSimpleToSbc},
  {"cli_split_build_writes_embedded_runner", CliSplitBuildWritesEmbeddedRunner},
  {"cli_split_build_computes_default_outputs", CliSplitBuildComputesDefaultOutputs},
};

const TestSection kCliBuildSections[] = {
  {"cli_build", kCliBuildTests, sizeof(kCliBuildTests) / sizeof(kCliBuildTests[0])},
};

} // namespace

const TestSection* GetCliBuildSections(size_t* count) {
  if (count) *count = sizeof(kCliBuildSections) / sizeof(kCliBuildSections[0]);
  return kCliBuildSections;
}

} // namespace Simple::VM::Tests
