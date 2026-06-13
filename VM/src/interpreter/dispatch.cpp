#include "interpreter/dispatch.h"

namespace Simple::VM::Interpreter {

int32_t ReadI32(const std::vector<uint8_t>& code, size_t& pc) {
  const uint32_t value = ReadU32(code, pc);
  return static_cast<int32_t>(value);
}

int64_t ReadI64(const std::vector<uint8_t>& code, size_t& pc) {
  const uint64_t value = ReadU64(code, pc);
  return static_cast<int64_t>(value);
}

uint32_t ReadU32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t value = static_cast<uint32_t>(code[pc]) |
                   (static_cast<uint32_t>(code[pc + 1]) << 8u) |
                   (static_cast<uint32_t>(code[pc + 2]) << 16u) |
                   (static_cast<uint32_t>(code[pc + 3]) << 24u);
  pc += 4;
  return value;
}

uint64_t ReadU64(const std::vector<uint8_t>& code, size_t& pc) {
  uint64_t value = static_cast<uint64_t>(code[pc]) |
                   (static_cast<uint64_t>(code[pc + 1]) << 8u) |
                   (static_cast<uint64_t>(code[pc + 2]) << 16u) |
                   (static_cast<uint64_t>(code[pc + 3]) << 24u) |
                   (static_cast<uint64_t>(code[pc + 4]) << 32u) |
                   (static_cast<uint64_t>(code[pc + 5]) << 40u) |
                   (static_cast<uint64_t>(code[pc + 6]) << 48u) |
                   (static_cast<uint64_t>(code[pc + 7]) << 56u);
  pc += 8;
  return value;
}

uint16_t ReadU16(const std::vector<uint8_t>& code, size_t& pc) {
  uint16_t value = static_cast<uint16_t>(code[pc]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(code[pc + 1]) << 8u);
  pc += 2;
  return value;
}

uint8_t ReadU8(const std::vector<uint8_t>& code, size_t& pc) {
  return code[pc++];
}

} // namespace Simple::VM::Interpreter
