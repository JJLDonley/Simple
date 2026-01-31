#ifndef SIMPLE_VM_IR_BUILDER_H
#define SIMPLE_VM_IR_BUILDER_H

#include <cstdint>
#include <string>
#include <vector>

#include "opcode.h"

namespace simplevm {

struct IrLabel {
  uint32_t id = 0;
};

struct IrFixup {
  uint32_t label_id = 0;
  std::size_t patch_offset = 0;
};

class IrBuilder {
 public:
  IrLabel CreateLabel();
  bool BindLabel(IrLabel label, std::string* error);

  void EmitOp(OpCode op);
  void EmitEnter(uint16_t locals);
  void EmitConstI32(int32_t value);
  void EmitConstI64(int64_t value);
  void EmitConstF32(float value);
  void EmitConstF64(double value);
  void EmitConstBool(bool value);
  void EmitCall(uint32_t func_id, uint8_t arg_count);
  void EmitCallIndirect(uint32_t sig_id, uint8_t arg_count);
  void EmitTailCall(uint32_t func_id, uint8_t arg_count);
  void EmitNewArray(uint32_t type_id, uint32_t length);
  void EmitArrayLen();
  void EmitArrayGetI32();
  void EmitArraySetI32();
  void EmitNewList(uint32_t type_id, uint32_t capacity);
  void EmitListLen();
  void EmitListGetI32();
  void EmitListSetI32();
  void EmitListPushI32();
  void EmitListPopI32();
  void EmitLoadLocal(uint32_t index);
  void EmitStoreLocal(uint32_t index);
  void EmitLoadGlobal(uint32_t index);
  void EmitStoreGlobal(uint32_t index);
  void EmitRet();
  void EmitPop();
  void EmitDup();
  void EmitCmpEqI32();
  void EmitCmpLtI32();
  void EmitBoolNot();
  void EmitBoolAnd();
  void EmitBoolOr();
  void EmitConvI32ToI64();
  void EmitConvI64ToI32();
  void EmitConvI32ToF32();
  void EmitConvI32ToF64();
  void EmitConvF32ToI32();
  void EmitConvF64ToI32();
  void EmitConvF32ToF64();
  void EmitConvF64ToF32();
  void EmitAddI32();
  void EmitAddI64();
  void EmitAddF32();
  void EmitAddF64();
  void EmitAndI32();
  void EmitOrI32();
  void EmitXorI32();
  void EmitShlI32();
  void EmitShrI32();
  void EmitJmp(IrLabel label);
  void EmitJmpTrue(IrLabel label);
  void EmitJmpFalse(IrLabel label);

  bool Finish(std::vector<uint8_t>* out, std::string* error);

 private:
  void EmitU8(uint8_t value);
  void EmitU16(uint16_t value);
  void EmitU32(uint32_t value);
  void EmitU64(uint64_t value);
  void EmitRel32Fixup(IrLabel label);
  static uint32_t FloatToBits(float value);
  static uint64_t DoubleToBits(double value);

  std::vector<uint8_t> code_;
  std::vector<int64_t> label_offsets_;
  std::vector<IrFixup> fixups_;
};

} // namespace simplevm

#endif // SIMPLE_VM_IR_BUILDER_H
