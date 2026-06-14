#include "test_utils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

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
