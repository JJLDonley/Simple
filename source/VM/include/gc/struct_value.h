#ifndef SIMPLE_VM_GC_STRUCT_VALUE_H
#define SIMPLE_VM_GC_STRUCT_VALUE_H

#include <cstdint>
#include <string>

#include "heap.h"
#include "sbc_types.h"

namespace Simple::VM::GC {

bool CloneStructValue(const Simple::Byte::SbcModule& module,
                      Heap& heap,
                      uint32_t source,
                      uint32_t* clone,
                      std::string* error);

bool StructValuesEqual(const Simple::Byte::SbcModule& module,
                       const Heap& heap,
                       uint32_t left,
                       uint32_t right,
                       bool* equal,
                       std::string* error);

} // namespace Simple::VM::GC

#endif
