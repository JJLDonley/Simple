#ifndef SIMPLE_VM_IR_BUILDER_H
#define SIMPLE_VM_IR_BUILDER_H

#include <cstdint>
#include <string>
#include <vector>

#include "opcode.h"

namespace Simple::IR {

using Simple::Byte::OpCode;

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
  void EmitConstI8(int8_t value);
  void EmitConstI16(int16_t value);
  void EmitConstU8(uint8_t value);
  void EmitConstU16(uint16_t value);
  void EmitConstU32(uint32_t value);
  void EmitConstU64(uint64_t value);
  void EmitConstChar(uint16_t value);
  void EmitConstString(uint32_t const_id);
  void EmitConstNull();
  void EmitCall(uint32_t func_id, uint8_t arg_count);
  void EmitCallIndirect(uint32_t sig_id, uint8_t arg_count);
  void EmitTailCall(uint32_t func_id, uint8_t arg_count);
  void EmitCallCheck();
  void EmitIntrinsic(uint32_t id);
  void EmitSysCall(uint32_t id);
  void EmitJmpTable(const std::vector<IrLabel>& cases, IrLabel default_label);
  void EmitNewArray(uint32_t type_id, uint32_t length);
  void EmitArrayLen();
  void EmitArrayGetI32();
  void EmitArraySetI32();
  void EmitArrayGetI64();
  void EmitArraySetI64();
  void EmitArrayGetF32();
  void EmitArraySetF32();
  void EmitArrayGetF64();
  void EmitArraySetF64();
  void EmitArrayGetRef();
  void EmitArraySetRef();
  void EmitNewList(uint32_t type_id, uint32_t capacity);
  void EmitListLen();
  void EmitListGetI32();
  void EmitListSetI32();
  void EmitListPushI32();
  void EmitListPopI32();
  void EmitListGetI64();
  void EmitListSetI64();
  void EmitListPushI64();
  void EmitListPopI64();
  void EmitListGetF32();
  void EmitListSetF32();
  void EmitListPushF32();
  void EmitListPopF32();
  void EmitListGetF64();
  void EmitListSetF64();
  void EmitListPushF64();
  void EmitListPopF64();
  void EmitListGetRef();
  void EmitListSetRef();
  void EmitListPushRef();
  void EmitListPopRef();
  void EmitListInsertI32();
  void EmitListRemoveI32();
  void EmitListClear();
  void EmitNewClosure(uint32_t method_id, uint8_t upvalue_count);
  void EmitIsNull();
  void EmitRefEq();
  void EmitRefNe();
  void EmitNewObject(uint32_t type_id);
  void EmitLoadField(uint32_t field_id);
  void EmitStoreField(uint32_t field_id);
  void EmitTypeOf();
  void EmitStringLen();
  void EmitStringConcat();
  void EmitStringGetChar();
  void EmitStringSlice();
  void EmitLoadLocal(uint32_t index);
  void EmitStoreLocal(uint32_t index);
  void EmitLoadGlobal(uint32_t index);
  void EmitStoreGlobal(uint32_t index);
  void EmitLoadUpvalue(uint32_t index);
  void EmitStoreUpvalue(uint32_t index);
  void EmitRet();
  void EmitPop();
  void EmitDup();
  void EmitDup2();
  void EmitSwap();
  void EmitRot();
  void EmitCmpEqI32();
  void EmitCmpLtI32();
  void EmitCmpNeI32();
  void EmitCmpLeI32();
  void EmitCmpGtI32();
  void EmitCmpGeI32();
  void EmitCmpEqI64();
  void EmitCmpNeI64();
  void EmitCmpLtI64();
  void EmitCmpLeI64();
  void EmitCmpGtI64();
  void EmitCmpGeI64();
  void EmitCmpEqF32();
  void EmitCmpNeF32();
  void EmitCmpLtF32();
  void EmitCmpLeF32();
  void EmitCmpGtF32();
  void EmitCmpGeF32();
  void EmitCmpEqF64();
  void EmitCmpNeF64();
  void EmitCmpLtF64();
  void EmitCmpLeF64();
  void EmitCmpGtF64();
  void EmitCmpGeF64();
  void EmitCmpEqU32();
  void EmitCmpNeU32();
  void EmitCmpLtU32();
  void EmitCmpLeU32();
  void EmitCmpGtU32();
  void EmitCmpGeU32();
  void EmitCmpEqU64();
  void EmitCmpNeU64();
  void EmitCmpLtU64();
  void EmitCmpLeU64();
  void EmitCmpGtU64();
  void EmitCmpGeU64();
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
  void EmitSubI32();
  void EmitMulI32();
  void EmitDivI32();
  void EmitModI32();
  void EmitAddI64();
  void EmitSubI64();
  void EmitMulI64();
  void EmitDivI64();
  void EmitModI64();
  void EmitAddF32();
  void EmitSubF32();
  void EmitMulF32();
  void EmitDivF32();
  void EmitAddF64();
  void EmitSubF64();
  void EmitMulF64();
  void EmitDivF64();
  void EmitAddU32();
  void EmitSubU32();
  void EmitMulU32();
  void EmitDivU32();
  void EmitModU32();
  void EmitAddU64();
  void EmitSubU64();
  void EmitMulU64();
  void EmitDivU64();
  void EmitModU64();
  void EmitAndI32();
  void EmitOrI32();
  void EmitXorI32();
  void EmitShlI32();
  void EmitShrI32();
  void EmitAndI64();
  void EmitOrI64();
  void EmitXorI64();
  void EmitShlI64();
  void EmitShrI64();
  void EmitNegI32();
  void EmitNegI64();
  void EmitNegF32();
  void EmitNegF64();
  void EmitNegI8();
  void EmitNegI16();
  void EmitNegU8();
  void EmitNegU16();
  void EmitNegU32();
  void EmitNegU64();
  void EmitIncI32();
  void EmitDecI32();
  void EmitIncI64();
  void EmitDecI64();
  void EmitIncF32();
  void EmitDecF32();
  void EmitIncF64();
  void EmitDecF64();
  void EmitIncU32();
  void EmitDecU32();
  void EmitIncU64();
  void EmitDecU64();
  void EmitIncI8();
  void EmitDecI8();
  void EmitIncI16();
  void EmitDecI16();
  void EmitIncU8();
  void EmitDecU8();
  void EmitIncU16();
  void EmitDecU16();
  void EmitJmp(IrLabel label);
  void EmitJmpTrue(IrLabel label);
  void EmitJmpFalse(IrLabel label);

  bool Finish(std::vector<uint8_t>* out, std::string* error);
  const std::vector<uint8_t>& const_pool() const { return const_pool_; }

 private:
  struct IrJmpTable {
    std::size_t table_base = 0;
    uint32_t payload_offset = 0;
    std::vector<uint32_t> case_label_ids;
  };

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
  std::vector<uint8_t> const_pool_;
  std::vector<IrJmpTable> jmp_tables_;
};

} // namespace Simple::IR

#endif // SIMPLE_VM_IR_BUILDER_H
