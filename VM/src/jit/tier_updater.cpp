#include "jit/tier_updater.h"

namespace Simple::VM::Jit {

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
                           const CanCompileFunction& can_compile) {
  if (!enable_jit) return;
  if (func_index >= call_counts.size()) return;
  const uint32_t count = ++call_counts[func_index];
  if (count >= tier1_threshold) {
    if (jit_tiers[func_index] != JitTier::Tier1) {
      jit_tiers[func_index] = JitTier::Tier1;
      jit_stubs[func_index].active = true;
      jit_stubs[func_index].compiled = jit_stubs[func_index].disabled ? false : can_compile(func_index);
      compile_counts[func_index] += 1;
      compile_ticks_tier1[func_index] = ++compile_tick;
    }
  } else if (count >= tier0_threshold) {
    if (jit_tiers[func_index] == JitTier::None) {
      jit_tiers[func_index] = JitTier::Tier0;
      jit_stubs[func_index].active = true;
      jit_stubs[func_index].compiled = jit_stubs[func_index].disabled ? false : can_compile(func_index);
      compile_counts[func_index] += 1;
      compile_ticks_tier0[func_index] = ++compile_tick;
    }
  }
}

void TierUpdater::operator()(size_t func_index) const {
  if (!call_counts || !jit_tiers || !jit_stubs || !compile_counts ||
      !compile_ticks_tier0 || !compile_ticks_tier1 || !compile_tick || !can_compile) {
    return;
  }
  UpdateTierForFunction(enable_jit,
                        func_index,
                        tier0_threshold,
                        tier1_threshold,
                        *call_counts,
                        *jit_tiers,
                        *jit_stubs,
                        *compile_counts,
                        *compile_ticks_tier0,
                        *compile_ticks_tier1,
                        *compile_tick,
                        can_compile);
}

} // namespace Simple::VM::Jit
