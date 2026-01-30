#ifndef SIMPLE_VM_H
#define SIMPLE_VM_H

#include <cstdint>
#include <string>
#include <vector>

#include "sbc_types.h"

namespace simplevm {

enum class ExecStatus {
  Ok,
  Halted,
  Trapped,
  BadModule,
};

enum class JitTier {
  None,
  Tier0,
  Tier1,
};

constexpr uint32_t kJitTier0Threshold = 3;
constexpr uint32_t kJitTier1Threshold = 6;
constexpr uint32_t kJitOpcodeThreshold = 10;

struct ExecResult {
  ExecStatus status = ExecStatus::Ok;
  std::string error;
  int32_t exit_code = 0;
  std::vector<JitTier> jit_tiers;
  std::vector<uint32_t> call_counts;
  std::vector<uint64_t> opcode_counts;
  std::vector<uint32_t> compile_counts;
  std::vector<uint32_t> func_opcode_counts;
  std::vector<uint64_t> compile_ticks_tier0;
  std::vector<uint64_t> compile_ticks_tier1;
};

ExecResult ExecuteModule(const SbcModule& module);
ExecResult ExecuteModule(const SbcModule& module, bool verify);

} // namespace simplevm

#endif // SIMPLE_VM_H
