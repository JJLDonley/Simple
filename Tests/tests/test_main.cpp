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
const TestSection* GetVmGcSections(size_t* count);
const TestSection* GetVmHeapSections(size_t* count);
const TestSection* GetVmInterpreterSections(size_t* count);
const TestSection* GetVmJitSections(size_t* count);
const TestSection* GetVmNativeChannelSections(size_t* count);
const TestSection* GetVmNativeFsSections(size_t* count);
const TestSection* GetVmRuntimeAbiSections(size_t* count);
const TestSection* GetVmRuntimeLimitsSections(size_t* count);
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
const TestSection* GetLangAstSections(size_t* count);
const TestSection* GetLangCastSections(size_t* count);
const TestSection* GetLangIntegrationSections(size_t* count);
const TestSection* GetLangIrbSections(size_t* count);
const TestSection* GetLangIreSections(size_t* count);
const TestSection* GetLangLexerSections(size_t* count);
const TestSection* GetLangRastSections(size_t* count);
const TestSection* GetLangTastSections(size_t* count);
#endif
const TestSection* GetCliBuildSections(size_t* count);
const TestSection* GetCliContractSections(size_t* count);
const TestSection* GetCliDiagnosticsSections(size_t* count);
const TestSection* GetCliImportsSections(size_t* count);
#if SIMPLEVM_TEST_INCLUDE_LSP
const TestSection* GetLspSections(size_t* count);
#endif
const TestSection* GetAuditSections(size_t* count);

using TestSectionGetter = const TestSection* (*)(size_t*);

void AppendSections(std::vector<TestSection>& sections, TestSectionGetter getter) {
  size_t count = 0;
  const TestSection* next = getter(&count);
  sections.insert(sections.end(), next, next + count);
}

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
    Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetRuntimeSmokeSections);
#endif
    Simple::VM::Tests::TestResult result = Simple::VM::Tests::RunAllSections(sections.data(),
                                                                            sections.size());
    return result.failed == 0 ? 0 : 1;
  }

  std::vector<Simple::VM::Tests::TestSection> sections;
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetAuditSections);
#if SIMPLEVM_TEST_INCLUDE_CORE
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetCoreSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmGcSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmHeapSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmInterpreterSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmJitSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmNativeChannelSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmNativeFsSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmRuntimeAbiSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetVmRuntimeLimitsSections);
#endif
#if SIMPLEVM_TEST_INCLUDE_IR
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetIrSections);
#endif
#if SIMPLEVM_TEST_INCLUDE_JIT
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetJitSections);
#endif
#if SIMPLEVM_TEST_INCLUDE_LANG
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangAstSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangCastSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangIntegrationSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangIrbSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangIreSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangLexerSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangRastSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLangTastSections);
#endif
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetCliBuildSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetCliContractSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetCliDiagnosticsSections);
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetCliImportsSections);
#if SIMPLEVM_TEST_INCLUDE_LSP
  Simple::VM::Tests::AppendSections(sections, Simple::VM::Tests::GetLspSections);
#endif

  Simple::VM::Tests::TestResult result = Simple::VM::Tests::RunAllSections(sections.data(), sections.size());
  return result.failed == 0 ? 0 : 1;
}
