#include "test_utils.h"

#include "jit/jit_scaffold.h"

namespace Simple::VM::Tests {
namespace {

bool VmSplitJitClassifiesKindsAndThresholds() {
  return Simple::VM::Jit::IsScalarKind(Simple::Byte::TypeKind::I32) &&
         Simple::VM::Jit::IsValueKind(Simple::Byte::TypeKind::String) &&
         !Simple::VM::Jit::IsScalarKind(Simple::Byte::TypeKind::String) &&
         Simple::VM::Jit::ReadThreshold("SIMPLEVM_TEST_THRESHOLD_MISSING", 1234) == 1234;
}

const TestCase kVmJitTests[] = {
  {"vm_split_jit_classifies_kinds_and_thresholds", VmSplitJitClassifiesKindsAndThresholds},
};

const TestSection kVmJitSections[] = {
  {"vm_jit", kVmJitTests, sizeof(kVmJitTests) / sizeof(kVmJitTests[0])},
};

} // namespace

const TestSection* GetVmJitSections(size_t* count) {
  if (count) *count = sizeof(kVmJitSections) / sizeof(kVmJitSections[0]);
  return kVmJitSections;
}

} // namespace Simple::VM::Tests
