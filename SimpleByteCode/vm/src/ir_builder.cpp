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
