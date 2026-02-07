#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Simple::VM::Tests {

struct TestCase {
  const char* name;
  bool (*fn)();
};

struct TestSection {
  const char* name;
  const TestCase* tests;
  size_t count;
};

struct TestResult {
  size_t total = 0;
  size_t failed = 0;
};

void SetEnvVar(const std::string& name, const std::string& value);
void UnsetEnvVar(const std::string& name);

void WriteU32Payload(std::vector<uint8_t>& payload, size_t offset, uint32_t value);
void AppendF32(std::vector<uint8_t>& out, float v);
void AppendF64(std::vector<uint8_t>& out, double v);
void AppendConstBlob(std::vector<uint8_t>& pool, uint32_t kind, const std::vector<uint8_t>& blob, uint32_t* out_const_id);
void PatchRel32(std::vector<uint8_t>& out, size_t operand_offset, size_t target_offset);
size_t Align4(size_t v);

bool ExpectSbcEqual(const std::vector<uint8_t>& got,
                    const std::vector<uint8_t>& expected,
                    const char* name);

std::vector<uint8_t> BuildDebugSection(uint32_t file_count,
                                       uint32_t line_count,
                                       uint32_t sym_count,
                                       uint32_t reserved,
                                       uint32_t method_id,
                                       uint32_t code_offset,
                                       uint32_t file_id,
                                       uint32_t line,
                                       uint32_t column);

std::vector<uint8_t> BuildModuleWithDebugSection(const std::vector<uint8_t>& code,
                                                 const std::vector<uint8_t>& debug_bytes);
std::vector<uint8_t> BuildJmpTableModule(int32_t index);

bool RunExpectTrap(const std::vector<uint8_t>& module_bytes, const char* name);
bool RunExpectTrapNoVerify(const std::vector<uint8_t>& module_bytes, const char* name);
bool RunExpectVerifyFail(const std::vector<uint8_t>& module_bytes, const char* name);
bool RunExpectExit(const std::vector<uint8_t>& module_bytes, int32_t expected);

TestResult RunSection(const TestSection& section);
TestResult RunAllSections(const TestSection* sections, size_t count);

} // namespace Simple::VM::Tests
