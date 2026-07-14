#ifndef SIMPLE_VM_NATIVE_ARG_UTILS_H
#define SIMPLE_VM_NATIVE_ARG_UTILS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "heap.h"
#include "native/registry.h"

namespace Simple::VM::Native {

HeapObject* NativeArgHeapObject(NativeCallContext& context, size_t index);
bool ReadStringArg(NativeCallContext& context, size_t index, std::string* out_value);
bool ReadStringSequence(NativeCallContext& context,
                        size_t index,
                        std::vector<std::string>* out);
bool ReadByteSequence(NativeCallContext& context, size_t index, std::vector<int32_t>* out);
Slot CreateByteList(Heap& heap, const std::vector<uint32_t>& values);
void WriteU32(std::vector<uint8_t>& payload, size_t offset, uint32_t value);
uint32_t ReadU32(const std::vector<uint8_t>& payload, size_t offset);

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_ARG_UTILS_H
