#include <iostream>
#include <string>
#include <vector>

#include "test_utils.h"

namespace simplevm::tests {

#if defined(TEST_SUITE_CORE)
#define SIMPLEVM_TEST_INCLUDE_CORE 1
#endif
#if defined(TEST_SUITE_IR)
#define SIMPLEVM_TEST_INCLUDE_IR 1
#endif
#if defined(TEST_SUITE_JIT)
#define SIMPLEVM_TEST_INCLUDE_JIT 1
#endif

#if !defined(SIMPLEVM_TEST_INCLUDE_CORE) && !defined(SIMPLEVM_TEST_INCLUDE_IR) && \
    !defined(SIMPLEVM_TEST_INCLUDE_JIT)
#define SIMPLEVM_TEST_INCLUDE_CORE 1
#define SIMPLEVM_TEST_INCLUDE_IR 1
#define SIMPLEVM_TEST_INCLUDE_JIT 1
#endif

#if SIMPLEVM_TEST_INCLUDE_CORE
const TestSection* GetCoreSections(size_t* count);
#endif
#if SIMPLEVM_TEST_INCLUDE_IR
const TestSection* GetIrSections(size_t* count);
#endif
#if SIMPLEVM_TEST_INCLUDE_JIT
const TestSection* GetJitSections(size_t* count);
int RunBenchLoop(size_t iterations);
#endif

} // namespace simplevm::tests

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--bench") {
#if SIMPLEVM_TEST_INCLUDE_JIT
    size_t iterations = 1000;
    if (argc > 2) {
      iterations = static_cast<size_t>(std::stoul(argv[2]));
    }
    return simplevm::tests::RunBenchLoop(iterations);
#else
    std::cerr << "--bench is only available in the JIT test suite\n";
    return 2;
#endif
  }

  std::vector<simplevm::tests::TestSection> sections;
#if SIMPLEVM_TEST_INCLUDE_CORE
  size_t core_count = 0;
  const simplevm::tests::TestSection* core_sections = simplevm::tests::GetCoreSections(&core_count);
  sections.insert(sections.end(), core_sections, core_sections + core_count);
#endif
#if SIMPLEVM_TEST_INCLUDE_IR
  size_t ir_count = 0;
  const simplevm::tests::TestSection* ir_sections = simplevm::tests::GetIrSections(&ir_count);
  sections.insert(sections.end(), ir_sections, ir_sections + ir_count);
#endif
#if SIMPLEVM_TEST_INCLUDE_JIT
  size_t jit_count = 0;
  const simplevm::tests::TestSection* jit_sections = simplevm::tests::GetJitSections(&jit_count);
  sections.insert(sections.end(), jit_sections, jit_sections + jit_count);
#endif

  simplevm::tests::TestResult result = simplevm::tests::RunAllSections(sections.data(), sections.size());
  return result.failed == 0 ? 0 : 1;
}
