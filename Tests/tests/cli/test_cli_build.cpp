#include "test_utils.h"

#include "command_contract.h"

namespace Simple::VM::Tests {
namespace {

bool CliSplitBuildComputesDefaultOutputs() {
  return Simple::CLI::DefaultBuildOutputPath("src/main.simple", false) == "src/main.sbc" &&
         Simple::CLI::DefaultBuildOutputPath("src/main.simple", true) == "src/main" &&
         Simple::CLI::ReplaceExtension("build/out.sir", ".sbc") == "build/out.sbc";
}

const TestCase kCliBuildTests[] = {
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
