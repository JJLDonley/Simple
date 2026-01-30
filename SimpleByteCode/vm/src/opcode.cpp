#include "opcode.h"

namespace simplevm {

bool GetOpInfo(uint8_t opcode, OpInfo* info) {
  switch (static_cast<OpCode>(opcode)) {
    case OpCode::Nop:
    case OpCode::Halt:
    case OpCode::Trap:
    case OpCode::Breakpoint:
      *info = {0, 0, 0};
      return true;
    case OpCode::Pop:
      *info = {0, 1, 0};
      return true;
    case OpCode::Dup:
      *info = {0, 1, 2};
      return true;
    case OpCode::Dup2:
      *info = {0, 2, 4};
      return true;
    case OpCode::Swap:
      *info = {0, 2, 2};
      return true;
    case OpCode::Rot:
      *info = {0, 3, 3};
      return true;
    case OpCode::Jmp:
    case OpCode::JmpTrue:
    case OpCode::JmpFalse:
      *info = {4, opcode == static_cast<uint8_t>(OpCode::Jmp) ? 0 : 1, 0};
      return true;
    case OpCode::ConstI8:
    case OpCode::ConstU8:
    case OpCode::ConstBool:
      *info = {1, 0, 1};
      return true;
    case OpCode::ConstI16:
    case OpCode::ConstU16:
    case OpCode::ConstChar:
      *info = {2, 0, 1};
      return true;
    case OpCode::ConstI32:
    case OpCode::ConstU32:
    case OpCode::ConstF32:
    case OpCode::ConstString:
      *info = {4, 0, 1};
      return true;
    case OpCode::ConstI64:
    case OpCode::ConstU64:
    case OpCode::ConstF64:
      *info = {8, 0, 1};
      return true;
    case OpCode::ConstI128:
    case OpCode::ConstU128:
      *info = {4, 0, 1};
      return true;
    case OpCode::ConstNull:
      *info = {0, 0, 1};
      return true;
    case OpCode::LoadLocal:
    case OpCode::StoreLocal:
    case OpCode::LoadGlobal:
    case OpCode::StoreGlobal:
    case OpCode::LoadUpvalue:
    case OpCode::StoreUpvalue:
      *info = {4, opcode == static_cast<uint8_t>(OpCode::StoreLocal) ||
                      opcode == static_cast<uint8_t>(OpCode::StoreGlobal)
                      || opcode == static_cast<uint8_t>(OpCode::StoreUpvalue)
                  ? 1
                  : 0,
               opcode == static_cast<uint8_t>(OpCode::LoadLocal) ||
                      opcode == static_cast<uint8_t>(OpCode::LoadGlobal) ||
                      opcode == static_cast<uint8_t>(OpCode::LoadUpvalue)
                  ? 1
                  : 0};
      return true;
    case OpCode::AddI32:
    case OpCode::SubI32:
    case OpCode::MulI32:
    case OpCode::DivI32:
    case OpCode::ModI32:
    case OpCode::AddI64:
    case OpCode::SubI64:
    case OpCode::MulI64:
    case OpCode::DivI64:
    case OpCode::ModI64:
    case OpCode::AddU32:
    case OpCode::SubU32:
    case OpCode::MulU32:
    case OpCode::DivU32:
    case OpCode::ModU32:
    case OpCode::AddU64:
    case OpCode::SubU64:
    case OpCode::MulU64:
    case OpCode::DivU64:
    case OpCode::ModU64:
    case OpCode::AddF32:
    case OpCode::SubF32:
    case OpCode::MulF32:
    case OpCode::DivF32:
    case OpCode::AddF64:
    case OpCode::SubF64:
    case OpCode::MulF64:
    case OpCode::DivF64:
    case OpCode::CmpEqI32:
    case OpCode::CmpLtI32:
    case OpCode::CmpNeI32:
    case OpCode::CmpLeI32:
    case OpCode::CmpGtI32:
    case OpCode::CmpGeI32:
    case OpCode::CmpEqI64:
    case OpCode::CmpLtI64:
    case OpCode::CmpNeI64:
    case OpCode::CmpLeI64:
    case OpCode::CmpGtI64:
    case OpCode::CmpGeI64:
    case OpCode::CmpEqU32:
    case OpCode::CmpLtU32:
    case OpCode::CmpNeU32:
    case OpCode::CmpLeU32:
    case OpCode::CmpGtU32:
    case OpCode::CmpGeU32:
    case OpCode::CmpEqU64:
    case OpCode::CmpLtU64:
    case OpCode::CmpNeU64:
    case OpCode::CmpLeU64:
    case OpCode::CmpGtU64:
    case OpCode::CmpGeU64:
    case OpCode::CmpEqF32:
    case OpCode::CmpLtF32:
    case OpCode::CmpNeF32:
    case OpCode::CmpLeF32:
    case OpCode::CmpGtF32:
    case OpCode::CmpGeF32:
    case OpCode::CmpEqF64:
    case OpCode::CmpLtF64:
    case OpCode::CmpNeF64:
    case OpCode::CmpLeF64:
    case OpCode::CmpGtF64:
    case OpCode::CmpGeF64:
    case OpCode::BoolAnd:
    case OpCode::BoolOr:
      *info = {0, 2, 1};
      return true;
    case OpCode::NegI32:
    case OpCode::NegI64:
    case OpCode::IncI32:
    case OpCode::DecI32:
    case OpCode::IncI64:
    case OpCode::DecI64:
    case OpCode::IncF32:
    case OpCode::DecF32:
    case OpCode::IncF64:
    case OpCode::DecF64:
      *info = {0, 1, 1};
      return true;
    case OpCode::BoolNot:
      *info = {0, 1, 1};
      return true;
    case OpCode::Call:
    case OpCode::CallIndirect:
    case OpCode::TailCall:
      *info = {5, 0, 0};
      return true;
    case OpCode::ConvI32ToI64:
    case OpCode::ConvI64ToI32:
    case OpCode::ConvI32ToF32:
    case OpCode::ConvI32ToF64:
    case OpCode::ConvF32ToI32:
    case OpCode::ConvF64ToI32:
    case OpCode::ConvF32ToF64:
    case OpCode::ConvF64ToF32:
    case OpCode::NegF32:
    case OpCode::NegF64:
      *info = {0, 1, 1};
      return true;
    case OpCode::Ret:
    case OpCode::Leave:
      *info = {0, 0, 0};
      return true;
    case OpCode::Enter:
      *info = {2, 0, 0};
      return true;
    case OpCode::Line:
      *info = {8, 0, 0};
      return true;
    case OpCode::ProfileStart:
    case OpCode::ProfileEnd:
    case OpCode::Intrinsic:
    case OpCode::SysCall:
      *info = {4, 0, 0};
      return true;
    case OpCode::NewObject:
      *info = {4, 0, 1};
      return true;
    case OpCode::NewClosure:
      *info = {5, 0, 1};
      return true;
    case OpCode::LoadField:
      *info = {4, 1, 1};
      return true;
    case OpCode::StoreField:
      *info = {4, 2, 0};
      return true;
    case OpCode::IsNull:
      *info = {0, 1, 1};
      return true;
    case OpCode::RefEq:
    case OpCode::RefNe:
      *info = {0, 2, 1};
      return true;
    case OpCode::TypeOf:
      *info = {0, 1, 1};
      return true;
    case OpCode::NewArray:
      *info = {8, 0, 1};
      return true;
    case OpCode::ArrayLen:
      *info = {0, 1, 1};
      return true;
    case OpCode::ArrayGetI32:
      *info = {0, 2, 1};
      return true;
    case OpCode::ArraySetI32:
      *info = {0, 3, 0};
      return true;
    case OpCode::NewList:
      *info = {8, 0, 1};
      return true;
    case OpCode::ListLen:
      *info = {0, 1, 1};
      return true;
    case OpCode::ListGetI32:
      *info = {0, 2, 1};
      return true;
    case OpCode::ListSetI32:
      *info = {0, 3, 0};
      return true;
    case OpCode::ListPushI32:
      *info = {0, 2, 0};
      return true;
    case OpCode::ListPopI32:
      *info = {0, 1, 1};
      return true;
    case OpCode::ListInsertI32:
      *info = {0, 3, 0};
      return true;
    case OpCode::ListRemoveI32:
      *info = {0, 2, 1};
      return true;
    case OpCode::ListClear:
      *info = {0, 1, 0};
      return true;
    case OpCode::StringLen:
      *info = {0, 1, 1};
      return true;
    case OpCode::StringConcat:
      *info = {0, 2, 1};
      return true;
    case OpCode::AndI32:
    case OpCode::OrI32:
    case OpCode::XorI32:
    case OpCode::ShlI32:
    case OpCode::ShrI32:
    case OpCode::AndI64:
    case OpCode::OrI64:
    case OpCode::XorI64:
    case OpCode::ShlI64:
    case OpCode::ShrI64:
      *info = {0, 2, 1};
      return true;
    case OpCode::StringGetChar:
      *info = {0, 2, 1};
      return true;
    case OpCode::StringSlice:
      *info = {0, 3, 1};
      return true;
    case OpCode::CallCheck:
      *info = {0, 0, 0};
      return true;
  }
  return false;
}

} // namespace simplevm
