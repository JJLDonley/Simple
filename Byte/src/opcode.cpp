#include "opcode.h"

namespace Simple::Byte {

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
    case OpCode::JmpTable:
      *info = {8, 1, 0};
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
    case OpCode::IncU32:
    case OpCode::DecU32:
    case OpCode::IncU64:
    case OpCode::DecU64:
    case OpCode::IncI8:
    case OpCode::DecI8:
    case OpCode::IncI16:
    case OpCode::DecI16:
    case OpCode::IncU8:
    case OpCode::DecU8:
    case OpCode::IncU16:
    case OpCode::DecU16:
    case OpCode::NegI8:
    case OpCode::NegI16:
    case OpCode::NegU8:
    case OpCode::NegU16:
    case OpCode::NegU32:
    case OpCode::NegU64:
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
    case OpCode::NewArrayI64:
    case OpCode::NewArrayF32:
    case OpCode::NewArrayF64:
    case OpCode::NewArrayRef:
      *info = {8, 0, 1};
      return true;
    case OpCode::ArrayLen:
      *info = {0, 1, 1};
      return true;
    case OpCode::ArrayGetI32:
    case OpCode::ArrayGetI64:
    case OpCode::ArrayGetF32:
    case OpCode::ArrayGetF64:
    case OpCode::ArrayGetRef:
      *info = {0, 2, 1};
      return true;
    case OpCode::ArraySetI32:
    case OpCode::ArraySetI64:
    case OpCode::ArraySetF32:
    case OpCode::ArraySetF64:
    case OpCode::ArraySetRef:
      *info = {0, 3, 0};
      return true;
    case OpCode::NewList:
    case OpCode::NewListI64:
    case OpCode::NewListF32:
    case OpCode::NewListF64:
    case OpCode::NewListRef:
      *info = {8, 0, 1};
      return true;
    case OpCode::ListLen:
      *info = {0, 1, 1};
      return true;
    case OpCode::ListGetI32:
    case OpCode::ListGetI64:
    case OpCode::ListGetF32:
    case OpCode::ListGetF64:
    case OpCode::ListGetRef:
      *info = {0, 2, 1};
      return true;
    case OpCode::ListSetI32:
    case OpCode::ListSetI64:
    case OpCode::ListSetF32:
    case OpCode::ListSetF64:
    case OpCode::ListSetRef:
      *info = {0, 3, 0};
      return true;
    case OpCode::ListPushI32:
    case OpCode::ListPushI64:
    case OpCode::ListPushF32:
    case OpCode::ListPushF64:
    case OpCode::ListPushRef:
      *info = {0, 2, 0};
      return true;
    case OpCode::ListPopI32:
    case OpCode::ListPopI64:
    case OpCode::ListPopF32:
    case OpCode::ListPopF64:
    case OpCode::ListPopRef:
      *info = {0, 1, 1};
      return true;
    case OpCode::ListInsertI32:
    case OpCode::ListInsertI64:
    case OpCode::ListInsertF32:
    case OpCode::ListInsertF64:
    case OpCode::ListInsertRef:
      *info = {0, 3, 0};
      return true;
    case OpCode::ListRemoveI32:
    case OpCode::ListRemoveI64:
    case OpCode::ListRemoveF32:
    case OpCode::ListRemoveF64:
    case OpCode::ListRemoveRef:
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

const char* OpCodeName(uint8_t opcode) {
  switch (static_cast<OpCode>(opcode)) {
    case OpCode::Nop: return "Nop";
    case OpCode::Halt: return "Halt";
    case OpCode::Trap: return "Trap";
    case OpCode::Breakpoint: return "Breakpoint";
    case OpCode::Jmp: return "Jmp";
    case OpCode::JmpTrue: return "JmpTrue";
    case OpCode::JmpFalse: return "JmpFalse";
    case OpCode::JmpTable: return "JmpTable";
    case OpCode::Pop: return "Pop";
    case OpCode::Dup: return "Dup";
    case OpCode::Dup2: return "Dup2";
    case OpCode::Swap: return "Swap";
    case OpCode::Rot: return "Rot";
    case OpCode::ConstI8: return "ConstI8";
    case OpCode::ConstI16: return "ConstI16";
    case OpCode::ConstI32: return "ConstI32";
    case OpCode::ConstI64: return "ConstI64";
    case OpCode::ConstI128: return "ConstI128";
    case OpCode::ConstU8: return "ConstU8";
    case OpCode::ConstU16: return "ConstU16";
    case OpCode::ConstU32: return "ConstU32";
    case OpCode::ConstU64: return "ConstU64";
    case OpCode::ConstU128: return "ConstU128";
    case OpCode::ConstF32: return "ConstF32";
    case OpCode::ConstF64: return "ConstF64";
    case OpCode::ConstBool: return "ConstBool";
    case OpCode::ConstChar: return "ConstChar";
    case OpCode::ConstString: return "ConstString";
    case OpCode::ConstNull: return "ConstNull";
    case OpCode::LoadLocal: return "LoadLocal";
    case OpCode::StoreLocal: return "StoreLocal";
    case OpCode::LoadGlobal: return "LoadGlobal";
    case OpCode::StoreGlobal: return "StoreGlobal";
    case OpCode::LoadUpvalue: return "LoadUpvalue";
    case OpCode::StoreUpvalue: return "StoreUpvalue";
    case OpCode::NewListRef: return "NewListRef";
    case OpCode::ListGetRef: return "ListGetRef";
    case OpCode::ListSetRef: return "ListSetRef";
    case OpCode::ListPushRef: return "ListPushRef";
    case OpCode::ListPopRef: return "ListPopRef";
    case OpCode::ListInsertRef: return "ListInsertRef";
    case OpCode::ListRemoveRef: return "ListRemoveRef";
    case OpCode::AddI32: return "AddI32";
    case OpCode::SubI32: return "SubI32";
    case OpCode::MulI32: return "MulI32";
    case OpCode::DivI32: return "DivI32";
    case OpCode::ModI32: return "ModI32";
    case OpCode::AddI64: return "AddI64";
    case OpCode::SubI64: return "SubI64";
    case OpCode::MulI64: return "MulI64";
    case OpCode::DivI64: return "DivI64";
    case OpCode::ModI64: return "ModI64";
    case OpCode::AddF32: return "AddF32";
    case OpCode::SubF32: return "SubF32";
    case OpCode::MulF32: return "MulF32";
    case OpCode::DivF32: return "DivF32";
    case OpCode::AddF64: return "AddF64";
    case OpCode::SubF64: return "SubF64";
    case OpCode::MulF64: return "MulF64";
    case OpCode::DivF64: return "DivF64";
    case OpCode::NegI32: return "NegI32";
    case OpCode::NegI64: return "NegI64";
    case OpCode::IncI32: return "IncI32";
    case OpCode::DecI32: return "DecI32";
    case OpCode::IncI64: return "IncI64";
    case OpCode::DecI64: return "DecI64";
    case OpCode::IncF32: return "IncF32";
    case OpCode::DecF32: return "DecF32";
    case OpCode::IncF64: return "IncF64";
    case OpCode::DecF64: return "DecF64";
    case OpCode::IncU32: return "IncU32";
    case OpCode::DecU32: return "DecU32";
    case OpCode::IncU64: return "IncU64";
    case OpCode::DecU64: return "DecU64";
    case OpCode::IncI8: return "IncI8";
    case OpCode::DecI8: return "DecI8";
    case OpCode::IncI16: return "IncI16";
    case OpCode::DecI16: return "DecI16";
    case OpCode::IncU8: return "IncU8";
    case OpCode::DecU8: return "DecU8";
    case OpCode::IncU16: return "IncU16";
    case OpCode::DecU16: return "DecU16";
    case OpCode::NegI8: return "NegI8";
    case OpCode::NegI16: return "NegI16";
    case OpCode::NegU8: return "NegU8";
    case OpCode::NegU16: return "NegU16";
    case OpCode::NegU32: return "NegU32";
    case OpCode::NegU64: return "NegU64";
    case OpCode::NegF32: return "NegF32";
    case OpCode::NegF64: return "NegF64";
    case OpCode::CmpEqI32: return "CmpEqI32";
    case OpCode::CmpLtI32: return "CmpLtI32";
    case OpCode::CmpNeI32: return "CmpNeI32";
    case OpCode::CmpLeI32: return "CmpLeI32";
    case OpCode::CmpGtI32: return "CmpGtI32";
    case OpCode::CmpGeI32: return "CmpGeI32";
    case OpCode::CmpEqI64: return "CmpEqI64";
    case OpCode::CmpNeI64: return "CmpNeI64";
    case OpCode::CmpLtI64: return "CmpLtI64";
    case OpCode::CmpLeI64: return "CmpLeI64";
    case OpCode::CmpGtI64: return "CmpGtI64";
    case OpCode::CmpGeI64: return "CmpGeI64";
    case OpCode::CmpEqF32: return "CmpEqF32";
    case OpCode::CmpNeF32: return "CmpNeF32";
    case OpCode::CmpLtF32: return "CmpLtF32";
    case OpCode::CmpLeF32: return "CmpLeF32";
    case OpCode::CmpGtF32: return "CmpGtF32";
    case OpCode::CmpGeF32: return "CmpGeF32";
    case OpCode::CmpEqF64: return "CmpEqF64";
    case OpCode::CmpNeF64: return "CmpNeF64";
    case OpCode::CmpLtF64: return "CmpLtF64";
    case OpCode::CmpLeF64: return "CmpLeF64";
    case OpCode::CmpGtF64: return "CmpGtF64";
    case OpCode::CmpGeF64: return "CmpGeF64";
    case OpCode::BoolNot: return "BoolNot";
    case OpCode::BoolAnd: return "BoolAnd";
    case OpCode::BoolOr: return "BoolOr";
    case OpCode::Call: return "Call";
    case OpCode::CallIndirect: return "CallIndirect";
    case OpCode::TailCall: return "TailCall";
    case OpCode::Ret: return "Ret";
    case OpCode::Enter: return "Enter";
    case OpCode::Leave: return "Leave";
    case OpCode::ConvI32ToI64: return "ConvI32ToI64";
    case OpCode::ConvI64ToI32: return "ConvI64ToI32";
    case OpCode::ConvI32ToF32: return "ConvI32ToF32";
    case OpCode::ConvI32ToF64: return "ConvI32ToF64";
    case OpCode::ConvF32ToI32: return "ConvF32ToI32";
    case OpCode::ConvF64ToI32: return "ConvF64ToI32";
    case OpCode::ConvF32ToF64: return "ConvF32ToF64";
    case OpCode::ConvF64ToF32: return "ConvF64ToF32";
    case OpCode::Line: return "Line";
    case OpCode::ProfileStart: return "ProfileStart";
    case OpCode::ProfileEnd: return "ProfileEnd";
    case OpCode::Intrinsic: return "Intrinsic";
    case OpCode::SysCall: return "SysCall";
    case OpCode::NewObject: return "NewObject";
    case OpCode::NewClosure: return "NewClosure";
    case OpCode::LoadField: return "LoadField";
    case OpCode::StoreField: return "StoreField";
    case OpCode::IsNull: return "IsNull";
    case OpCode::RefEq: return "RefEq";
    case OpCode::RefNe: return "RefNe";
    case OpCode::TypeOf: return "TypeOf";
    case OpCode::NewListF64: return "NewListF64";
    case OpCode::ListGetF64: return "ListGetF64";
    case OpCode::ListSetF64: return "ListSetF64";
    case OpCode::ListPushF64: return "ListPushF64";
    case OpCode::ListPopF64: return "ListPopF64";
    case OpCode::ListInsertF64: return "ListInsertF64";
    case OpCode::ListRemoveF64: return "ListRemoveF64";
    case OpCode::NewArray: return "NewArray";
    case OpCode::ArrayLen: return "ArrayLen";
    case OpCode::ArrayGetI32: return "ArrayGetI32";
    case OpCode::ArraySetI32: return "ArraySetI32";
    case OpCode::NewArrayI64: return "NewArrayI64";
    case OpCode::ArrayGetI64: return "ArrayGetI64";
    case OpCode::ArraySetI64: return "ArraySetI64";
    case OpCode::NewArrayF32: return "NewArrayF32";
    case OpCode::ArrayGetF32: return "ArrayGetF32";
    case OpCode::ArraySetF32: return "ArraySetF32";
    case OpCode::NewArrayF64: return "NewArrayF64";
    case OpCode::ArrayGetF64: return "ArrayGetF64";
    case OpCode::ArraySetF64: return "ArraySetF64";
    case OpCode::NewArrayRef: return "NewArrayRef";
    case OpCode::ArrayGetRef: return "ArrayGetRef";
    case OpCode::ArraySetRef: return "ArraySetRef";
    case OpCode::NewList: return "NewList";
    case OpCode::ListLen: return "ListLen";
    case OpCode::ListGetI32: return "ListGetI32";
    case OpCode::ListSetI32: return "ListSetI32";
    case OpCode::ListPushI32: return "ListPushI32";
    case OpCode::ListPopI32: return "ListPopI32";
    case OpCode::ListInsertI32: return "ListInsertI32";
    case OpCode::ListRemoveI32: return "ListRemoveI32";
    case OpCode::ListClear: return "ListClear";
    case OpCode::NewListF32: return "NewListF32";
    case OpCode::ListGetF32: return "ListGetF32";
    case OpCode::ListSetF32: return "ListSetF32";
    case OpCode::ListPushF32: return "ListPushF32";
    case OpCode::ListPopF32: return "ListPopF32";
    case OpCode::ListInsertF32: return "ListInsertF32";
    case OpCode::ListRemoveF32: return "ListRemoveF32";
    case OpCode::StringLen: return "StringLen";
    case OpCode::StringConcat: return "StringConcat";
    case OpCode::StringGetChar: return "StringGetChar";
    case OpCode::StringSlice: return "StringSlice";
    case OpCode::CallCheck: return "CallCheck";
    case OpCode::AddU32: return "AddU32";
    case OpCode::SubU32: return "SubU32";
    case OpCode::MulU32: return "MulU32";
    case OpCode::DivU32: return "DivU32";
    case OpCode::ModU32: return "ModU32";
    case OpCode::AddU64: return "AddU64";
    case OpCode::SubU64: return "SubU64";
    case OpCode::MulU64: return "MulU64";
    case OpCode::DivU64: return "DivU64";
    case OpCode::ModU64: return "ModU64";
    case OpCode::CmpEqU32: return "CmpEqU32";
    case OpCode::CmpNeU32: return "CmpNeU32";
    case OpCode::CmpLtU32: return "CmpLtU32";
    case OpCode::CmpLeU32: return "CmpLeU32";
    case OpCode::CmpGtU32: return "CmpGtU32";
    case OpCode::CmpGeU32: return "CmpGeU32";
    case OpCode::CmpEqU64: return "CmpEqU64";
    case OpCode::CmpNeU64: return "CmpNeU64";
    case OpCode::CmpLtU64: return "CmpLtU64";
    case OpCode::CmpLeU64: return "CmpLeU64";
    case OpCode::CmpGtU64: return "CmpGtU64";
    case OpCode::CmpGeU64: return "CmpGeU64";
    case OpCode::AndI64: return "AndI64";
    case OpCode::OrI64: return "OrI64";
    case OpCode::XorI64: return "XorI64";
    case OpCode::ShlI64: return "ShlI64";
    case OpCode::ShrI64: return "ShrI64";
    case OpCode::NewListI64: return "NewListI64";
    case OpCode::ListGetI64: return "ListGetI64";
    case OpCode::ListSetI64: return "ListSetI64";
    case OpCode::ListPushI64: return "ListPushI64";
    case OpCode::ListPopI64: return "ListPopI64";
    case OpCode::ListInsertI64: return "ListInsertI64";
    case OpCode::ListRemoveI64: return "ListRemoveI64";
    case OpCode::AndI32: return "AndI32";
    case OpCode::OrI32: return "OrI32";
    case OpCode::XorI32: return "XorI32";
    case OpCode::ShlI32: return "ShlI32";
    case OpCode::ShrI32: return "ShrI32";
    default:
      return "Unknown";
  }
}

} // namespace Simple::Byte
