#include "test_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opcode.h"
#include "sbc_emitter.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "vm.h"

namespace simplevm::tests {

using simplevm::sbc::AppendU32;
using simplevm::sbc::AppendU64;
using simplevm::sbc::AppendStringToPool;
using simplevm::sbc::AppendU8;
using simplevm::sbc::AppendU16;
using simplevm::sbc::BuildModuleWithTablesAndSigAndDebug;
using simplevm::sbc::BuildModuleWithTables;
using simplevm::sbc::ReadU32At;
using simplevm::sbc::WriteU32;
using simplevm::sbc::AppendI32;

void SetEnvVar(const std::string& name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name.c_str(), value.c_str());
#else
  setenv(name.c_str(), value.c_str(), 1);
#endif
}

void UnsetEnvVar(const std::string& name) {
#ifdef _WIN32
  _putenv_s(name.c_str(), "");
#else
  unsetenv(name.c_str());
#endif
}

void WriteU32Payload(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void AppendF32(std::vector<uint8_t>& out, float v) {
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  AppendU32(out, bits);
}

void AppendF64(std::vector<uint8_t>& out, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  AppendU64(out, bits);
}

void AppendConstBlob(std::vector<uint8_t>& pool, uint32_t kind, const std::vector<uint8_t>& blob, uint32_t* out_const_id) {
  uint32_t const_id = static_cast<uint32_t>(pool.size());
  AppendU32(pool, kind);
  uint32_t blob_offset = static_cast<uint32_t>(pool.size() + 4);
  AppendU32(pool, blob_offset);
  AppendU32(pool, static_cast<uint32_t>(blob.size()));
  pool.insert(pool.end(), blob.begin(), blob.end());
  *out_const_id = const_id;
}

void PatchRel32(std::vector<uint8_t>& out, size_t operand_offset, size_t target_offset) {
  size_t next_pc = operand_offset + 4;
  int32_t rel = static_cast<int32_t>(static_cast<int64_t>(target_offset) - static_cast<int64_t>(next_pc));
  WriteU32(out, operand_offset, static_cast<uint32_t>(rel));
}

size_t Align4(size_t v) {
  return (v + 3u) & ~static_cast<size_t>(3u);
}

bool ExpectSbcEqual(const std::vector<uint8_t>& got,
                    const std::vector<uint8_t>& expected,
                    const char* name) {
  if (got == expected) {
    return true;
  }
  std::cerr << "expected SBC mismatch: " << name << "\n";
  std::cerr << "  expected size: " << expected.size() << "\n";
  std::cerr << "  got size: " << got.size() << "\n";
  const size_t min_size = std::min(expected.size(), got.size());
  for (size_t i = 0; i < min_size; ++i) {
    if (expected[i] != got[i]) {
      std::cerr << "  first diff at byte " << i
                << " expected=0x" << std::hex << static_cast<int>(expected[i])
                << " got=0x" << static_cast<int>(got[i]) << std::dec << "\n";
      break;
    }
  }
  return false;
}

std::vector<uint8_t> BuildDebugSection(uint32_t file_count,
                                       uint32_t line_count,
                                       uint32_t sym_count,
                                       uint32_t reserved,
                                       uint32_t method_id,
                                       uint32_t code_offset,
                                       uint32_t file_id,
                                       uint32_t line,
                                       uint32_t column) {
  std::vector<uint8_t> out;
  AppendU32(out, file_count);
  AppendU32(out, line_count);
  AppendU32(out, sym_count);
  AppendU32(out, reserved);
  for (uint32_t i = 0; i < file_count; ++i) {
    AppendU32(out, 0);
    AppendU32(out, 0);
  }
  for (uint32_t i = 0; i < line_count; ++i) {
    AppendU32(out, method_id);
    AppendU32(out, code_offset);
    AppendU32(out, file_id);
    AppendU32(out, line);
    AppendU32(out, column);
  }
  for (uint32_t i = 0; i < sym_count; ++i) {
    AppendU32(out, 0);
    AppendU32(out, 0);
    AppendU32(out, 0);
    AppendU32(out, 0);
  }
  return out;
}

std::vector<uint8_t> BuildModuleWithDebugSection(const std::vector<uint8_t>& code,
                                                 const std::vector<uint8_t>& debug_bytes) {
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "");
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSigAndDebug(code, const_pool, {}, {}, debug_bytes, 0, 0, 0, 0, 0, 0,
                                             empty_params);
}

std::vector<uint8_t> BuildJmpTableModule(int32_t index) {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, index);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  size_t const_id_offset = code.size();
  AppendU32(code, 0);
  size_t default_offset = code.size();
  AppendI32(code, 0);
  size_t table_base = code.size();

  size_t case0 = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t case1 = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t default_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  PatchRel32(code, default_offset, default_block);

  std::vector<uint8_t> blob;
  AppendU32(blob, 2);
  AppendI32(blob, static_cast<int32_t>(static_cast<int64_t>(case0) - static_cast<int64_t>(table_base)));
  AppendI32(blob, static_cast<int32_t>(static_cast<int64_t>(case1) - static_cast<int64_t>(table_base)));

  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 6, blob, &const_id);
  WriteU32(code, const_id_offset, const_id);

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

bool RunExpectTrap(const std::vector<uint8_t>& module_bytes, const char* name) {
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << "expected trap: " << name << "\n";
    return false;
  }
  return true;
}

bool RunExpectTrapNoVerify(const std::vector<uint8_t>& module_bytes, const char* name) {
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module, false);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << "expected trap: " << name << "\n";
    return false;
  }
  return true;
}

bool RunExpectVerifyFail(const std::vector<uint8_t>& module_bytes, const char* name) {
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure: " << name << "\n";
    return false;
  }
  return true;
}

bool RunExpectExit(const std::vector<uint8_t>& module_bytes, int32_t expected) {
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.exit_code != expected) {
    std::cerr << "expected " << expected << ", got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

TestResult RunSection(const TestSection& section) {
  std::cout << "section: " << section.name << " (" << section.count << " tests)\n";
  size_t failed = 0;
  for (size_t i = 0; i < section.count; ++i) {
    const TestCase& test = section.tests[i];
    if (!test.fn()) {
      ++failed;
      std::cerr << "failed: " << test.name << "\n";
    }
  }
  std::cout << "section result: " << section.name << " "
            << (section.count - failed) << "/" << section.count << "\n";
  return TestResult{section.count, failed};
}

TestResult RunAllSections(const TestSection* sections, size_t count) {
  TestResult total{};
  for (size_t i = 0; i < count; ++i) {
    TestResult result = RunSection(sections[i]);
    total.total += result.total;
    total.failed += result.failed;
  }
  std::cout << "total tests: " << (total.total - total.failed)
            << "/" << total.total << "\n";
  return total;
}

} // namespace simplevm::tests
