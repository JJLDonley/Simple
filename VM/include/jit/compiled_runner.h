#ifndef SIMPLE_VM_JIT_COMPILED_RUNNER_H
#define SIMPLE_VM_JIT_COMPILED_RUNNER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "heap.h"
#include "interpreter/stack.h"
#include "jit/compile_policy.h"
#include "jit/tier_updater.h"
#include "sbc_types.h"
#include "vm.h"

namespace Simple::VM::Jit {

struct CompiledRunContext {
  const Simple::Byte::SbcModule* module = nullptr;
  Heap* heap = nullptr;
  const std::vector<JitTier>* jit_tiers = nullptr;
  TierUpdater* update_tier = nullptr;
  CompilePredicate* can_compile = nullptr;
  std::vector<uint32_t>* jit_compiled_exec_counts = nullptr;
  std::vector<uint32_t>* jit_tier1_exec_counts = nullptr;
};

bool RunCompiledFunction(CompiledRunContext& context,
                         size_t func_index,
                         const std::vector<Simple::VM::Interpreter::Slot>& args,
                         Simple::VM::Interpreter::Slot& out_ret,
                         bool& out_has_ret,
                         std::string& error);

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_COMPILED_RUNNER_H
