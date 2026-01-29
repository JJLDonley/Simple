#ifndef SIMPLE_VM_OPCODE_H
#define SIMPLE_VM_OPCODE_H

#include <cstdint>

namespace simplevm {

enum class OpCode : uint8_t {
  Nop = 0x00,
  Halt = 0x01,
  Trap = 0x02,
  Breakpoint = 0x03,
  Jmp = 0x04,
  JmpTrue = 0x05,
  JmpFalse = 0x06,

  Pop = 0x10,
  Dup = 0x11,
  Dup2 = 0x12,
  Swap = 0x13,
  Rot = 0x14,

  ConstI8 = 0x18,
  ConstI16 = 0x19,
  ConstI32 = 0x1A,
  ConstI64 = 0x1B,
  ConstI128 = 0x1C,
  ConstU8 = 0x1D,
  ConstU16 = 0x1E,
  ConstU32 = 0x1F,
  ConstU64 = 0x20,
  ConstU128 = 0x21,
  ConstF32 = 0x22,
  ConstF64 = 0x23,
  ConstBool = 0x24,
  ConstChar = 0x25,
  ConstString = 0x26,
  ConstNull = 0x27,

  LoadLocal = 0x30,
  StoreLocal = 0x31,
  LoadGlobal = 0x32,
  StoreGlobal = 0x33,

  AddI32 = 0x40,
  SubI32 = 0x41,
  MulI32 = 0x42,
  DivI32 = 0x43,
  ModI32 = 0x44,

  CmpEqI32 = 0x50,
  CmpLtI32 = 0x51,
  CmpNeI32 = 0x52,
  CmpLeI32 = 0x53,
  CmpGtI32 = 0x54,
  CmpGeI32 = 0x55,

  BoolNot = 0x60,
  BoolAnd = 0x61,
  BoolOr = 0x62,

  Call = 0x70,
  CallIndirect = 0x71,
  TailCall = 0x72,
  Ret = 0x73,
  Enter = 0x74,
  Leave = 0x75,

  Line = 0x80,
  ProfileStart = 0x81,
  ProfileEnd = 0x82,

  Intrinsic = 0x90,
  SysCall = 0x91,

  NewObject = 0xA0,
  NewClosure = 0xA1,
  LoadField = 0xA2,
  StoreField = 0xA3,
  IsNull = 0xA4,
  RefEq = 0xA5,
  RefNe = 0xA6,
  TypeOf = 0xA7,

  NewArray = 0xB0,
  ArrayLen = 0xB1,
  ArrayGetI32 = 0xB2,
  ArraySetI32 = 0xB3,

  NewList = 0xC0,
  ListLen = 0xC1,
  ListGetI32 = 0xC2,
  ListSetI32 = 0xC3,
  ListPushI32 = 0xC4,
  ListPopI32 = 0xC5,
  ListInsertI32 = 0xC6,
  ListRemoveI32 = 0xC7,
  ListClear = 0xC8,

  StringLen = 0xD0,
  StringConcat = 0xD1,
  StringGetChar = 0xD2,
  StringSlice = 0xD3,

  CallCheck = 0xE0,
};

struct OpInfo {
  int operand_bytes;
  int pops;
  int pushes;
};

bool GetOpInfo(uint8_t opcode, OpInfo* info);

} // namespace simplevm

#endif // SIMPLE_VM_OPCODE_H
