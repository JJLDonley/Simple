#include "test_utils.h"

#include "runtime/runtime_limits.h"

namespace Simple::VM::Tests {
namespace {

bool VmSplitRuntimeLimitsChecksSequencesAndDepth() {
  Simple::VM::RuntimeLimits limits;
  limits.max_array_list_size = 3;
  limits.max_call_depth = 2;
  return Simple::VM::Runtime::CheckSequenceLimit(limits, 3) &&
         !Simple::VM::Runtime::CheckSequenceLimit(limits, 4) &&
         Simple::VM::Runtime::CheckCallDepthLimit(limits, 0) &&
         !Simple::VM::Runtime::CheckCallDepthLimit(limits, 1);
}

const TestCase kVmRuntimeLimitsTests[] = {
  {"vm_split_runtime_limits_checks_sequences_and_depth", VmSplitRuntimeLimitsChecksSequencesAndDepth},
};

const TestSection kVmRuntimeLimitsSections[] = {
  {"vm_runtime_limits", kVmRuntimeLimitsTests, sizeof(kVmRuntimeLimitsTests) / sizeof(kVmRuntimeLimitsTests[0])},
};

} // namespace

const TestSection* GetVmRuntimeLimitsSections(size_t* count) {
  if (count) *count = sizeof(kVmRuntimeLimitsSections) / sizeof(kVmRuntimeLimitsSections[0]);
  return kVmRuntimeLimitsSections;
}

} // namespace Simple::VM::Tests
