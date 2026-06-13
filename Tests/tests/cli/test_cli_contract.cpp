#include "test_utils.h"

#include "command_contract.h"

namespace Simple::VM::Tests {
namespace {

bool CliSplitContractClassifiesInputExtensions() {
  return Simple::CLI::IsSimpleSourcePath("game.simple") &&
         Simple::CLI::IsSirPath("build/out.sir") &&
         Simple::CLI::IsSbcPath("module.sbc") &&
         !Simple::CLI::IsSimpleSourcePath("module.sbc");
}

const TestCase kCliContractTests[] = {
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
