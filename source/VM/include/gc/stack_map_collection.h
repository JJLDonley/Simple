#ifndef SIMPLE_VM_GC_STACK_MAP_COLLECTION_H
#define SIMPLE_VM_GC_STACK_MAP_COLLECTION_H

#include <cstddef>
#include <vector>

#include "gc/root_tracer.h"
#include "heap.h"
#include "interpreter/frames.h"
#include "sbc_verifier.h"
#include "simple_api.h"

namespace Simple::VM::Gc {

const Simple::Byte::StackMap* FindVerifiedStackMap(const Simple::Byte::VerifyResult& verify_result,
                                                   size_t func_index,
                                                   size_t pc_value);

void MaybeCollectWithStackMap(bool have_meta,
                              size_t op_counter,
                              size_t pc,
                              const Simple::Byte::VerifyResult& verify_result,
                              Heap& heap,
                              const std::vector<Slot>& globals,
                              const std::vector<Slot>& stack,
                              const std::vector<Interpreter::FrameState>& call_stack,
                              const Interpreter::FrameState& current,
                              const std::vector<Slot>& locals_arena,
                              const std::vector<uint32_t>& pointer_roots);

} // namespace Simple::VM::Gc

#endif // SIMPLE_VM_GC_STACK_MAP_COLLECTION_H
