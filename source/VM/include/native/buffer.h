#ifndef SIMPLE_VM_NATIVE_BUFFER_H
#define SIMPLE_VM_NATIVE_BUFFER_H

#include <cstdint>
#include <vector>

#include "heap.h"

namespace Simple::VM::Native::Buffer {

bool IsBuffer(const HeapObject* obj);
uint32_t Len(const HeapObject* obj);
uint32_t ReadLE(const HeapObject* obj, uint32_t offset, uint32_t width);
bool WriteLE(HeapObject* obj, uint32_t offset, uint32_t width, uint32_t value);
std::vector<uint32_t> Slice(const HeapObject* obj, uint32_t offset, uint32_t count);
uint32_t Copy(HeapObject* dst, uint32_t dst_offset, const HeapObject* src, uint32_t src_offset,
              uint32_t count);

} // namespace Simple::VM::Native::Buffer

#endif // SIMPLE_VM_NATIVE_BUFFER_H
