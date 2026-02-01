#include "ir_builder.h"

#include <cstring>

namespace simplevm {

IrLabel IrBuilder::CreateLabel() {
  IrLabel label{static_cast<uint32_t>(label_offsets_.size())};
  label_offsets_.push_back(-1);
  return label;
}

bool IrBuilder::BindLabel(IrLabel label, std::string* error) {
  if (label.id >= label_offsets_.size()) {
    if (error) *error = "label id out of range";
    return false;
  }
  if (label_offsets_[label.id] >= 0) {
    if (error) *error = "label already bound";
    return false;
  }
  label_offsets_[label.id] = static_cast<int64_t>(code_.size());
  return true;
}

void IrBuilder::EmitOp(OpCode op) {
  EmitU8(static_cast<uint8_t>(op));
}

void IrBuilder::EmitEnter(uint16_t locals) {
  EmitOp(OpCode::Enter);
  EmitU16(locals);
}

void IrBuilder::EmitConstI32(int32_t value) {
  EmitOp(OpCode::ConstI32);
  EmitU32(static_cast<uint32_t>(value));
}

void IrBuilder::EmitConstI64(int64_t value) {
  EmitOp(OpCode::ConstI64);
  EmitU64(static_cast<uint64_t>(value));
}

void IrBuilder::EmitConstF32(float value) {
  EmitOp(OpCode::ConstF32);
  EmitU32(FloatToBits(value));
}

void IrBuilder::EmitConstF64(double value) {
  EmitOp(OpCode::ConstF64);
  EmitU64(DoubleToBits(value));
}

void IrBuilder::EmitConstBool(bool value) {
  EmitOp(OpCode::ConstBool);
  EmitU8(value ? 1 : 0);
}

void IrBuilder::EmitConstString(uint32_t const_id) {
  EmitOp(OpCode::ConstString);
  EmitU32(const_id);
}

void IrBuilder::EmitCall(uint32_t func_id, uint8_t arg_count) {
  EmitOp(OpCode::Call);
  EmitU32(func_id);
  EmitU8(arg_count);
}

void IrBuilder::EmitCallIndirect(uint32_t sig_id, uint8_t arg_count) {
  EmitOp(OpCode::CallIndirect);
  EmitU32(sig_id);
  EmitU8(arg_count);
}

void IrBuilder::EmitTailCall(uint32_t func_id, uint8_t arg_count) {
  EmitOp(OpCode::TailCall);
  EmitU32(func_id);
  EmitU8(arg_count);
}

void IrBuilder::EmitNewArray(uint32_t type_id, uint32_t length) {
  EmitOp(OpCode::NewArray);
  EmitU32(type_id);
  EmitU32(length);
}

void IrBuilder::EmitArrayLen() {
  EmitOp(OpCode::ArrayLen);
}

void IrBuilder::EmitArrayGetI32() {
  EmitOp(OpCode::ArrayGetI32);
}

void IrBuilder::EmitArraySetI32() {
  EmitOp(OpCode::ArraySetI32);
}

void IrBuilder::EmitArrayGetI64() {
  EmitOp(OpCode::ArrayGetI64);
}

void IrBuilder::EmitArraySetI64() {
  EmitOp(OpCode::ArraySetI64);
}

void IrBuilder::EmitArrayGetF32() {
  EmitOp(OpCode::ArrayGetF32);
}

void IrBuilder::EmitArraySetF32() {
  EmitOp(OpCode::ArraySetF32);
}

void IrBuilder::EmitArrayGetF64() {
  EmitOp(OpCode::ArrayGetF64);
}

void IrBuilder::EmitArraySetF64() {
  EmitOp(OpCode::ArraySetF64);
}

void IrBuilder::EmitArrayGetRef() {
  EmitOp(OpCode::ArrayGetRef);
}

void IrBuilder::EmitArraySetRef() {
  EmitOp(OpCode::ArraySetRef);
}

void IrBuilder::EmitNewList(uint32_t type_id, uint32_t capacity) {
  EmitOp(OpCode::NewList);
  EmitU32(type_id);
  EmitU32(capacity);
}

void IrBuilder::EmitListLen() {
  EmitOp(OpCode::ListLen);
}

void IrBuilder::EmitListGetI32() {
  EmitOp(OpCode::ListGetI32);
}

void IrBuilder::EmitListSetI32() {
  EmitOp(OpCode::ListSetI32);
}

void IrBuilder::EmitListPushI32() {
  EmitOp(OpCode::ListPushI32);
}

void IrBuilder::EmitListPopI32() {
  EmitOp(OpCode::ListPopI32);
}

void IrBuilder::EmitListGetI64() {
  EmitOp(OpCode::ListGetI64);
}

void IrBuilder::EmitListSetI64() {
  EmitOp(OpCode::ListSetI64);
}

void IrBuilder::EmitListPushI64() {
  EmitOp(OpCode::ListPushI64);
}

void IrBuilder::EmitListPopI64() {
  EmitOp(OpCode::ListPopI64);
}

void IrBuilder::EmitListGetF32() {
  EmitOp(OpCode::ListGetF32);
}

void IrBuilder::EmitListSetF32() {
  EmitOp(OpCode::ListSetF32);
}

void IrBuilder::EmitListPushF32() {
  EmitOp(OpCode::ListPushF32);
}

void IrBuilder::EmitListPopF32() {
  EmitOp(OpCode::ListPopF32);
}

void IrBuilder::EmitListGetF64() {
  EmitOp(OpCode::ListGetF64);
}

void IrBuilder::EmitListSetF64() {
  EmitOp(OpCode::ListSetF64);
}

void IrBuilder::EmitListPushF64() {
  EmitOp(OpCode::ListPushF64);
}

void IrBuilder::EmitListPopF64() {
  EmitOp(OpCode::ListPopF64);
}

void IrBuilder::EmitListGetRef() {
  EmitOp(OpCode::ListGetRef);
}

void IrBuilder::EmitListSetRef() {
  EmitOp(OpCode::ListSetRef);
}

void IrBuilder::EmitListPushRef() {
  EmitOp(OpCode::ListPushRef);
}

void IrBuilder::EmitListPopRef() {
  EmitOp(OpCode::ListPopRef);
}

void IrBuilder::EmitListInsertI32() {
  EmitOp(OpCode::ListInsertI32);
}

void IrBuilder::EmitListRemoveI32() {
  EmitOp(OpCode::ListRemoveI32);
}

void IrBuilder::EmitListClear() {
  EmitOp(OpCode::ListClear);
}

void IrBuilder::EmitIsNull() {
  EmitOp(OpCode::IsNull);
}

void IrBuilder::EmitRefEq() {
  EmitOp(OpCode::RefEq);
}

void IrBuilder::EmitRefNe() {
  EmitOp(OpCode::RefNe);
}

void IrBuilder::EmitNewObject(uint32_t type_id) {
  EmitOp(OpCode::NewObject);
  EmitU32(type_id);
}

void IrBuilder::EmitLoadField(uint32_t field_id) {
  EmitOp(OpCode::LoadField);
  EmitU32(field_id);
}

void IrBuilder::EmitStoreField(uint32_t field_id) {
  EmitOp(OpCode::StoreField);
  EmitU32(field_id);
}

void IrBuilder::EmitTypeOf() {
  EmitOp(OpCode::TypeOf);
}

void IrBuilder::EmitStringLen() {
  EmitOp(OpCode::StringLen);
}

void IrBuilder::EmitStringConcat() {
  EmitOp(OpCode::StringConcat);
}

void IrBuilder::EmitStringGetChar() {
  EmitOp(OpCode::StringGetChar);
}

void IrBuilder::EmitStringSlice() {
  EmitOp(OpCode::StringSlice);
}

void IrBuilder::EmitLoadLocal(uint32_t index) {
  EmitOp(OpCode::LoadLocal);
  EmitU32(index);
}

void IrBuilder::EmitStoreLocal(uint32_t index) {
  EmitOp(OpCode::StoreLocal);
  EmitU32(index);
}

void IrBuilder::EmitLoadGlobal(uint32_t index) {
  EmitOp(OpCode::LoadGlobal);
  EmitU32(index);
}

void IrBuilder::EmitStoreGlobal(uint32_t index) {
  EmitOp(OpCode::StoreGlobal);
  EmitU32(index);
}

void IrBuilder::EmitRet() {
  EmitOp(OpCode::Ret);
}

void IrBuilder::EmitPop() {
  EmitOp(OpCode::Pop);
}

void IrBuilder::EmitDup() {
  EmitOp(OpCode::Dup);
}

void IrBuilder::EmitCmpEqI32() {
  EmitOp(OpCode::CmpEqI32);
}

void IrBuilder::EmitCmpLtI32() {
  EmitOp(OpCode::CmpLtI32);
}

void IrBuilder::EmitBoolNot() {
  EmitOp(OpCode::BoolNot);
}

void IrBuilder::EmitBoolAnd() {
  EmitOp(OpCode::BoolAnd);
}

void IrBuilder::EmitBoolOr() {
  EmitOp(OpCode::BoolOr);
}

void IrBuilder::EmitConvI32ToI64() {
  EmitOp(OpCode::ConvI32ToI64);
}

void IrBuilder::EmitConvI64ToI32() {
  EmitOp(OpCode::ConvI64ToI32);
}

void IrBuilder::EmitConvI32ToF32() {
  EmitOp(OpCode::ConvI32ToF32);
}

void IrBuilder::EmitConvI32ToF64() {
  EmitOp(OpCode::ConvI32ToF64);
}

void IrBuilder::EmitConvF32ToI32() {
  EmitOp(OpCode::ConvF32ToI32);
}

void IrBuilder::EmitConvF64ToI32() {
  EmitOp(OpCode::ConvF64ToI32);
}

void IrBuilder::EmitConvF32ToF64() {
  EmitOp(OpCode::ConvF32ToF64);
}

void IrBuilder::EmitConvF64ToF32() {
  EmitOp(OpCode::ConvF64ToF32);
}

void IrBuilder::EmitAddI32() {
  EmitOp(OpCode::AddI32);
}

void IrBuilder::EmitAddI64() {
  EmitOp(OpCode::AddI64);
}

void IrBuilder::EmitAddF32() {
  EmitOp(OpCode::AddF32);
}

void IrBuilder::EmitAddF64() {
  EmitOp(OpCode::AddF64);
}

void IrBuilder::EmitAndI32() {
  EmitOp(OpCode::AndI32);
}

void IrBuilder::EmitOrI32() {
  EmitOp(OpCode::OrI32);
}

void IrBuilder::EmitXorI32() {
  EmitOp(OpCode::XorI32);
}

void IrBuilder::EmitShlI32() {
  EmitOp(OpCode::ShlI32);
}

void IrBuilder::EmitShrI32() {
  EmitOp(OpCode::ShrI32);
}

void IrBuilder::EmitJmp(IrLabel label) {
  EmitOp(OpCode::Jmp);
  EmitRel32Fixup(label);
}

void IrBuilder::EmitJmpTrue(IrLabel label) {
  EmitOp(OpCode::JmpTrue);
  EmitRel32Fixup(label);
}

void IrBuilder::EmitJmpFalse(IrLabel label) {
  EmitOp(OpCode::JmpFalse);
  EmitRel32Fixup(label);
}

bool IrBuilder::Finish(std::vector<uint8_t>* out, std::string* error) {
  for (const auto& fixup : fixups_) {
    if (fixup.label_id >= label_offsets_.size()) {
      if (error) *error = "label id out of range";
      return false;
    }
    int64_t target = label_offsets_[fixup.label_id];
    if (target < 0) {
      if (error) *error = "label not bound";
      return false;
    }
    int64_t next = static_cast<int64_t>(fixup.patch_offset + 4);
    int64_t rel = target - next;
    uint32_t patch = static_cast<uint32_t>(static_cast<int32_t>(rel));
    code_[fixup.patch_offset + 0] = static_cast<uint8_t>(patch & 0xFFu);
    code_[fixup.patch_offset + 1] = static_cast<uint8_t>((patch >> 8) & 0xFFu);
    code_[fixup.patch_offset + 2] = static_cast<uint8_t>((patch >> 16) & 0xFFu);
    code_[fixup.patch_offset + 3] = static_cast<uint8_t>((patch >> 24) & 0xFFu);
  }
  *out = code_;
  return true;
}

void IrBuilder::EmitU8(uint8_t value) {
  code_.push_back(value);
}

void IrBuilder::EmitU16(uint16_t value) {
  code_.push_back(static_cast<uint8_t>(value & 0xFFu));
  code_.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void IrBuilder::EmitU32(uint32_t value) {
  code_.push_back(static_cast<uint8_t>(value & 0xFFu));
  code_.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
  code_.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
  code_.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

void IrBuilder::EmitU64(uint64_t value) {
  EmitU32(static_cast<uint32_t>(value & 0xFFFFFFFFu));
  EmitU32(static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFu));
}

void IrBuilder::EmitRel32Fixup(IrLabel label) {
  IrFixup fixup{label.id, code_.size()};
  fixups_.push_back(fixup);
  EmitU32(0);
}

uint32_t IrBuilder::FloatToBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint64_t IrBuilder::DoubleToBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

} // namespace simplevm
