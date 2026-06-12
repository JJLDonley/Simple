#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "TAST/tast.h"

namespace Simple::Lang::IRB {

struct IrType {
  std::string name;
};

struct IrSig {
  std::vector<IrType> params;
  IrType result;
  bool has_result = false;
};

struct IrInst {
  std::string opcode;
  std::vector<std::string> operands;
};

struct IrBlock {
  std::string label;
  std::vector<IrInst> instructions;
};

struct IrFunction {
  std::string name;
  IrSig signature;
  std::vector<IrBlock> blocks;
};

struct IrSignature {
  std::string name;
  IrSig signature;
};

struct IrImport {
  std::string name;
  std::string module;
  std::string symbol;
  std::string signature_name;
  IrSig signature;
};

struct IrGlobal {
  std::string name;
  IrType type;
  std::string init;
};

struct IrArtifactField {
  std::string name;
  IrType type;
  uint32_t index = 0;
};

struct IrArtifactLayout {
  std::string name;
  std::vector<IrArtifactField> fields;
};

struct IrAbiField {
  std::string name;
  IrType type;
};

struct IrAbiType {
  std::string name;
  std::vector<IrAbiField> fields;
};

struct IrStackInfo {
  std::string function;
  uint32_t locals = 0;
  uint32_t max_stack = 0;
};

struct IrModule {
  std::vector<IrSignature> signatures;
  std::vector<IrImport> imports;
  std::vector<IrGlobal> globals;
  std::vector<IrFunction> functions;
  std::vector<IrArtifactLayout> artifact_layouts;
  std::vector<IrAbiType> abi_types;
  std::vector<IrStackInfo> stack_infos;
};

// Language IR module boundary. During migration this carries serialized SIR
// plus the structured IR skeleton introduced for the IRB -> IRE split.
struct Module {
  IrModule ir;
  std::string sir_text;
};

bool BuildModule(const Simple::Lang::TAST::TypedProgram& typed,
                 Module* out,
                 std::string* error);

} // namespace Simple::Lang::IRB
