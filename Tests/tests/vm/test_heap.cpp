#include "test_utils.h"

#include <fstream>
#include <iterator>
#include <string>

#include "heap.h"

namespace Simple::VM::Tests {
namespace {

bool VmHeapOwnsPayloadAndStringHelpers() {
  std::ifstream vm("VM/src/vm.cpp");
  std::ifstream header("VM/include/heap.h");
  std::ifstream source("VM/src/heap.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("uint32_t ReadU32Payload(") != std::string::npos &&
         header_text.find("uint32_t CreateString(") != std::string::npos &&
         source_text.find("uint32_t ReadU32Payload(") != std::string::npos &&
         source_text.find("std::u16string ReadString(") != std::string::npos &&
         vm_text.find("uint32_t ReadU32Payload(") == std::string::npos &&
         vm_text.find("uint32_t CreateString(Heap&") == std::string::npos;
}

bool VmHeapStringHelpersRoundTripText() {
  Simple::VM::Heap heap;
  const uint32_t handle = Simple::VM::CreateString(heap, u"abc");
  const Simple::VM::HeapObject* object = heap.Get(handle);
  return object && Simple::VM::ReadString(object) == u"abc";
}

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
  {"vm_heap_owns_payload_and_string_helpers", VmHeapOwnsPayloadAndStringHelpers},
  {"vm_heap_string_helpers_round_trip_text", VmHeapStringHelpersRoundTripText},
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
