#ifndef SIMPLE_VM_INTERPRETER_FRAMES_H
#define SIMPLE_VM_INTERPRETER_FRAMES_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "heap.h"
#include "sbc_types.h"

namespace Simple::VM::Interpreter {

struct FrameState {
  size_t func_index = 0;
  size_t return_pc = 0;
  size_t stack_base = 0;
  uint32_t closure_ref = HeapLayout::kNullRef;
  uint32_t completing_promise_ref = HeapLayout::kNullRef;
  uint32_t line = 0;
  uint32_t column = 0;
  size_t locals_base = 0;
  uint16_t locals_count = 0;
};

FrameState MakeFrame(size_t func_index, size_t return_pc, size_t stack_base, uint32_t closure_ref);
size_t AllocateLocalSlots(std::vector<uint64_t>& locals_arena, uint16_t count);
FrameState BuildFrame(const Simple::Byte::SbcModule& module,
                      std::vector<uint64_t>& locals_arena,
                      size_t func_index,
                      size_t return_pc,
                      size_t stack_base,
                      uint32_t closure_ref);

} // namespace Simple::VM::Interpreter

#endif // SIMPLE_VM_INTERPRETER_FRAMES_H
