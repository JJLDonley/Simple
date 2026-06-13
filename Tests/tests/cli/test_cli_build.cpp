#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "build_contract.h"
#include "command_contract.h"

namespace Simple::VM::Tests {
namespace {

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
