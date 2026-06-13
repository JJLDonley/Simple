#include "test_utils.h"

#include <cstdint>
#include <vector>

#include "gc/root_tracer.h"

namespace Simple::VM::Tests {
namespace {

bool VmSplitGcTracesGlobalRoots() {
  Simple::VM::Heap heap;
  const uint32_t live = heap.Allocate(Simple::VM::ObjectKind::String, 0, 4);
  const uint32_t dead = heap.Allocate(Simple::VM::ObjectKind::String, 0, 4);
  std::vector<Simple::VM::Gc::Slot> globals = {live, dead};
  std::vector<uint8_t> ref_bits = {0x01};
  Simple::VM::Gc::RootTraceContext context;
  context.heap = &heap;
  context.globals = &globals;
  context.global_ref_bits = &ref_bits;
  Simple::VM::Gc::TraceRoots(context);
  heap.Sweep();
  return heap.Get(live) != nullptr && heap.Get(dead) == nullptr &&
         Simple::VM::Gc::IsNullRef(Simple::VM::HeapLayout::kNullRef);
}

const TestCase kVmGcTests[] = {
  {"vm_split_gc_traces_global_roots", VmSplitGcTracesGlobalRoots},
};

const TestSection kVmGcSections[] = {
  {"vm_gc", kVmGcTests, sizeof(kVmGcTests) / sizeof(kVmGcTests[0])},
};

} // namespace

const TestSection* GetVmGcSections(size_t* count) {
  if (count) *count = sizeof(kVmGcSections) / sizeof(kVmGcSections[0]);
  return kVmGcSections;
}

} // namespace Simple::VM::Tests
