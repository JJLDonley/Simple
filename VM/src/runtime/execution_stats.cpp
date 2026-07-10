#include "runtime/execution_stats.h"

#include "jit/status.h"

#include <cstddef>

namespace Simple::VM::Runtime {
namespace {

constexpr size_t kJitStatusCount = 5;

size_t JitStatusIndex(Simple::VM::Jit::JitStatusCode code) {
  return static_cast<size_t>(code);
}

} // namespace

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
                                const std::vector<uint32_t>& jit_tier1_exec_counts,
                                const std::vector<uint32_t>& llvm_reject_counts,
                                const std::vector<std::string>& llvm_reject_reasons) {
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
  result.llvm_reject_counts = llvm_reject_counts;
  result.llvm_reject_reasons = llvm_reject_reasons;
  result.jit_status_counts.assign(kJitStatusCount, 0);
  for (uint32_t count : jit_compiled_exec_counts) {
    result.jit_status_counts[JitStatusIndex(Simple::VM::Jit::JitStatusCode::Return)] += count;
  }
  for (size_t i = 0; i < llvm_reject_counts.size(); ++i) {
    const char* reason = i < llvm_reject_reasons.size() ? llvm_reject_reasons[i].c_str() : "";
    const auto code = Simple::VM::Jit::ClassifyJitReason(reason);
    result.jit_status_counts[JitStatusIndex(code)] += llvm_reject_counts[i];
  }
  return result;
}

} // namespace Simple::VM::Runtime
