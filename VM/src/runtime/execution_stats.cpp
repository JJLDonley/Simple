#include "runtime/execution_stats.h"

namespace Simple::VM::Runtime {

ExecResult AttachExecutionStats(ExecResult result,
                                const std::vector<JitTier>& jit_tiers,
                                const std::vector<uint32_t>& call_counts,
                                const std::vector<uint64_t>& opcode_counts,
                                const std::vector<uint32_t>& compile_counts,
                                const std::vector<uint32_t>& func_opcode_counts,
                                const std::vector<uint64_t>& compile_ticks_tier0,
                                const std::vector<uint64_t>& compile_ticks_tier1,
                                const std::vector<uint32_t>& jit_dispatch_counts,
                                const std::vector<uint32_t>& jit_compiled_exec_counts,
                                const std::vector<uint32_t>& jit_tier1_exec_counts) {
  result.jit_tiers = jit_tiers;
  result.call_counts = call_counts;
  result.opcode_counts = opcode_counts;
  result.compile_counts = compile_counts;
  result.func_opcode_counts = func_opcode_counts;
  result.compile_ticks_tier0 = compile_ticks_tier0;
  result.compile_ticks_tier1 = compile_ticks_tier1;
  result.jit_dispatch_counts = jit_dispatch_counts;
  result.jit_compiled_exec_counts = jit_compiled_exec_counts;
  result.jit_tier1_exec_counts = jit_tier1_exec_counts;
  return result;
}

} // namespace Simple::VM::Runtime
