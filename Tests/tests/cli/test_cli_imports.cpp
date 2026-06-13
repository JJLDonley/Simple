#include "test_utils.h"

#include "import_contract.h"

namespace Simple::VM::Tests {
namespace {

bool CliSplitImportsNormalizesSimplePaths() {
  return Simple::CLI::ImportPathWithSimpleExtension("lib/math") == "lib/math.simple" &&
         Simple::CLI::ImportPathWithSimpleExtension("lib/math.simple") == "lib/math.simple" &&
         Simple::CLI::IsExplicitRelativeImportPath("./lib/math") &&
         Simple::CLI::IsExplicitRelativeImportPath("lib/math") &&
         !Simple::CLI::IsExplicitRelativeImportPath("Math");
}

const TestCase kCliImportsTests[] = {
  {"cli_split_imports_normalizes_simple_paths", CliSplitImportsNormalizesSimplePaths},
};

const TestSection kCliImportsSections[] = {
  {"cli_imports", kCliImportsTests, sizeof(kCliImportsTests) / sizeof(kCliImportsTests[0])},
};

} // namespace

const TestSection* GetCliImportsSections(size_t* count) {
  if (count) *count = sizeof(kCliImportsSections) / sizeof(kCliImportsSections[0]);
  return kCliImportsSections;
}

} // namespace Simple::VM::Tests
