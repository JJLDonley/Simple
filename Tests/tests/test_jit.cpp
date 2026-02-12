#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "opcode.h"
#include "sbc_emitter.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "test_utils.h"
#include "vm.h"

namespace Simple::VM::Tests {

using Simple::Byte::sbc::AppendI32;
using Simple::Byte::sbc::AppendI64;
using Simple::Byte::sbc::AppendU8;
using Simple::Byte::sbc::AppendU16;
using Simple::Byte::sbc::AppendU32;
using Simple::Byte::sbc::AppendU64;
using Simple::Byte::sbc::BuildModule;
using Simple::Byte::sbc::BuildModuleWithFunctionsAndSigs;
using Simple::Byte::sbc::SigSpec;
using Simple::Byte::sbc::WriteU32;

std::vector<uint8_t> BuildModuleWithFunctions(const std::vector<std::vector<uint8_t>>& funcs,
                                              const std::vector<uint16_t>& locals);
std::vector<uint8_t> BuildModuleWithFunctionsAndSig(const std::vector<std::vector<uint8_t>>& funcs,
                                                    const std::vector<uint16_t>& locals,
                                                    uint32_t ret_type_id,
                                                    uint16_t param_count,
                                                    const std::vector<uint32_t>& param_types);

std::vector<uint8_t> BuildJitTierModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Nop));
  }
  for (uint32_t i = 0; i < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCallIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
    AppendU32(entry, 0);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotCalleeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotCalleeDispatchModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotCallIndirectDispatchModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < 2; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
    AppendU32(entry, 0);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotTailCallDispatchModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitMixedPromotionDispatchModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 2);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 2);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> tier1_callee;
  AppendU8(tier1_callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(tier1_callee, 0);
  AppendU8(tier1_callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(tier1_callee, 0);
  AppendU8(tier1_callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> hot_callee;
  AppendU8(hot_callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(hot_callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(hot_callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(hot_callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(hot_callee, 0);
  AppendU8(hot_callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, tier1_callee, hot_callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitEntryOnlyHotModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(entry, 0, 0);
}

std::vector<uint8_t> BuildJitCompiledLocalsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitCompiledI32ArithmeticModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 10);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledScalarI32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(callee, static_cast<uint8_t>(-5));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegI8));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegU8));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI16));
  AppendU16(callee, static_cast<uint16_t>(7));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IncI16));
  AppendU8(callee, static_cast<uint8_t>(OpCode::DecI16));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(callee, static_cast<uint16_t>(7));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IncU16));
  AppendU8(callee, static_cast<uint8_t>(OpCode::DecU16));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstChar));
  AppendU16(callee, static_cast<uint16_t>('A'));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 9);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IncU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::DecU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 10);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ShlI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ShrI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::OrI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AndI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::XorI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledI64U64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 9);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IncU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::DecU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 9);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IncI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::DecI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AndI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::OrI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::XorI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ShlI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ShrI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledFloatOpsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(callee, 9.0f);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(callee, 2.0f);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(callee, 0.5f);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IncF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::DecF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(callee, 10.0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(callee, 4.0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(callee, 1.5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::NegF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IncF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::DecF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledConversionsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvI32ToI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvI32ToF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvF32ToF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvF64ToF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvI32ToF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledCompareScalarModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpGtU32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(callee, 1.5f);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(callee, 2.5f);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtF32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(callee, 3.0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(callee, 2.0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpGtF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledI32LocalsArithmeticModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 10);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitCompiledI32CompareModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, -3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 9);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 9);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpGeI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledCompareBoolIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
    AppendU32(entry, 0);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledCompareBoolTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitCompiledBranchModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(callee, jmp_offset, static_cast<uint32_t>(rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledBranchIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
    AppendU32(entry, 0);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(callee, jmp_offset, static_cast<uint32_t>(rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledBranchTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(callee, jmp_offset, static_cast<uint32_t>(rel));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitCompiledLoopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);

  size_t loop_start = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = callee.size();
  AppendI32(callee, 0);
  size_t loop_end = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(callee, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(callee, jmp_back_offset, static_cast<uint32_t>(back_rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildBenchMixedOpsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  size_t loop_start = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = code.size();
  AppendI32(code, 0);

  size_t loop_end = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(code, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(code, jmp_back_offset, static_cast<uint32_t>(back_rel));

  return BuildModule(code, 1, 1);
}

std::vector<uint8_t> BuildBenchCallsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
    AppendU32(entry, 0);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);

  size_t loop_start = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = callee.size();
  AppendI32(callee, 0);
  size_t loop_end = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(callee, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(callee, jmp_back_offset, static_cast<uint32_t>(back_rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitCompiledLoopIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
    AppendU32(entry, 0);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);

  size_t loop_start = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = callee.size();
  AppendI32(callee, 0);
  size_t loop_end = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(callee, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(callee, jmp_back_offset, static_cast<uint32_t>(back_rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitOpcodeHotLoopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);

  size_t loop_start = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = callee.size();
  AppendI32(callee, 0);
  size_t loop_end = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(callee, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(callee, jmp_back_offset, static_cast<uint32_t>(back_rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitOpcodeHotLoopIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);

  size_t loop_start = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = callee.size();
  AppendI32(callee, 0);
  size_t loop_end = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(callee, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(callee, jmp_back_offset, static_cast<uint32_t>(back_rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitOpcodeHotLoopTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);

  size_t loop_start = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = callee.size();
  AppendI32(callee, 0);
  size_t loop_end = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(callee, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(callee, jmp_back_offset, static_cast<uint32_t>(back_rel));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 1});
}

std::vector<uint8_t> BuildJitOpcodeHotBranchModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(callee, jmp_offset, static_cast<uint32_t>(rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotBranchTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(callee, jmp_offset, static_cast<uint32_t>(rel));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotBranchIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = callee.size();
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(callee, jmp_offset, static_cast<uint32_t>(rel));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotUnsupportedModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::Line));
  AppendU32(callee, 1);
  AppendU32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitTypedArrayFallbackModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitTier0Threshold + 1; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::NewArrayF64));
  AppendU32(callee, 0);
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(callee, 3.0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ArraySetF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ArrayGetF64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitTypedListFallbackModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitTier0Threshold + 1; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(callee, 0);
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ListGetI64));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledFallbackModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledFallbackTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitCompiledFallbackIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types;
  return BuildModuleWithFunctionsAndSig({entry, callee}, {0, 0}, 0, 0, param_types);
}

std::vector<uint8_t> BuildJitTier1FallbackModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitTier1FallbackNoReenableModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitTier1FallbackIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
    AppendU32(entry, 0);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types;
  return BuildModuleWithFunctionsAndSig({entry, callee}, {0, 0}, 0, 0, param_types);
}

std::vector<uint8_t> BuildJitTier1FallbackTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitFallbackDirectThenIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types;
  return BuildModuleWithFunctionsAndSig({entry, callee}, {0, 0}, 0, 0, param_types);
}

std::vector<uint8_t> BuildJitFallbackIndirectThenDirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types;
  return BuildModuleWithFunctionsAndSig({entry, callee}, {0, 0}, 0, 0, param_types);
}

std::vector<uint8_t> BuildJitOpcodeHotFallbackModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotFallbackNoReenableModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitDispatchAfterFallbackModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitParamCalleeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitTier0Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 7);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  SigSpec entry_sig{0, 0, {}};
  SigSpec callee_sig{0, 1, {0}};
  std::vector<std::vector<uint8_t>> funcs{entry, callee};
  std::vector<uint16_t> locals{0, 1};
  std::vector<uint32_t> sig_ids{0, 1};
  return BuildModuleWithFunctionsAndSigs(funcs, locals, sig_ids, {entry_sig, callee_sig});
}

std::vector<uint8_t> BuildJitOpcodeHotParamCalleeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  SigSpec entry_sig{0, 0, {}};
  SigSpec callee_sig{0, 1, {0}};
  std::vector<std::vector<uint8_t>> funcs{entry, callee};
  std::vector<uint16_t> locals{0, 1};
  std::vector<uint32_t> sig_ids{0, 1};
  return BuildModuleWithFunctionsAndSigs(funcs, locals, sig_ids, {entry_sig, callee_sig});
}

std::vector<uint8_t> BuildJitOpcodeHotI32CompareModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, -1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpGeI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotCompareBoolIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotCompareBoolTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitCompiledBoolOpsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledLocalsBoolChainModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitCompiledLocalBoolStoreModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitCompiledLocalBoolAndOrModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalBoolAndOrModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalBoolAndOrIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalBoolAndOrTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalBoolStoreModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalBoolStoreIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 1});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalBoolStoreTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 1});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalsBoolChainModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalsBoolChainIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotLocalsBoolChainTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotBoolOpsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotBoolOpsIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotBoolOpsTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(callee, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotI32LocalsArithmeticModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 12);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotI32LocalsArithmeticIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 2);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 12);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 5);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 2});
}

std::vector<uint8_t> BuildJitOpcodeHotI32ArithmeticModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 8);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotI32ArithmeticIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 9);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotI32ArithmeticTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> helper;
  AppendU8(helper, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(helper, 0);
  AppendU8(helper, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(helper, 2);
  AppendU8(helper, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  for (uint32_t i = 0; i < Simple::VM::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 8);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::DivI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 4);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

bool RunJitTierTest() {
  std::vector<uint8_t> module_bytes = BuildJitTierModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[0] != 1) {
    std::cerr << "expected entry call count 1, got " << exec.call_counts[0] << "\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for callee\n";
    return false;
  }
  if (exec.func_opcode_counts.size() < 2) {
    std::cerr << "expected opcode counts per function\n";
    return false;
  }
  if (exec.func_opcode_counts[0] < Simple::VM::kJitOpcodeThreshold) {
    std::cerr << "expected entry opcode count >= " << Simple::VM::kJitOpcodeThreshold << "\n";
    return false;
  }
  if (exec.jit_tiers[0] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for entry\n";
    return false;
  }
  if (exec.opcode_counts.size() != 256) {
    std::cerr << "expected 256 opcode counters\n";
    return false;
  }
  if (exec.opcode_counts[static_cast<uint8_t>(Simple::Byte::OpCode::Call)] == 0) {
    std::cerr << "expected CALL opcode count > 0\n";
    return false;
  }
  if (exec.compile_counts.size() < 2) {
    std::cerr << "expected compile counts for functions\n";
    return false;
  }
  if (exec.compile_counts[1] != 2) {
    std::cerr << "expected 2 compile events for callee, got " << exec.compile_counts[1] << "\n";
    return false;
  }
  if (exec.compile_ticks_tier0.size() < 2 || exec.compile_ticks_tier1.size() < 2) {
    std::cerr << "expected compile tick arrays for functions\n";
    return false;
  }
  if (exec.compile_ticks_tier0[1] == 0 || exec.compile_ticks_tier1[1] == 0) {
    std::cerr << "expected compile ticks for callee tiers\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2) {
    std::cerr << "expected jit dispatch counts for functions\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected jit dispatch count for callee\n";
    return false;
  }
  return true;
}

bool RunJitDispatchCallIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitCallIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2 || exec.jit_dispatch_counts.size() < 2) {
    std::cerr << "expected jit data for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for call_indirect callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected jit dispatch count for call_indirect callee\n";
    return false;
  }
  return true;
}

bool RunJitDispatchTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3 || exec.jit_dispatch_counts.size() < 3) {
    std::cerr << "expected jit data for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 3) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[2] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for tailcall callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts[2] == 0) {
    std::cerr << "expected jit dispatch count for tailcall callee\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotCalleeTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotCalleeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2 || exec.func_opcode_counts.size() < 2) {
    std::cerr << "expected jit data for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != 1) {
    std::cerr << "expected callee call count 1, got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.func_opcode_counts[1] < Simple::VM::kJitOpcodeThreshold) {
    std::cerr << "expected callee opcode count >= " << Simple::VM::kJitOpcodeThreshold << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot callee\n";
    return false;
  }
  if (exec.compile_counts.size() < 2) {
    std::cerr << "expected compile counts for functions\n";
    return false;
  }
  if (exec.compile_counts[1] == 0) {
    std::cerr << "expected compile count for opcode-hot callee\n";
    return false;
  }
  if (exec.compile_ticks_tier0.size() < 2 || exec.compile_ticks_tier0[1] == 0) {
    std::cerr << "expected tier0 compile tick for opcode-hot callee\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotCalleeTickTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotCalleeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.compile_ticks_tier0.size() < 2) {
    std::cerr << "expected tier0 compile ticks for functions\n";
    return false;
  }
  if (exec.compile_ticks_tier0[1] == 0) {
    std::cerr << "expected tier0 compile tick for opcode-hot callee\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotCalleeDispatchTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotCalleeDispatchModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2 || exec.func_opcode_counts.size() < 2) {
    std::cerr << "expected jit data for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != 2) {
    std::cerr << "expected callee call count 2, got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2 || exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected jit dispatch count for opcode-hot callee\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotCallIndirectDispatchTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotCallIndirectDispatchModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2 || exec.func_opcode_counts.size() < 2) {
    std::cerr << "expected jit data for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != 2) {
    std::cerr << "expected callee call count 2, got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot call_indirect callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2 || exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected jit dispatch count for opcode-hot call_indirect callee\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotTailCallDispatchTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotTailCallDispatchModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3 || exec.func_opcode_counts.size() < 3) {
    std::cerr << "expected jit data for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 3) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[2] != 2) {
    std::cerr << "expected callee call count 2, got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot tailcall callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 3 || exec.jit_dispatch_counts[2] == 0) {
    std::cerr << "expected jit dispatch count for opcode-hot tailcall callee\n";
    return false;
  }
  return true;
}

bool RunJitMixedPromotionDispatchTest() {
  std::vector<uint8_t> module_bytes = BuildJitMixedPromotionDispatchModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3 || exec.jit_dispatch_counts.size() < 3) {
    std::cerr << "expected jit data for functions\n";
    return false;
  }
  if (exec.call_counts.size() < 3) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected tier1 callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.call_counts[2] != 2) {
    std::cerr << "expected opcode-hot callee call count 2, got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for call-count callee\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] == 0 || exec.jit_dispatch_counts[2] == 0) {
    std::cerr << "expected jit dispatch counts for both callees\n";
    return false;
  }
  return true;
}

bool RunJitEntryOnlyHotTest() {
  std::vector<uint8_t> module_bytes = BuildJitEntryOnlyHotModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 1 || exec.func_opcode_counts.size() < 1) {
    std::cerr << "expected jit data for entry\n";
    return false;
  }
  if (exec.func_opcode_counts[0] < Simple::VM::kJitOpcodeThreshold) {
    std::cerr << "expected entry opcode count >= " << Simple::VM::kJitOpcodeThreshold << "\n";
    return false;
  }
  if (exec.jit_tiers[0] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot entry\n";
    return false;
  }
  if (exec.compile_counts.size() < 1 || exec.compile_counts[0] == 0) {
    std::cerr << "expected compile count for opcode-hot entry\n";
    return false;
  }
  if (exec.compile_ticks_tier0.size() < 1 || exec.compile_ticks_tier0[0] == 0) {
    std::cerr << "expected tier0 compile tick for opcode-hot entry\n";
    return false;
  }
  return true;
}

bool RunJitCompileTickOrderingTest() {
  std::vector<uint8_t> module_bytes = BuildJitTierModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.compile_ticks_tier0.size() < 2 || exec.compile_ticks_tier1.size() < 2) {
    std::cerr << "expected compile tick arrays for functions\n";
    return false;
  }
  if (exec.compile_ticks_tier0[1] == 0 || exec.compile_ticks_tier1[1] == 0) {
    std::cerr << "expected compile ticks for callee tiers\n";
    return false;
  }
  if (exec.compile_ticks_tier0[1] >= exec.compile_ticks_tier1[1]) {
    std::cerr << "expected tier0 tick before tier1 for callee\n";
    return false;
  }
  return true;
}

bool RunJitCompiledLocalsTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledLocalsModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled-locals callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled-locals callee\n";
    return false;
  }
  return true;
}

bool RunJitCompiledI32ArithmeticTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledI32ArithmeticModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled arithmetic callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled arithmetic callee\n";
    return false;
  }
  if (exec.exit_code != 4) {
    std::cerr << "expected exit code 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledScalarI32Test() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledScalarI32Module();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for scalar i32 callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for scalar i32 callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledI64U64Test() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledI64U64Module();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for i64/u64 callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for i64/u64 callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledFloatOpsTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledFloatOpsModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for float callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for float callee\n";
    return false;
  }
  if (exec.exit_code != 4) {
    std::cerr << "expected exit code 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledConversionsTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledConversionsModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for conversion callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for conversion callee\n";
    return false;
  }
  if (exec.exit_code != 12) {
    std::cerr << "expected exit code 12, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledCompareScalarTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledCompareScalarModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compare callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compare callee\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledI32LocalsArithmeticTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledI32LocalsArithmeticModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled locals arithmetic callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled locals arithmetic callee\n";
    return false;
  }
  if (exec.exit_code != 4) {
    std::cerr << "expected exit code 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledI32CompareTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledI32CompareModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled compare callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled compare callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledCompareBoolIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledCompareBoolIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled compare+bool indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled compare+bool indirect callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for compare+bool indirect callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledCompareBoolTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledCompareBoolTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 3) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[2] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled compare+bool tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for compiled compare+bool tailcall callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 3) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[2] == 0) {
    std::cerr << "expected tier1 exec count for compare+bool tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledBranchTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledBranchModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled branch callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled branch callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for compiled branch callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledBranchIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledBranchIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled branch indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled branch indirect callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for compiled branch indirect callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledBranchTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledBranchTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 3) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[2] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled branch tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for compiled branch tailcall callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 3) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[2] == 0) {
    std::cerr << "expected tier1 exec count for compiled branch tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledLoopTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledLoopModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled loop callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled loop callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for compiled loop callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledLoopIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledLoopIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled loop indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled loop indirect callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for compiled loop indirect callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDifferentialTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledI32ArithmeticModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec_nojit.status != exec_jit.status) {
    std::cerr << "jit diff status\n";
    return false;
  }
  if (exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "jit diff exit code: " << exec_nojit.exit_code << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDifferentialBranchTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledBranchModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec_nojit.status != exec_jit.status) {
    std::cerr << "jit diff branch status\n";
    return false;
  }
  if (exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "jit diff branch exit code: " << exec_nojit.exit_code << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDifferentialLoopTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledLoopModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec_nojit.status != exec_jit.status) {
    std::cerr << "jit diff loop status\n";
    return false;
  }
  if (exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "jit diff loop exit code: " << exec_nojit.exit_code << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDifferentialCompareBoolTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledBoolOpsModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec_nojit.status != exec_jit.status) {
    std::cerr << "jit diff bool status\n";
    return false;
  }
  if (exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "jit diff bool exit code: " << exec_nojit.exit_code << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDifferentialIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledCompareBoolIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec_nojit.status != exec_jit.status) {
    std::cerr << "jit diff indirect status\n";
    return false;
  }
  if (exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "jit diff indirect exit code: " << exec_nojit.exit_code << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDifferentialTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledCompareBoolTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec_nojit.status != exec_jit.status) {
    std::cerr << "jit diff tailcall status\n";
    return false;
  }
  if (exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "jit diff tailcall exit code: " << exec_nojit.exit_code << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLoopTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLoopModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot loop callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot loop callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLoopIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLoopIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot loop indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot loop indirect callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLoopTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLoopTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot loop tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot loop tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTier1ExecCountTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledI32ArithmeticModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for callee\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[0] != 0) {
    std::cerr << "expected zero tier1 exec count for entry\n";
    return false;
  }
  return true;
}

bool RunJitTier1SkipNopTest() {
  std::vector<uint8_t> module_bytes = BuildJitTierModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts.size() < 2) {
    std::cerr << "expected tier1 exec counts for functions\n";
    return false;
  }
  if (exec.jit_tier1_exec_counts[1] == 0) {
    std::cerr << "expected tier1 exec count for callee\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotBranchTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotBranchModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot branch callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot branch callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotBranchTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotBranchTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot branch tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot branch tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotBranchIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotBranchIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot branch indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot branch indirect callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotUnsupportedTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotUnsupportedModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot unsupported callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs for unsupported callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTypedArrayFallbackTest() {
  std::vector<uint8_t> module_bytes = BuildJitTypedArrayFallbackModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for typed array callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs for typed array callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2) {
    std::cerr << "expected jit dispatch counts for functions\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected jit dispatch count for typed array callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTypedListFallbackTest() {
  std::vector<uint8_t> module_bytes = BuildJitTypedListFallbackModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for typed list callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs for typed list callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2) {
    std::cerr << "expected jit dispatch counts for functions\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected jit dispatch count for typed list callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledFallbackTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledFallbackModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for fallback callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledFallbackTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledFallbackTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for fallback tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledFallbackIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledFallbackIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for fallback indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTier1FallbackTest() {
  std::vector<uint8_t> module_bytes = BuildJitTier1FallbackModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for fallback tier1 callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTier1FallbackNoReenableTest() {
  std::vector<uint8_t> module_bytes = BuildJitTier1FallbackNoReenableModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for fallback no-reenable callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTier1FallbackIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitTier1FallbackIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for fallback tier1 indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTier1FallbackTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitTier1FallbackTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for fallback tier1 tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitFallbackDirectThenIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitFallbackDirectThenIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for fallback callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitFallbackIndirectThenDirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitFallbackIndirectThenDirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for fallback callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotFallbackTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotFallbackModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot fallback callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotFallbackNoReenableTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotFallbackNoReenableModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot fallback no-reenable callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDispatchAfterFallbackTest() {
  std::vector<uint8_t> module_bytes = BuildJitDispatchAfterFallbackModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2) {
    std::cerr << "expected jit dispatch counts for functions\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected dispatch count for fallback callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 1) {
    std::cerr << "expected exactly one compiled exec before fallback\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitParamCalleeTest() {
  std::vector<uint8_t> module_bytes = BuildJitParamCalleeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for param callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled execs for param callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotParamCalleeTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotParamCalleeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot param callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled execs for opcode-hot param callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitDisabledTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLoopModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, false);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::None) {
    std::cerr << "expected no jit tier when disabled\n";
    return false;
  }
  if (exec.compile_counts.size() < 2) {
    std::cerr << "expected compile counts for functions\n";
    return false;
  }
  if (exec.compile_counts[1] != 0) {
    std::cerr << "expected no compile counts when jit disabled\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2) {
    std::cerr << "expected jit dispatch counts for functions\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] != 0) {
    std::cerr << "expected no jit dispatch counts when jit disabled\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs when jit disabled\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

int RunBenchLoop(size_t iterations) {
  struct BenchCase {
    const char* name;
    std::vector<uint8_t> bytes;
  };
  std::vector<BenchCase> cases;
  cases.push_back({"single_type", BuildJitCompiledLoopModule()});
  cases.push_back({"mixed_ops", BuildBenchMixedOpsModule()});
  cases.push_back({"calls", BuildBenchCallsModule()});

  auto run_case = [&](const BenchCase& bench_case, bool enable_jit) {
    Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(bench_case.bytes);
    if (!load.ok) {
      std::cerr << "bench load failed (" << bench_case.name << "): " << load.error << "\n";
      return false;
    }
    Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      std::cerr << "bench verify failed (" << bench_case.name << "): " << vr.error << "\n";
      return false;
    }
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
      Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, enable_jit);
      if (exec.status != Simple::VM::ExecStatus::Halted) {
        std::cerr << "bench exec failed (" << bench_case.name << ")\n";
        return false;
      }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << bench_case.name << " " << (enable_jit ? "jit" : "nojit")
              << " iterations=" << iterations << " ms=" << ms << "\n";
    return true;
  };

  for (const auto& bench_case : cases) {
    if (!run_case(bench_case, false)) return 1;
    if (!run_case(bench_case, true)) return 1;
  }
  return 0;
}

bool RunJitOpcodeHotI32CompareTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotI32CompareModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot compare callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot compare callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotCompareBoolIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotCompareBoolIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot compare+bool indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot compare+bool indirect callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotCompareBoolTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotCompareBoolTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot compare+bool tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot compare+bool tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledBoolOpsTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledBoolOpsModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled bool ops callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled bool ops callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledLocalsBoolChainTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledLocalsBoolChainModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled locals bool chain callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled locals bool chain callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledLocalBoolStoreTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledLocalBoolStoreModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled local-bool callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled local-bool callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledLocalBoolAndOrTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledLocalBoolAndOrModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled local-bool and/or callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for compiled local-bool and/or callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalBoolAndOrTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalBoolAndOrModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot local-bool and/or callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot local-bool and/or callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalBoolAndOrIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalBoolAndOrIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot local-bool and/or indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot local-bool and/or indirect callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalBoolAndOrTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalBoolAndOrTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot local-bool and/or tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot local-bool and/or tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalBoolStoreTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalBoolStoreModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot local-bool callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot local-bool callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalBoolStoreIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalBoolStoreIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot local-bool indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot local-bool indirect callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalBoolStoreTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalBoolStoreTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot local-bool tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot local-bool tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalsBoolChainTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalsBoolChainModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot locals bool chain callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot locals bool chain callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalsBoolChainIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalsBoolChainIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot locals bool chain indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot locals bool chain indirect callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotLocalsBoolChainTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalsBoolChainTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot locals bool chain tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot locals bool chain tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotBoolOpsTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotBoolOpsModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot bool ops callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot bool ops callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotBoolOpsIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotBoolOpsIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot bool ops indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot bool ops indirect callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotBoolOpsTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotBoolOpsTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot bool ops tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot bool ops tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected exit code 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotI32LocalsArithmeticTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotI32LocalsArithmeticModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot locals arithmetic callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot locals arithmetic callee\n";
    return false;
  }
  if (exec.exit_code != 4) {
    std::cerr << "expected exit code 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotI32LocalsArithmeticIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotI32LocalsArithmeticIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot locals indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot locals indirect callee\n";
    return false;
  }
  if (exec.exit_code != 4) {
    std::cerr << "expected exit code 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotI32ArithmeticTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotI32ArithmeticModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot arithmetic callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot arithmetic callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotI32ArithmeticIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotI32ArithmeticIndirectModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot indirect callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot indirect callee\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitOpcodeHotI32ArithmeticTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotI32ArithmeticTailCallModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != Simple::VM::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot tailcall callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for opcode-hot tailcall callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

static const TestCase kJitTests[] = {
  {"jit_tier", RunJitTierTest},
  {"jit_call_indirect_dispatch", RunJitDispatchCallIndirectTest},
  {"jit_tailcall_dispatch", RunJitDispatchTailCallTest},
  {"jit_opcode_hot_callee", RunJitOpcodeHotCalleeTest},
  {"jit_opcode_hot_callee_tick", RunJitOpcodeHotCalleeTickTest},
  {"jit_opcode_hot_callee_dispatch", RunJitOpcodeHotCalleeDispatchTest},
  {"jit_opcode_hot_call_indirect_dispatch", RunJitOpcodeHotCallIndirectDispatchTest},
  {"jit_opcode_hot_tailcall_dispatch", RunJitOpcodeHotTailCallDispatchTest},
  {"jit_mixed_promotion_dispatch", RunJitMixedPromotionDispatchTest},
  {"jit_entry_only_hot", RunJitEntryOnlyHotTest},
  {"jit_compile_tick_order", RunJitCompileTickOrderingTest},
  {"jit_compiled_locals", RunJitCompiledLocalsTest},
  {"jit_compiled_i32_arith", RunJitCompiledI32ArithmeticTest},
  {"jit_compiled_scalar_i32", RunJitCompiledScalarI32Test},
  {"jit_compiled_i64_u64", RunJitCompiledI64U64Test},
  {"jit_compiled_float_ops", RunJitCompiledFloatOpsTest},
  {"jit_compiled_conversions", RunJitCompiledConversionsTest},
  {"jit_compiled_compare_scalar", RunJitCompiledCompareScalarTest},
  {"jit_compiled_i32_locals_arith", RunJitCompiledI32LocalsArithmeticTest},
  {"jit_compiled_i32_compare", RunJitCompiledI32CompareTest},
  {"jit_compiled_compare_bool_indirect", RunJitCompiledCompareBoolIndirectTest},
  {"jit_compiled_compare_bool_tailcall", RunJitCompiledCompareBoolTailCallTest},
  {"jit_compiled_branch", RunJitCompiledBranchTest},
  {"jit_compiled_branch_indirect", RunJitCompiledBranchIndirectTest},
  {"jit_compiled_branch_tailcall", RunJitCompiledBranchTailCallTest},
  {"jit_compiled_loop", RunJitCompiledLoopTest},
  {"jit_compiled_loop_indirect", RunJitCompiledLoopIndirectTest},
  {"jit_diff", RunJitDifferentialTest},
  {"jit_diff_branch", RunJitDifferentialBranchTest},
  {"jit_diff_loop", RunJitDifferentialLoopTest},
  {"jit_diff_bool", RunJitDifferentialCompareBoolTest},
  {"jit_diff_indirect", RunJitDifferentialIndirectTest},
  {"jit_diff_tailcall", RunJitDifferentialTailCallTest},
  {"jit_tier1_exec_count", RunJitTier1ExecCountTest},
  {"jit_tier1_skip_nop", RunJitTier1SkipNopTest},
  {"jit_opcode_hot_loop", RunJitOpcodeHotLoopTest},
  {"jit_opcode_hot_loop_indirect", RunJitOpcodeHotLoopIndirectTest},
  {"jit_opcode_hot_loop_tailcall", RunJitOpcodeHotLoopTailCallTest},
  {"jit_opcode_hot_branch", RunJitOpcodeHotBranchTest},
  {"jit_opcode_hot_branch_tailcall", RunJitOpcodeHotBranchTailCallTest},
  {"jit_opcode_hot_branch_indirect", RunJitOpcodeHotBranchIndirectTest},
  {"jit_opcode_hot_unsupported", RunJitOpcodeHotUnsupportedTest},
  {"jit_typed_array_fallback", RunJitTypedArrayFallbackTest},
  {"jit_typed_list_fallback", RunJitTypedListFallbackTest},
  {"jit_compiled_fallback", RunJitCompiledFallbackTest},
  {"jit_compiled_fallback_tailcall", RunJitCompiledFallbackTailCallTest},
  {"jit_compiled_fallback_indirect", RunJitCompiledFallbackIndirectTest},
  {"jit_tier1_fallback", RunJitTier1FallbackTest},
  {"jit_tier1_fallback_no_reenable", RunJitTier1FallbackNoReenableTest},
  {"jit_tier1_fallback_indirect", RunJitTier1FallbackIndirectTest},
  {"jit_tier1_fallback_tailcall", RunJitTier1FallbackTailCallTest},
  {"jit_fallback_direct_then_indirect", RunJitFallbackDirectThenIndirectTest},
  {"jit_fallback_indirect_then_direct", RunJitFallbackIndirectThenDirectTest},
  {"jit_opcode_hot_fallback", RunJitOpcodeHotFallbackTest},
  {"jit_opcode_hot_fallback_no_reenable", RunJitOpcodeHotFallbackNoReenableTest},
  {"jit_dispatch_after_fallback", RunJitDispatchAfterFallbackTest},
  {"jit_param_callee", RunJitParamCalleeTest},
  {"jit_opcode_hot_param_callee", RunJitOpcodeHotParamCalleeTest},
  {"jit_disabled", RunJitDisabledTest},
  {"jit_compiled_bool_ops", RunJitCompiledBoolOpsTest},
  {"jit_compiled_locals_bool_chain", RunJitCompiledLocalsBoolChainTest},
  {"jit_compiled_local_bool_store", RunJitCompiledLocalBoolStoreTest},
  {"jit_compiled_local_bool_and_or", RunJitCompiledLocalBoolAndOrTest},
  {"jit_opcode_hot_local_bool_and_or", RunJitOpcodeHotLocalBoolAndOrTest},
  {"jit_opcode_hot_local_bool_and_or_indirect", RunJitOpcodeHotLocalBoolAndOrIndirectTest},
  {"jit_opcode_hot_local_bool_and_or_tailcall", RunJitOpcodeHotLocalBoolAndOrTailCallTest},
  {"jit_opcode_hot_local_bool_store", RunJitOpcodeHotLocalBoolStoreTest},
  {"jit_opcode_hot_local_bool_store_indirect", RunJitOpcodeHotLocalBoolStoreIndirectTest},
  {"jit_opcode_hot_local_bool_store_tailcall", RunJitOpcodeHotLocalBoolStoreTailCallTest},
  {"jit_opcode_hot_locals_bool_chain", RunJitOpcodeHotLocalsBoolChainTest},
  {"jit_opcode_hot_locals_bool_chain_indirect", RunJitOpcodeHotLocalsBoolChainIndirectTest},
  {"jit_opcode_hot_locals_bool_chain_tailcall", RunJitOpcodeHotLocalsBoolChainTailCallTest},
  {"jit_opcode_hot_bool_ops", RunJitOpcodeHotBoolOpsTest},
  {"jit_opcode_hot_bool_ops_indirect", RunJitOpcodeHotBoolOpsIndirectTest},
  {"jit_opcode_hot_bool_ops_tailcall", RunJitOpcodeHotBoolOpsTailCallTest},
  {"jit_opcode_hot_i32_compare", RunJitOpcodeHotI32CompareTest},
  {"jit_opcode_hot_compare_bool_indirect", RunJitOpcodeHotCompareBoolIndirectTest},
  {"jit_opcode_hot_compare_bool_tailcall", RunJitOpcodeHotCompareBoolTailCallTest},
  {"jit_opcode_hot_i32_locals_arith", RunJitOpcodeHotI32LocalsArithmeticTest},
  {"jit_opcode_hot_i32_locals_arith_indirect", RunJitOpcodeHotI32LocalsArithmeticIndirectTest},
  {"jit_opcode_hot_i32_arith", RunJitOpcodeHotI32ArithmeticTest},
  {"jit_opcode_hot_i32_arith_indirect", RunJitOpcodeHotI32ArithmeticIndirectTest},
  {"jit_opcode_hot_i32_arith_tailcall", RunJitOpcodeHotI32ArithmeticTailCallTest},};

static const TestSection kJitSections[] = {
  {"jit", kJitTests, sizeof(kJitTests) / sizeof(kJitTests[0])},
};

const TestSection* GetJitSections(size_t* count) {
  if (count) {
    *count = sizeof(kJitSections) / sizeof(kJitSections[0]);
  }
  return kJitSections;
}

} // namespace Simple::VM::Tests
