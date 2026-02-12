#include <iostream>
#include <string>
#include <vector>

#include "simple_runner.h"
#include "sir_runner.h"
#include "test_utils.h"

namespace Simple::VM::Tests {

#if defined(TEST_SUITE_CORE)
#define SIMPLEVM_TEST_INCLUDE_CORE 1
#endif
#if defined(TEST_SUITE_IR)
#define SIMPLEVM_TEST_INCLUDE_IR 1
#endif
#if defined(TEST_SUITE_JIT)
#define SIMPLEVM_TEST_INCLUDE_JIT 1
#endif
#if defined(TEST_SUITE_LANG)
#define SIMPLEVM_TEST_INCLUDE_LANG 1
#endif
#if defined(TEST_SUITE_LSP)
#define SIMPLEVM_TEST_INCLUDE_LSP 1
#endif

#if !defined(SIMPLEVM_TEST_INCLUDE_CORE) && !defined(SIMPLEVM_TEST_INCLUDE_IR) && \
    !defined(SIMPLEVM_TEST_INCLUDE_JIT) && !defined(SIMPLEVM_TEST_INCLUDE_LANG) && \
    !defined(SIMPLEVM_TEST_INCLUDE_LSP)
#define SIMPLEVM_TEST_INCLUDE_CORE 1
#define SIMPLEVM_TEST_INCLUDE_IR 1
#define SIMPLEVM_TEST_INCLUDE_JIT 1
#define SIMPLEVM_TEST_INCLUDE_LANG 1
#define SIMPLEVM_TEST_INCLUDE_LSP 1
#endif

#if SIMPLEVM_TEST_INCLUDE_CORE
const TestSection* GetCoreSections(size_t* count);
const TestSection* GetRuntimeSmokeSections(size_t* count);
#endif
#if SIMPLEVM_TEST_INCLUDE_IR
const TestSection* GetIrSections(size_t* count);
#endif
#if SIMPLEVM_TEST_INCLUDE_JIT
const TestSection* GetJitSections(size_t* count);
int RunBenchLoop(size_t iterations);
int RunBenchHotLoop(size_t iterations);
#endif
#if SIMPLEVM_TEST_INCLUDE_LANG
const TestSection* GetLangSections(size_t* count);
#endif
#if SIMPLEVM_TEST_INCLUDE_LSP
const TestSection* GetLspSections(size_t* count);
#endif

} // namespace Simple::VM::Tests

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--sir") {
    if (argc < 3) {
      std::cerr << "usage: simplevm_tests --sir <file.sir> [--no-verify]\n";
      return 2;
    }
    const std::string path = argv[2];
    bool verify = true;
    if (argc > 3 && std::string(argv[3]) == "--no-verify") {
      verify = false;
    }
    return Simple::VM::Tests::RunSirFile(path, verify);
  }
  if (argc > 1 && std::string(argv[1]) == "--simple") {
    if (argc < 3) {
      std::cerr << "usage: simplevm_tests --simple <file.simple> [--no-verify]\n";
      return 2;
    }
    const std::string path = argv[2];
    bool verify = true;
    if (argc > 3 && std::string(argv[3]) == "--no-verify") {
      verify = false;
    }
    return Simple::VM::Tests::RunSimpleFile(path, verify);
  }
  if (argc > 1 && std::string(argv[1]) == "--perf") {
    if (argc < 3) {
      std::cerr << "usage: simplevm_tests --perf <dir> [--iters N] [--no-verify]\n";
      return 2;
    }
    const std::string dir = argv[2];
    size_t iterations = 100;
    bool verify = true;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--no-verify") {
        verify = false;
        continue;
      }
      if (arg == "--iters" && i + 1 < argc) {
        try {
          iterations = static_cast<size_t>(std::stoul(argv[i + 1]));
        } catch (...) {
          std::cerr << "invalid --iters value\n";
          return 2;
        }
        ++i;
        continue;
      }
      try {
        iterations = static_cast<size_t>(std::stoul(arg));
      } catch (...) {
        std::cerr << "usage: simplevm_tests --perf <dir> [--iters N] [--no-verify]\n";
        return 2;
      }
    }
    return Simple::VM::Tests::RunSirPerfDir(dir, iterations, verify);
  }
  if (argc > 1 && std::string(argv[1]) == "--simple-perf") {
    if (argc < 3) {
      std::cerr << "usage: simplevm_tests --simple-perf <dir> [--iters N] [--no-verify]\n";
      return 2;
    }
    const std::string dir = argv[2];
    size_t iterations = 100;
    bool verify = true;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--no-verify") {
        verify = false;
        continue;
      }
      if (arg == "--iters" && i + 1 < argc) {
        try {
          iterations = static_cast<size_t>(std::stoul(argv[i + 1]));
        } catch (...) {
          std::cerr << "invalid --iters value\n";
          return 2;
        }
        ++i;
        continue;
      }
      try {
        iterations = static_cast<size_t>(std::stoul(arg));
      } catch (...) {
        std::cerr << "usage: simplevm_tests --simple-perf <dir> [--iters N] [--no-verify]\n";
        return 2;
      }
    }
    return Simple::VM::Tests::RunSimplePerfDir(dir, iterations, verify);
  }

  if (argc > 1 && std::string(argv[1]) == "--bench") {
#if SIMPLEVM_TEST_INCLUDE_JIT
    size_t iterations = 1000;
    if (argc > 2) {
      iterations = static_cast<size_t>(std::stoul(argv[2]));
    }
    return Simple::VM::Tests::RunBenchLoop(iterations);
#else
    std::cerr << "--bench is only available in the JIT test suite\n";
    return 2;
#endif
  }
  if (argc > 1 && std::string(argv[1]) == "--bench-hot") {
#if SIMPLEVM_TEST_INCLUDE_JIT
    size_t iterations = 1000;
    if (argc > 2) {
      iterations = static_cast<size_t>(std::stoul(argv[2]));
    }
    return Simple::VM::Tests::RunBenchHotLoop(iterations);
#else
    std::cerr << "--bench-hot is only available in the JIT test suite\n";
    return 2;
#endif
  }
  if (argc > 1 && std::string(argv[1]) == "--smoke") {
    std::vector<Simple::VM::Tests::TestSection> sections;
#if SIMPLEVM_TEST_INCLUDE_CORE
    size_t smoke_count = 0;
    const Simple::VM::Tests::TestSection* smoke_sections =
        Simple::VM::Tests::GetRuntimeSmokeSections(&smoke_count);
    sections.insert(sections.end(), smoke_sections, smoke_sections + smoke_count);
#endif
    Simple::VM::Tests::TestResult result = Simple::VM::Tests::RunAllSections(sections.data(),
                                                                            sections.size());
    return result.failed == 0 ? 0 : 1;
  }

  std::vector<Simple::VM::Tests::TestSection> sections;
#if SIMPLEVM_TEST_INCLUDE_CORE
  size_t core_count = 0;
  const Simple::VM::Tests::TestSection* core_sections = Simple::VM::Tests::GetCoreSections(&core_count);
  sections.insert(sections.end(), core_sections, core_sections + core_count);
#endif
#if SIMPLEVM_TEST_INCLUDE_IR
  size_t ir_count = 0;
  const Simple::VM::Tests::TestSection* ir_sections = Simple::VM::Tests::GetIrSections(&ir_count);
  sections.insert(sections.end(), ir_sections, ir_sections + ir_count);
#endif
#if SIMPLEVM_TEST_INCLUDE_JIT
  size_t jit_count = 0;
  const Simple::VM::Tests::TestSection* jit_sections = Simple::VM::Tests::GetJitSections(&jit_count);
  sections.insert(sections.end(), jit_sections, jit_sections + jit_count);
#endif
#if SIMPLEVM_TEST_INCLUDE_LANG
  size_t lang_count = 0;
  const Simple::VM::Tests::TestSection* lang_sections = Simple::VM::Tests::GetLangSections(&lang_count);
  sections.insert(sections.end(), lang_sections, lang_sections + lang_count);
#endif
#if SIMPLEVM_TEST_INCLUDE_LSP
  size_t lsp_count = 0;
  const Simple::VM::Tests::TestSection* lsp_sections = Simple::VM::Tests::GetLspSections(&lsp_count);
  sections.insert(sections.end(), lsp_sections, lsp_sections + lsp_count);
#endif

  Simple::VM::Tests::TestResult result = Simple::VM::Tests::RunAllSections(sections.data(), sections.size());
  return result.failed == 0 ? 0 : 1;
}
