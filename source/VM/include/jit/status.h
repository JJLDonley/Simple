#ifndef SIMPLE_VM_JIT_STATUS_H
#define SIMPLE_VM_JIT_STATUS_H

namespace Simple::VM::Jit {

enum class JitStatusCode {
  Halt,
  Return,
  Trap,
  Fallback,
  Unsupported,
};

const char* JitStatusCodeName(JitStatusCode code);
JitStatusCode ClassifyJitReason(const char* reason);

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_STATUS_H
