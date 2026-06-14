#ifndef SIMPLE_VM_JIT_TIER_UPDATER_H
#define SIMPLE_VM_JIT_TIER_UPDATER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "jit/jit_scaffold.h"
#include "vm.h"

namespace Simple::VM::Jit {

using CanCompileFunction = std::function<bool(size_t)>;

void UpdateTierForFunction(bool enable_jit,
                           size_t func_index,
                           uint32_t tier0_threshold,
                           uint32_t tier1_threshold,
                           std::vector<uint32_t>& call_counts,
                           std::vector<JitTier>& jit_tiers,
                           std::vector<Stub>& jit_stubs,
                           std::vector<uint32_t>& compile_counts,
                           std::vector<uint64_t>& compile_ticks_tier0,
                           std::vector<uint64_t>& compile_ticks_tier1,
                           uint64_t& compile_tick,
                           const CanCompileFunction& can_compile);

struct TierUpdater {
  bool enable_jit = false;
  uint32_t tier0_threshold = 0;
  uint32_t tier1_threshold = 0;
  std::vector<uint32_t>* call_counts = nullptr;
  std::vector<JitTier>* jit_tiers = nullptr;
  std::vector<Stub>* jit_stubs = nullptr;
  std::vector<uint32_t>* compile_counts = nullptr;
  std::vector<uint64_t>* compile_ticks_tier0 = nullptr;
  std::vector<uint64_t>* compile_ticks_tier1 = nullptr;
  uint64_t* compile_tick = nullptr;
  CanCompileFunction can_compile;

  void operator()(size_t func_index) const;
};

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_TIER_UPDATER_H
