#include "ir_builder.h"

#include <cstring>

#include "sbc_emitter.h"

namespace simplevm {
namespace {

void AppendConstBlob(std::vector<uint8_t>& pool,
                     uint32_t kind,
                     const std::vector<uint8_t>& blob,
                     uint32_t* out_const_id,
                     uint32_t* out_payload_offset) {
  uint32_t const_id = static_cast<uint32_t>(pool.size());
  simplevm::sbc::AppendU32(pool, kind);
  uint32_t payload_offset = static_cast<uint32_t>(pool.size() + 4);
  simplevm::sbc::AppendU32(pool, payload_offset);
  simplevm::sbc::AppendU32(pool, static_cast<uint32_t>(blob.size()));
  pool.insert(pool.end(), blob.begin(), blob.end());
  if (out_const_id) *out_const_id = const_id;
  if (out_payload_offset) *out_payload_offset = payload_offset;
}

} // namespace

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

void IrBuilder::EmitConstI8(int8_t value) {
  EmitOp(OpCode::ConstI8);
  EmitU8(static_cast<uint8_t>(value));
}

void IrBuilder::EmitConstI16(int16_t value) {
  EmitOp(OpCode::ConstI16);
  EmitU16(static_cast<uint16_t>(value));
}

void IrBuilder::EmitConstU8(uint8_t value) {
  EmitOp(OpCode::ConstU8);
  EmitU8(value);
}

void IrBuilder::EmitConstU16(uint16_t value) {
  EmitOp(OpCode::ConstU16);
  EmitU16(value);
}

void IrBuilder::EmitConstU32(uint32_t value) {
  EmitOp(OpCode::ConstU32);
  EmitU32(value);
}

void IrBuilder::EmitConstU64(uint64_t value) {
  EmitOp(OpCode::ConstU64);
  EmitU64(value);
}

void IrBuilder::EmitConstChar(uint16_t value) {
  EmitOp(OpCode::ConstChar);
  EmitU16(value);
}

void IrBuilder::EmitConstString(uint32_t const_id) {
  EmitOp(OpCode::ConstString);
  EmitU32(const_id);
}

void IrBuilder::EmitConstNull() {
  EmitOp(OpCode::ConstNull);
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

void IrBuilder::EmitCallCheck() {
  EmitOp(OpCode::CallCheck);
}

void IrBuilder::EmitIntrinsic(uint32_t id) {
  EmitOp(OpCode::Intrinsic);
  EmitU32(id);
}

void IrBuilder::EmitSysCall(uint32_t id) {
  EmitOp(OpCode::SysCall);
  EmitU32(id);
}

void IrBuilder::EmitJmpTable(const std::vector<IrLabel>& cases, IrLabel default_label) {
  EmitOp(OpCode::JmpTable);
  std::vector<uint8_t> blob;
  simplevm::sbc::AppendU32(blob, static_cast<uint32_t>(cases.size()));
  for (size_t i = 0; i < cases.size(); ++i) {
    simplevm::sbc::AppendU32(blob, 0);
  }

  uint32_t const_id = 0;
  uint32_t payload_offset = 0;
  AppendConstBlob(const_pool_, 6, blob, &const_id, &payload_offset);
  EmitU32(const_id);
  EmitRel32Fixup(default_label);
  IrJmpTable table;
  table.table_base = code_.size();
  table.payload_offset = payload_offset;
  table.case_label_ids.reserve(cases.size());
  for (const auto& label : cases) {
    table.case_label_ids.push_back(label.id);
  }
  jmp_tables_.push_back(std::move(table));
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

void IrBuilder::EmitNewClosure(uint32_t method_id, uint8_t upvalue_count) {
  EmitOp(OpCode::NewClosure);
  EmitU32(method_id);
  EmitU8(upvalue_count);
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

void IrBuilder::EmitLoadUpvalue(uint32_t index) {
  EmitOp(OpCode::LoadUpvalue);
  EmitU32(index);
}

void IrBuilder::EmitStoreUpvalue(uint32_t index) {
  EmitOp(OpCode::StoreUpvalue);
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

void IrBuilder::EmitDup2() {
  EmitOp(OpCode::Dup2);
}

void IrBuilder::EmitSwap() {
  EmitOp(OpCode::Swap);
}

void IrBuilder::EmitRot() {
  EmitOp(OpCode::Rot);
}

void IrBuilder::EmitCmpEqI32() {
  EmitOp(OpCode::CmpEqI32);
}

void IrBuilder::EmitCmpLtI32() {
  EmitOp(OpCode::CmpLtI32);
}

void IrBuilder::EmitCmpNeI32() {
  EmitOp(OpCode::CmpNeI32);
}

void IrBuilder::EmitCmpLeI32() {
  EmitOp(OpCode::CmpLeI32);
}

void IrBuilder::EmitCmpGtI32() {
  EmitOp(OpCode::CmpGtI32);
}

void IrBuilder::EmitCmpGeI32() {
  EmitOp(OpCode::CmpGeI32);
}

void IrBuilder::EmitCmpEqI64() {
  EmitOp(OpCode::CmpEqI64);
}

void IrBuilder::EmitCmpNeI64() {
  EmitOp(OpCode::CmpNeI64);
}

void IrBuilder::EmitCmpLtI64() {
  EmitOp(OpCode::CmpLtI64);
}

void IrBuilder::EmitCmpLeI64() {
  EmitOp(OpCode::CmpLeI64);
}

void IrBuilder::EmitCmpGtI64() {
  EmitOp(OpCode::CmpGtI64);
}

void IrBuilder::EmitCmpGeI64() {
  EmitOp(OpCode::CmpGeI64);
}

void IrBuilder::EmitCmpEqF32() {
  EmitOp(OpCode::CmpEqF32);
}

void IrBuilder::EmitCmpNeF32() {
  EmitOp(OpCode::CmpNeF32);
}

void IrBuilder::EmitCmpLtF32() {
  EmitOp(OpCode::CmpLtF32);
}

void IrBuilder::EmitCmpLeF32() {
  EmitOp(OpCode::CmpLeF32);
}

void IrBuilder::EmitCmpGtF32() {
  EmitOp(OpCode::CmpGtF32);
}

void IrBuilder::EmitCmpGeF32() {
  EmitOp(OpCode::CmpGeF32);
}

void IrBuilder::EmitCmpEqF64() {
  EmitOp(OpCode::CmpEqF64);
}

void IrBuilder::EmitCmpNeF64() {
  EmitOp(OpCode::CmpNeF64);
}

void IrBuilder::EmitCmpLtF64() {
  EmitOp(OpCode::CmpLtF64);
}

void IrBuilder::EmitCmpLeF64() {
  EmitOp(OpCode::CmpLeF64);
}

void IrBuilder::EmitCmpGtF64() {
  EmitOp(OpCode::CmpGtF64);
}

void IrBuilder::EmitCmpGeF64() {
  EmitOp(OpCode::CmpGeF64);
}

void IrBuilder::EmitCmpEqU32() {
  EmitOp(OpCode::CmpEqU32);
}

void IrBuilder::EmitCmpNeU32() {
  EmitOp(OpCode::CmpNeU32);
}

void IrBuilder::EmitCmpLtU32() {
  EmitOp(OpCode::CmpLtU32);
}

void IrBuilder::EmitCmpLeU32() {
  EmitOp(OpCode::CmpLeU32);
}

void IrBuilder::EmitCmpGtU32() {
  EmitOp(OpCode::CmpGtU32);
}

void IrBuilder::EmitCmpGeU32() {
  EmitOp(OpCode::CmpGeU32);
}

void IrBuilder::EmitCmpEqU64() {
  EmitOp(OpCode::CmpEqU64);
}

void IrBuilder::EmitCmpNeU64() {
  EmitOp(OpCode::CmpNeU64);
}

void IrBuilder::EmitCmpLtU64() {
  EmitOp(OpCode::CmpLtU64);
}

void IrBuilder::EmitCmpLeU64() {
  EmitOp(OpCode::CmpLeU64);
}

void IrBuilder::EmitCmpGtU64() {
  EmitOp(OpCode::CmpGtU64);
}

void IrBuilder::EmitCmpGeU64() {
  EmitOp(OpCode::CmpGeU64);
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

void IrBuilder::EmitSubI32() {
  EmitOp(OpCode::SubI32);
}

void IrBuilder::EmitMulI32() {
  EmitOp(OpCode::MulI32);
}

void IrBuilder::EmitDivI32() {
  EmitOp(OpCode::DivI32);
}

void IrBuilder::EmitModI32() {
  EmitOp(OpCode::ModI32);
}

void IrBuilder::EmitAddI64() {
  EmitOp(OpCode::AddI64);
}

void IrBuilder::EmitSubI64() {
  EmitOp(OpCode::SubI64);
}

void IrBuilder::EmitMulI64() {
  EmitOp(OpCode::MulI64);
}

void IrBuilder::EmitDivI64() {
  EmitOp(OpCode::DivI64);
}

void IrBuilder::EmitModI64() {
  EmitOp(OpCode::ModI64);
}

void IrBuilder::EmitAddF32() {
  EmitOp(OpCode::AddF32);
}

void IrBuilder::EmitSubF32() {
  EmitOp(OpCode::SubF32);
}

void IrBuilder::EmitMulF32() {
  EmitOp(OpCode::MulF32);
}

void IrBuilder::EmitDivF32() {
  EmitOp(OpCode::DivF32);
}

void IrBuilder::EmitAddF64() {
  EmitOp(OpCode::AddF64);
}

void IrBuilder::EmitSubF64() {
  EmitOp(OpCode::SubF64);
}

void IrBuilder::EmitMulF64() {
  EmitOp(OpCode::MulF64);
}

void IrBuilder::EmitDivF64() {
  EmitOp(OpCode::DivF64);
}

void IrBuilder::EmitAddU32() {
  EmitOp(OpCode::AddU32);
}

void IrBuilder::EmitSubU32() {
  EmitOp(OpCode::SubU32);
}

void IrBuilder::EmitMulU32() {
  EmitOp(OpCode::MulU32);
}

void IrBuilder::EmitDivU32() {
  EmitOp(OpCode::DivU32);
}

void IrBuilder::EmitModU32() {
  EmitOp(OpCode::ModU32);
}

void IrBuilder::EmitAddU64() {
  EmitOp(OpCode::AddU64);
}

void IrBuilder::EmitSubU64() {
  EmitOp(OpCode::SubU64);
}

void IrBuilder::EmitMulU64() {
  EmitOp(OpCode::MulU64);
}

void IrBuilder::EmitDivU64() {
  EmitOp(OpCode::DivU64);
}

void IrBuilder::EmitModU64() {
  EmitOp(OpCode::ModU64);
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

void IrBuilder::EmitAndI64() {
  EmitOp(OpCode::AndI64);
}

void IrBuilder::EmitOrI64() {
  EmitOp(OpCode::OrI64);
}

void IrBuilder::EmitXorI64() {
  EmitOp(OpCode::XorI64);
}

void IrBuilder::EmitShlI64() {
  EmitOp(OpCode::ShlI64);
}

void IrBuilder::EmitShrI64() {
  EmitOp(OpCode::ShrI64);
}

void IrBuilder::EmitNegI32() {
  EmitOp(OpCode::NegI32);
}

void IrBuilder::EmitNegI64() {
  EmitOp(OpCode::NegI64);
}

void IrBuilder::EmitIncI32() {
  EmitOp(OpCode::IncI32);
}

void IrBuilder::EmitDecI32() {
  EmitOp(OpCode::DecI32);
}

void IrBuilder::EmitIncI64() {
  EmitOp(OpCode::IncI64);
}

void IrBuilder::EmitDecI64() {
  EmitOp(OpCode::DecI64);
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
  for (const auto& table : jmp_tables_) {
    if (table.payload_offset + 8 + table.case_label_ids.size() * 4 > const_pool_.size()) {
      if (error) *error = "jmp table const pool out of bounds";
      return false;
    }
    for (size_t i = 0; i < table.case_label_ids.size(); ++i) {
      uint32_t label_id = table.case_label_ids[i];
      if (label_id >= label_offsets_.size()) {
        if (error) *error = "label id out of range";
        return false;
      }
      int64_t target = label_offsets_[label_id];
      if (target < 0) {
        if (error) *error = "label not bound";
        return false;
      }
      int64_t rel = target - static_cast<int64_t>(table.table_base);
      uint32_t patch = static_cast<uint32_t>(static_cast<int32_t>(rel));
      size_t offset = static_cast<size_t>(table.payload_offset) + 8 + i * 4;
      const_pool_[offset + 0] = static_cast<uint8_t>(patch & 0xFFu);
      const_pool_[offset + 1] = static_cast<uint8_t>((patch >> 8) & 0xFFu);
      const_pool_[offset + 2] = static_cast<uint8_t>((patch >> 16) & 0xFFu);
      const_pool_[offset + 3] = static_cast<uint8_t>((patch >> 24) & 0xFFu);
    }
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
