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

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_COMPILE_POLICY_H
