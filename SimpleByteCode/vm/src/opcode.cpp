#include "opcode.h"

namespace simplevm {

bool GetOpInfo(uint8_t opcode, OpInfo* info) {
  switch (static_cast<OpCode>(opcode)) {
    case OpCode::Nop:
    case OpCode::Halt:
    case OpCode::Trap:
    case OpCode::Breakpoint:
    case OpCode::Pop:
    case OpCode::Dup:
    case OpCode::Dup2:
    case OpCode::Swap:
    case OpCode::Rot:
      *info = {0, 0, 0};
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
      *info = {4, opcode == static_cast<uint8_t>(OpCode::StoreLocal) ||
                      opcode == static_cast<uint8_t>(OpCode::StoreGlobal)
                  ? 1
                  : 0,
               opcode == static_cast<uint8_t>(OpCode::LoadLocal) ||
                      opcode == static_cast<uint8_t>(OpCode::LoadGlobal)
                  ? 1
                  : 0};
      return true;
    case OpCode::AddI32:
    case OpCode::SubI32:
    case OpCode::MulI32:
    case OpCode::DivI32:
    case OpCode::CmpEqI32:
    case OpCode::CmpLtI32:
    case OpCode::BoolAnd:
    case OpCode::BoolOr:
      *info = {0, 2, 1};
      return true;
    case OpCode::BoolNot:
      *info = {0, 1, 1};
      return true;
    case OpCode::Call:
    case OpCode::CallIndirect:
    case OpCode::TailCall:
      *info = {5, 0, 0};
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
  }
  return false;
}

} // namespace simplevm
