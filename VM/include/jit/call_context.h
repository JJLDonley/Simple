#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "heap.h"
#include "interpreter/stack.h"
#include "sbc_types.h"

namespace Simple::VM::Jit {

using Slot = Simple::VM::Interpreter::Slot;

enum class JitCallTrapKind {
  None,
  Trap,
  Fallback,
  Unsupported,
};

struct JitCallTrap {
  JitCallTrapKind kind = JitCallTrapKind::None;
  std::string message;
};

struct JitCallContext {
  std::vector<Slot> args;
  std::vector<Slot> operand_stack;
  std::vector<Slot> locals;
  std::vector<Slot> spills;
  std::vector<Slot>* globals = nullptr;
  Heap* heap = nullptr;
  Slot return_value = 0;
  bool has_return = false;
  JitCallTrap trap;
  std::vector<uint32_t> root_refs;
};

bool JitArg(const JitCallContext& context, size_t index, Slot* out);
bool JitStackSlot(const JitCallContext& context, size_t index, Slot* out);
bool PushJitStack(JitCallContext* context, Slot value);
bool PopJitStack(JitCallContext* context, Slot* out);
bool JitLocal(const JitCallContext& context, size_t index, Slot* out);
bool SetJitLocal(JitCallContext* context, size_t index, Slot value);
bool JitGlobal(const JitCallContext& context, size_t index, Slot* out);
bool SetJitGlobal(JitCallContext* context, size_t index, Slot value);
void SetJitReturn(JitCallContext* context, Slot value);
void ClearJitReturn(JitCallContext* context);
void SetJitTrap(JitCallContext* context, JitCallTrapKind kind, std::string message);
void RegisterJitRoot(JitCallContext* context, uint32_t ref);
void ClearJitRoots(JitCallContext* context);
bool IsJitRootType(const Simple::Byte::SbcModule& module, uint32_t type_id);
bool PublishJitRootsFromContext(JitCallContext* context,
                                const Simple::Byte::SbcModule& module,
                                const std::vector<uint32_t>& arg_type_ids,
                                const std::vector<uint32_t>& local_type_ids,
                                const std::vector<uint32_t>& stack_type_ids);
void PublishJitRootSlotsByMask(JitCallContext* context,
                               const std::vector<Slot>& slots,
                               uint64_t ref_mask);

} // namespace Simple::VM::Jit
