#include "test_utils.h"

#include "heap.h"

namespace Simple::VM::Tests {
namespace {

bool VmSplitHeapAllocatesAndReusesHandles() {
  Simple::VM::Heap heap;
  heap.SetLimits(4, 1024);
  const uint32_t first = heap.Allocate(Simple::VM::ObjectKind::String, 7, 8);
  auto* object = heap.Get(first);
  if (!object || object->header.kind != Simple::VM::ObjectKind::String ||
      object->header.type_id != 7 || object->payload.size() != 8) {
    return false;
  }
  heap.Sweep();
  const uint32_t second = heap.Allocate(Simple::VM::ObjectKind::Array, 3, 4);
  return second == first && heap.Get(second) && heap.Get(second)->header.kind == Simple::VM::ObjectKind::Array;
}

const TestCase kVmHeapTests[] = {
  {"vm_split_heap_allocates_and_reuses_handles", VmSplitHeapAllocatesAndReusesHandles},
};

const TestSection kVmHeapSections[] = {
  {"vm_heap", kVmHeapTests, sizeof(kVmHeapTests) / sizeof(kVmHeapTests[0])},
};

} // namespace

const TestSection* GetVmHeapSections(size_t* count) {
  if (count) *count = sizeof(kVmHeapSections) / sizeof(kVmHeapSections[0]);
  return kVmHeapSections;
}

} // namespace Simple::VM::Tests
