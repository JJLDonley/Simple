#ifndef SIMPLE_VM_IR_LANG_H
#define SIMPLE_VM_IR_LANG_H

#include <cstdint>
#include <string>
#include <unordered_map>
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
  std::string sig_name;
  bool sig_is_name = false;
  std::unordered_map<std::string, uint16_t> locals_map;
  std::vector<IrTextInst> insts;
};

struct IrTextField {
  std::string name;
  std::string type;
  uint32_t offset = 0;
};

struct IrTextType {
  std::string name;
  std::string kind;
  uint32_t size = 0;
  std::vector<IrTextField> fields;
};

struct IrTextSig {
  std::string name;
  std::string ret;
  std::vector<std::string> params;
};

struct IrTextConst {
  std::string name;
  std::string kind;
  std::string value;
};

struct IrTextImport {
  std::string kind; // syscall | intrinsic
  std::string name;
  uint32_t id = 0;
};

struct IrTextModule {
  std::vector<IrTextType> types;
  std::vector<IrTextSig> sigs;
  std::vector<IrTextConst> consts;
  std::vector<IrTextImport> imports;
  std::vector<IrTextFunction> functions;
  std::string entry_name;
  uint32_t entry_index = 0;
};

bool ParseIrTextModule(const std::string& text, IrTextModule* out, std::string* error);
bool LowerIrTextToModule(const IrTextModule& text, Simple::IR::IrModule* out, std::string* error);

} // namespace Simple::IR::Text

#endif // SIMPLE_VM_IR_LANG_H
