#ifndef SIMPLE_VM_RUNTIME_EXECUTION_STATS_H
#define SIMPLE_VM_RUNTIME_EXECUTION_STATS_H

#include <cstdint>
#include <vector>

#include "vm.h"

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
                                const std::vector<uint32_t>& jit_tier1_exec_counts);

} // namespace Simple::VM::Runtime

#endif // SIMPLE_VM_RUNTIME_EXECUTION_STATS_H
