#ifndef SIMPLE_VM_IR_LANG_H
#define SIMPLE_VM_IR_LANG_H

#include <cstdint>
#include <string>
#include <vector>

#include "ir_compiler.h"

namespace Simple::IR::Text {

enum class InstKind {
  Op,
  Label,
};

struct IrTextInst {
  InstKind kind = InstKind::Op;
  std::string op;
  std::vector<std::string> args;
  std::string label;
};

struct IrTextFunction {
  std::string name;
  uint16_t locals = 0;
  uint32_t stack_max = 8;
  uint32_t sig_id = 0;
  std::vector<IrTextInst> insts;
};

struct IrTextModule {
  std::vector<IrTextFunction> functions;
  std::string entry_name;
  uint32_t entry_index = 0;
};

bool ParseIrTextModule(const std::string& text, IrTextModule* out, std::string* error);
bool LowerIrTextToModule(const IrTextModule& text, Simple::IR::IrModule* out, std::string* error);

} // namespace Simple::IR::Text

#endif // SIMPLE_VM_IR_LANG_H
