#ifndef SIMPLE_VM_JIT_LLVM_BACKEND_H
#define SIMPLE_VM_JIT_LLVM_BACKEND_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "interpreter/stack.h"
#include "jit/status.h"
#include "sbc_types.h"

namespace Simple::VM {
class Heap;
struct ExecOptions;
}

namespace Simple::VM::Jit {

struct LlvmJitOptions {
  bool optimize = true;
  // When false, reject SBC call/import/native-call shapes instead of lowering
  // through temporary helper bridges. Use this for CLI tier dispatch until the
  // LLVM ABI carries full VM/runtime context.
  bool allow_runtime_calls = true;
};

struct LlvmJitStatus {
  bool available = false;
  std::string message;
};

// First-stage LLVM ORC integration seam.  This intentionally has no VM state
// dependency yet: the next step is to make this return callable JIT entries for
// verified SBC functions and let vm.cpp dispatch through them.
class LlvmJitBackend {
 public:
  explicit LlvmJitBackend(LlvmJitOptions options = {});

  LlvmJitStatus Status() const;

  // Returns true only when the LLVM backend is compiled in and can accept the
  // function shape. Native code emission is added behind this API next.
  bool CanAcceptFunction(const Simple::Byte::SbcModule& module,
                         size_t func_index,
                         std::string& reason) const;

  // Compile and execute a currently supported subset through LLVM ORC.
  // Unsupported shapes return false with reason="unsupported" so callers can
  // continue to interpreter fallback.
  bool TryRunFunction(const Simple::Byte::SbcModule& module,
                      size_t func_index,
                      const std::vector<Simple::VM::Interpreter::Slot>& args,
                      Simple::VM::Interpreter::Slot& out_ret,
                      bool& out_has_ret,
                      std::string& reason) const;

  bool TryRunFunctionWithRuntime(const Simple::Byte::SbcModule& module,
                                 size_t func_index,
                                 const std::vector<Simple::VM::Interpreter::Slot>& args,
                                 Simple::VM::Heap* heap,
                                 std::vector<Simple::VM::Interpreter::Slot>* globals,
                                 const Simple::VM::ExecOptions* exec_options,
                                 Simple::VM::Interpreter::Slot& out_ret,
                                 bool& out_has_ret,
                                 std::string& reason) const;

 private:
  LlvmJitOptions options_;
};

LlvmJitStatus GetLlvmJitStatus();

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_LLVM_BACKEND_H
