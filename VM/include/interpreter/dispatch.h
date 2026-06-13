#ifndef SIMPLE_VM_INTERPRETER_DISPATCH_H
#define SIMPLE_VM_INTERPRETER_DISPATCH_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Simple::VM::Interpreter {

int32_t ReadI32(const std::vector<uint8_t>& code, size_t& pc);
int64_t ReadI64(const std::vector<uint8_t>& code, size_t& pc);
uint32_t ReadU32(const std::vector<uint8_t>& code, size_t& pc);
uint64_t ReadU64(const std::vector<uint8_t>& code, size_t& pc);
uint16_t ReadU16(const std::vector<uint8_t>& code, size_t& pc);
uint8_t ReadU8(const std::vector<uint8_t>& code, size_t& pc);

} // namespace Simple::VM::Interpreter

#endif // SIMPLE_VM_INTERPRETER_DISPATCH_H
