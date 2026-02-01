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

namespace simplevm::tests {

using simplevm::sbc::AppendI32;
using simplevm::sbc::AppendI64;
using simplevm::sbc::AppendU8;
using simplevm::sbc::AppendU16;
using simplevm::sbc::AppendU32;
using simplevm::sbc::BuildModule;
using simplevm::sbc::BuildModuleWithFunctionsAndSigs;
using simplevm::sbc::SigSpec;
using simplevm::sbc::WriteU32;

std::vector<uint8_t> BuildModuleWithFunctions(const std::vector<std::vector<uint8_t>>& funcs,
                                              const std::vector<uint16_t>& locals);
std::vector<uint8_t> BuildModuleWithFunctionsAndSig(const std::vector<std::vector<uint8_t>>& funcs,
                                                    const std::vector<uint16_t>& locals,
                                                    uint32_t ret_type_id,
                                                    uint16_t param_count,
                                                    const std::vector<uint32_t>& param_types);

std::vector<uint8_t> BuildJitTierModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Nop));
  }
  for (uint32_t i = 0; i < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotCalleeDispatchModule() {
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotCallIndirectDispatchModule() {
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitOpcodeHotTailCallDispatchModule() {
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, helper, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitMixedPromotionDispatchModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < simplevm::kJitTier1Threshold; ++i) {
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(hot_callee, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(hot_callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(hot_callee, 0);
  AppendU8(hot_callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, tier1_callee, hot_callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitEntryOnlyHotModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::Nop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(entry, 0, 0);
}

std::vector<uint8_t> BuildJitCompiledLocalsModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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

std::vector<uint8_t> BuildJitCompiledI32LocalsArithmeticModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < simplevm::kJitTier0Threshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < simplevm::kJitTier0Threshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i < simplevm::kJitTier0Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
    AppendI32(entry, 7);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 1);
    if (i + 1 < simplevm::kJitTier0Threshold) {
      AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
    }
  }
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  for (uint32_t i = 0; i + 1 < simplevm::kJitTier1Threshold; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  using simplevm::OpCode;
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
  for (uint32_t i = 0; i < simplevm::kJitOpcodeThreshold + 1; ++i) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
    std::cerr << "expected Tier1 for callee\n";
    return false;
  }
  if (exec.func_opcode_counts.size() < 2) {
    std::cerr << "expected opcode counts per function\n";
    return false;
  }
  if (exec.func_opcode_counts[0] < simplevm::kJitOpcodeThreshold) {
    std::cerr << "expected entry opcode count >= " << simplevm::kJitOpcodeThreshold << "\n";
    return false;
  }
  if (exec.jit_tiers[0] != simplevm::JitTier::Tier0) {
    std::cerr << "expected Tier0 for entry\n";
    return false;
  }
  if (exec.opcode_counts.size() != 256) {
    std::cerr << "expected 256 opcode counters\n";
    return false;
  }
  if (exec.opcode_counts[static_cast<uint8_t>(simplevm::OpCode::Call)] == 0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.call_counts[2] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.func_opcode_counts[1] < simplevm::kJitOpcodeThreshold) {
    std::cerr << "expected callee opcode count >= " << simplevm::kJitOpcodeThreshold << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected tier1 callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.call_counts[2] != 2) {
    std::cerr << "expected opcode-hot callee call count 2, got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
    std::cerr << "expected Tier1 for call-count callee\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 1 || exec.func_opcode_counts.size() < 1) {
    std::cerr << "expected jit data for entry\n";
    return false;
  }
  if (exec.func_opcode_counts[0] < simplevm::kJitOpcodeThreshold) {
    std::cerr << "expected entry opcode count >= " << simplevm::kJitOpcodeThreshold << "\n";
    return false;
  }
  if (exec.jit_tiers[0] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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

bool RunJitCompiledI32LocalsArithmeticTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledI32LocalsArithmeticModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 3) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[2] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 3) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[2] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[2] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec_nojit = simplevm::ExecuteModule(load.module, true, false);
  simplevm::ExecResult exec_jit = simplevm::ExecuteModule(load.module, true, true);
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec_nojit = simplevm::ExecuteModule(load.module, true, false);
  simplevm::ExecResult exec_jit = simplevm::ExecuteModule(load.module, true, true);
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec_nojit = simplevm::ExecuteModule(load.module, true, false);
  simplevm::ExecResult exec_jit = simplevm::ExecuteModule(load.module, true, true);
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec_nojit = simplevm::ExecuteModule(load.module, true, false);
  simplevm::ExecResult exec_jit = simplevm::ExecuteModule(load.module, true, true);
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec_nojit = simplevm::ExecuteModule(load.module, true, false);
  simplevm::ExecResult exec_jit = simplevm::ExecuteModule(load.module, true, true);
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec_nojit = simplevm::ExecuteModule(load.module, true, false);
  simplevm::ExecResult exec_jit = simplevm::ExecuteModule(load.module, true, true);
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
    std::cerr << "expected Tier0 for param callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs for param callee\n";
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
    std::cerr << "expected Tier0 for opcode-hot param callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs for opcode-hot param callee\n";
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module, true, false);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::None) {
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
    simplevm::LoadResult load = simplevm::LoadModuleFromBytes(bench_case.bytes);
    if (!load.ok) {
      std::cerr << "bench load failed (" << bench_case.name << "): " << load.error << "\n";
      return false;
    }
    simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
    if (!vr.ok) {
      std::cerr << "bench verify failed (" << bench_case.name << "): " << vr.error << "\n";
      return false;
    }
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
      simplevm::ExecResult exec = simplevm::ExecuteModule(load.module, true, enable_jit);
      if (exec.status != simplevm::ExecStatus::Halted) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] != simplevm::kJitTier1Threshold) {
    std::cerr << "expected callee call count " << simplevm::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier1) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != simplevm::JitTier::Tier0) {
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[2] != simplevm::JitTier::Tier0) {
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

} // namespace simplevm::tests
