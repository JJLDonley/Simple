#ifndef SIMPLE_VM_IR_COMPILER_H
#define SIMPLE_VM_IR_COMPILER_H

#include <cstdint>
#include <string>
#include <vector>

#include "simple_api.h"
#include "sbc_emitter.h"

namespace Simple::IR {

struct IrFunction {
  std::vector<uint8_t> code;
  uint16_t local_count = 0;
  uint32_t sig_id = 0;
  uint32_t stack_max = 8;
};

struct IrModule {
  std::vector<IrFunction> functions;

  std::vector<Simple::Byte::sbc::SigSpec> sig_specs;
  std::vector<uint8_t> types_bytes;
  std::vector<uint8_t> fields_bytes;
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> globals_bytes;
  std::vector<uint8_t> imports_bytes;
  std::vector<uint8_t> exports_bytes;
  std::vector<uint8_t> debug_bytes;

  uint32_t entry_method_id = 0;
};

SIMPLEVM_API bool CompileToSbc(const IrModule& module, std::vector<uint8_t>* out, std::string* error);

} // namespace Simple::IR

#endif // SIMPLE_VM_IR_COMPILER_H
