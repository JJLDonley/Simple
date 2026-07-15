#include "test_utils.h"
#include "simple_runner.h"

#include <cstdint>
#include <vector>

#include "gc/aggregate_trace.h"
#include "gc/root_tracer.h"
#include "heap.h"
#include "runtime/values.h"
#include "sbc_emitter.h"

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
         Simple::VM::Runtime::IsNullRef(Simple::VM::HeapLayout::kNullRef);
}

bool VmGcTracesOnlyActiveTaggedPayload() {
  Simple::Byte::SbcModule module;
  module.types.resize(3);
  module.types[0].kind = static_cast<uint8_t>(Simple::Byte::TypeKind::I32);
  module.types[0].size = 4;
  module.types[1].kind = static_cast<uint8_t>(Simple::Byte::TypeKind::String);
  module.types[1].size = 4;
  module.types[2].kind = static_cast<uint8_t>(Simple::Byte::TypeKind::Result);
  module.types[2].size = 12;
  module.types[2].field_count = 3;
  const uint32_t tag_name = Simple::Byte::sbc::AppendStringToPool(module.const_pool, "tag");
  const uint32_t value_name = Simple::Byte::sbc::AppendStringToPool(module.const_pool, "value");
  const uint32_t error_name = Simple::Byte::sbc::AppendStringToPool(module.const_pool, "error");
  module.fields = {
      Simple::Byte::FieldRow{tag_name, 0, 0, 0},
      Simple::Byte::FieldRow{value_name, 1, 4, 0},
      Simple::Byte::FieldRow{error_name, 1, 8, 0},
  };

  Simple::VM::Heap heap;
  heap.SetAggregateTraceDescriptors(
      Simple::VM::Gc::BuildAggregateTraceDescriptors(module));
  const uint32_t value_active = heap.Allocate(Simple::VM::ObjectKind::String, 0, 8);
  const uint32_t error_inactive = heap.Allocate(Simple::VM::ObjectKind::String, 0, 8);
  const uint32_t value_result = heap.Allocate(Simple::VM::ObjectKind::Aggregate, 2, 12);
  Simple::VM::HeapObject* value_object = heap.Get(value_result);
  if (!value_object) return false;
  Simple::VM::WriteU32Payload(value_object->payload, 0, 0);
  Simple::VM::WriteU32Payload(value_object->payload, 4, value_active);
  Simple::VM::WriteU32Payload(value_object->payload, 8, error_inactive);

  const uint32_t error_active = heap.Allocate(Simple::VM::ObjectKind::String, 0, 8);
  const uint32_t value_inactive = heap.Allocate(Simple::VM::ObjectKind::String, 0, 8);
  const uint32_t error_result = heap.Allocate(Simple::VM::ObjectKind::Aggregate, 2, 12);
  Simple::VM::HeapObject* error_object = heap.Get(error_result);
  if (!error_object) return false;
  Simple::VM::WriteU32Payload(error_object->payload, 0, 1);
  Simple::VM::WriteU32Payload(error_object->payload, 4, value_inactive);
  Simple::VM::WriteU32Payload(error_object->payload, 8, error_active);

  heap.ResetMarks();
  heap.Mark(value_result);
  heap.Mark(error_result);
  heap.Sweep();
  return heap.Get(value_result) != nullptr && heap.Get(error_result) != nullptr &&
         heap.Get(value_active) != nullptr && heap.Get(error_active) != nullptr &&
         heap.Get(value_inactive) == nullptr && heap.Get(error_inactive) == nullptr;
}

bool VmGcTracesSwitchLoopLocalRefs() {
  return RunSimpleFile("tests/simple/gc_switch_loop_ref_lifetimes.simple", true) == 0;
}

bool VmGcTracesGlobalRefs() {
  return RunSimpleFile("tests/simple/gc_global_ref_lifetimes.simple", true) == 0;
}

bool VmGcTracesCallTemporaryRefs() {
  return RunSimpleFile("tests/simple/gc_call_stack_temp_refs.simple", true) == 0;
}

const TestCase kVmGcTests[] = {
  {"vm_split_gc_traces_global_roots", VmSplitGcTracesGlobalRoots},
  {"vm_gc_traces_only_active_tagged_payload", VmGcTracesOnlyActiveTaggedPayload},
  {"vm_gc_traces_switch_loop_local_refs", VmGcTracesSwitchLoopLocalRefs},
  {"vm_gc_traces_global_refs", VmGcTracesGlobalRefs},
  {"vm_gc_traces_call_temporary_refs", VmGcTracesCallTemporaryRefs},
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
