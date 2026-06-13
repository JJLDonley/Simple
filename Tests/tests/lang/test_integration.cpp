#include "test_utils.h"

#include "simple_runner.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitIntegrationRunsSimpleFixture() {
  return RunSimpleFile("Tests/simple/sum_loop.simple", true) == 4950;
}

const TestCase kLangIntegrationTests[] = {
  {"lang_split_integration_runs_simple_fixture", LangSplitIntegrationRunsSimpleFixture},
};

const TestSection kLangIntegrationSections[] = {
  {"lang_integration", kLangIntegrationTests, sizeof(kLangIntegrationTests) / sizeof(kLangIntegrationTests[0])},
};

} // namespace

const TestSection* GetLangIntegrationSections(size_t* count) {
  if (count) *count = sizeof(kLangIntegrationSections) / sizeof(kLangIntegrationSections[0]);
  return kLangIntegrationSections;
}

} // namespace Simple::VM::Tests
