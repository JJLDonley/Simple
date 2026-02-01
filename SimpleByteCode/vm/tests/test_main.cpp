#include <string>
#include <vector>

#include "test_utils.h"

namespace simplevm::tests {

const TestSection* GetCoreSections(size_t* count);
const TestSection* GetIrSections(size_t* count);
const TestSection* GetJitSections(size_t* count);
int RunBenchLoop(size_t iterations);

} // namespace simplevm::tests

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--bench") {
    size_t iterations = 1000;
    if (argc > 2) {
      iterations = static_cast<size_t>(std::stoul(argv[2]));
    }
    return simplevm::tests::RunBenchLoop(iterations);
  }

  size_t core_count = 0;
  size_t ir_count = 0;
  size_t jit_count = 0;
  const simplevm::tests::TestSection* core_sections = simplevm::tests::GetCoreSections(&core_count);
  const simplevm::tests::TestSection* ir_sections = simplevm::tests::GetIrSections(&ir_count);
  const simplevm::tests::TestSection* jit_sections = simplevm::tests::GetJitSections(&jit_count);

  std::vector<simplevm::tests::TestSection> sections;
  sections.insert(sections.end(), core_sections, core_sections + core_count);
  sections.insert(sections.end(), ir_sections, ir_sections + ir_count);
  sections.insert(sections.end(), jit_sections, jit_sections + jit_count);

  simplevm::tests::TestResult result = simplevm::tests::RunAllSections(sections.data(), sections.size());
  return result.failed == 0 ? 0 : 1;
}
