#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "build_contract.h"
#include "cli/cli_test_utils.h"
#include "command_contract.h"

namespace Simple::VM::Tests {
namespace {

bool CliCompileSvmDefaultsToExeAndInfersSimpleExt() {
  const auto out_path = TestTempExecutablePath("svm_compile_hello_exec");
  if (RunCliSvm({"compile", "tests/simple/hello", "--out", out_path.string()}) != 0) return false;
  const bool ok = RunProcess(out_path, {}) == 0;
  std::filesystem::remove(out_path);
  return ok;
}

bool CliCompileSvmOutSbcStaysBytecode() {
  const auto out_path = TestTempPath("svm_compile_hello.sbc");
  if (RunCliSvm({"compile", "tests/simple/hello.simple", "--out", out_path.string()}) != 0) return false;
  std::ifstream in(out_path, std::ios::binary);
  const bool ok = in.good() && in.peek() != std::ifstream::traits_type::eof();
  in.close();
  std::filesystem::remove(out_path);
  return ok;
}

bool CliBuildDynamicExe() {
  const auto out_path = TestTempExecutablePath("simple_build_hello_exec");
  if (RunCliSvm({"build", "-d", "tests/simple/hello.simple", "--out", out_path.string()}) != 0) return false;
  if (RunProcess(out_path, {}) != 0) return false;
#if defined(__linux__)
  const std::string deps = RunProcessCaptureStdout(
      "ldd", {out_path.string()}, "simple_cli_build_stdout.txt");
  if (deps.empty()) return false;
  if (deps.find("libsimplevm_runtime.so") == std::string::npos) return false;
#endif
  std::filesystem::remove(out_path);
  return true;
}

bool CliBuildStaticExe() {
  const auto out_path = TestTempExecutablePath("simple_build_hello_exec_static");
  if (RunCliSvm({"build", "-s", "tests/simple/hello.simple", "--out", out_path.string()}) != 0) return false;
  if (RunProcess(out_path, {}) != 0) return false;
#if defined(__linux__)
  const std::string deps = RunProcessCaptureStdout(
      "ldd", {out_path.string()}, "simple_cli_build_stdout.txt");
  if (deps.empty()) return false;
  if (deps.find("libsimplevm_runtime.so") != std::string::npos) return false;
#endif
  std::filesystem::remove(out_path);
  return true;
}

bool CliBuildEmitIr() {
  const auto out_path = TestTempPath("simple_cli_emit_ir.sir");
  if (RunCliSvm({"emit", "-ir", "tests/simple/hello.simple", "--out", out_path.string()}) != 0) return false;
  std::ifstream in(out_path);
  if (!in) return false;
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  std::filesystem::remove(out_path);
  return contents.find("func") != std::string::npos;
}

bool CliBuildEmitSbc() {
  const auto out_path = TestTempPath("simple_cli_emit_sbc.sbc");
  if (RunCliSvm({"emit", "-sbc", "tests/simple/hello.simple", "--out", out_path.string()}) != 0) return false;
  std::ifstream in(out_path, std::ios::binary);
  const bool ok = in.good() && in.peek() != std::ifstream::traits_type::eof();
  in.close();
  std::filesystem::remove(out_path);
  return ok;
}

bool CliBuildCheckSimple() {
  return RunCliSvm({"check", "tests/simple/hello.simple"}) == 0;
}

bool CliBuildCheckSir() {
  return RunCliSvm({"check", "tests/sir/fib_iter.sir"}) == 0;
}

bool CliBuildCheckSbc() {
  return RunCliSvm({"check", "tests/tests/fixtures/add_i32.sbc"}) == 0;
}

bool CliBuildSimpleToSbc() {
  const auto out_path = TestTempPath("simple_cli_build_hello.sbc");
  if (RunCliSvm({"build", "tests/simple/hello.simple", "--out", out_path.string()}) != 0) return false;
  std::ifstream in(out_path, std::ios::binary);
  const bool ok = in.good() && in.peek() != std::ifstream::traits_type::eof();
  in.close();
  std::filesystem::remove(out_path);
  return ok;
}

bool CliSplitBuildWritesEmbeddedRunner() {
  const auto path = TestTempPath("simple_cli_embedded_runner_test.cpp");
  std::string error;
  const bool ok = Simple::CLI::WriteEmbeddedRunner(path.string(), {0x01, 0x02, 0x03}, &error);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
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
