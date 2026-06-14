#ifndef SIMPLE_VM_JIT_COMPILE_POLICY_H
#define SIMPLE_VM_JIT_COMPILE_POLICY_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sbc_types.h"
#include "sbc_verifier.h"

namespace Simple::VM::Jit {

bool CanCompileMethod(const Simple::Byte::SbcModule& module,
                      const Simple::Byte::VerifyResult& verify_result,
                      bool have_meta,
                      size_t func_index,
                      std::vector<uint8_t>& compile_stack);

struct CompilePredicate {
  const Simple::Byte::SbcModule* module = nullptr;
  const Simple::Byte::VerifyResult* verify_result = nullptr;
  bool have_meta = false;
  std::vector<uint8_t>* compile_stack = nullptr;

  bool operator()(size_t func_index) const;
};

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_COMPILE_POLICY_H
