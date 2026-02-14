#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>
#include <string>
#include <vector>

#include "heap.h"
#include "intrinsic_ids.h"
#include "opcode.h"
#include "ir_lang.h"
#include "ir_builder.h"
#include "ir_compiler.h"
#include "sbc_emitter.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "scratch_arena.h"
#include "vm.h"
#include "test_utils.h"

namespace Simple::VM::Tests {

using Simple::Byte::sbc::AppendU8;
using Simple::Byte::sbc::AppendU16;
using Simple::Byte::sbc::AppendU32;
using Simple::Byte::sbc::AppendU64;
using Simple::Byte::sbc::AppendI32;
using Simple::Byte::sbc::AppendI64;
using Simple::Byte::sbc::AppendStringToPool;
using Simple::Byte::sbc::AppendConstString;
using Simple::Byte::sbc::WriteU8;
using Simple::Byte::sbc::WriteU16;
using Simple::Byte::sbc::WriteU32;
using Simple::Byte::sbc::ReadU32At;
using Simple::Byte::sbc::BuildModule;
using Simple::Byte::sbc::BuildModuleWithTables;
using Simple::Byte::sbc::BuildModuleWithTablesAndSig;
using Simple::Byte::sbc::BuildModuleWithTablesAndSigAndDebug;
using Simple::Byte::sbc::BuildModuleWithFunctionsAndSigs;
using Simple::Byte::sbc::SectionData;
using Simple::Byte::sbc::SigSpec;

std::vector<uint8_t> BuildModuleWithStackMax(const std::vector<uint8_t>& code,
                                             uint32_t global_count,
                                             uint16_t local_count,
                                             uint32_t stack_max) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 7) continue;
    uint32_t func_offset = ReadU32At(module, off + 4);
    if (func_offset + 16 <= module.size()) {
      WriteU32(module, func_offset + 12, stack_max);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildModuleWithEntryMethodId(const std::vector<uint8_t>& code,
                                                  uint32_t global_count,
                                                  uint16_t local_count,
                                                  uint32_t entry_method_id) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  if (module.size() > 0x10 + 3) {
    WriteU32(module, 0x10, entry_method_id);
  }
  return module;
}

std::vector<uint8_t> BuildModuleWithFunctionOffset(const std::vector<uint8_t>& code,
                                                   uint32_t global_count,
                                                   uint16_t local_count,
                                                   uint32_t func_code_offset) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 7) continue;
    uint32_t func_offset = ReadU32At(module, off + 4);
    if (func_offset + 8 <= module.size()) {
      WriteU32(module, func_offset + 4, func_code_offset);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildModuleWithMethodCodeOffset(const std::vector<uint8_t>& code,
                                                     uint32_t global_count,
                                                     uint16_t local_count,
                                                     uint32_t method_code_offset) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 3) continue;
    uint32_t methods_offset = ReadU32At(module, off + 4);
    if (methods_offset + 8 <= module.size()) {
      WriteU32(module, methods_offset + 8, method_code_offset);
    }
    break;
  }
  return module;
}


std::vector<uint8_t> BuildModuleWithHeaderFlags(const std::vector<uint8_t>& code,
                                                uint32_t global_count,
                                                uint16_t local_count,
                                                uint8_t flags) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  if (module.size() > 0x07) {
    WriteU8(module, 0x07, flags);
  }
  return module;
}

std::vector<uint8_t> BuildModuleWithSigParamCount(const std::vector<uint8_t>& code,
                                                  uint32_t global_count,
                                                  uint16_t local_count,
                                                  uint16_t param_count) {
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  std::vector<uint32_t> param_types(static_cast<size_t>(param_count), 0);
  return BuildModuleWithTablesAndSig(code, const_pool, empty, empty, global_count, local_count,
                                     0, param_count, 0, 0, param_types);
}

std::vector<uint8_t> BuildModuleWithSigCallConv(const std::vector<uint8_t>& code,
                                                uint32_t global_count,
                                                uint16_t local_count,
                                                uint16_t call_conv) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 4) continue;
    uint32_t sig_offset = ReadU32At(module, off + 4);
    if (sig_offset + 8 <= module.size()) {
      WriteU16(module, sig_offset + 6, call_conv);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildModuleWithMethodFlags(const std::vector<uint8_t>& code,
                                                uint32_t global_count,
                                                uint16_t local_count,
                                                uint16_t flags) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 3) continue;
    uint32_t methods_offset = ReadU32At(module, off + 4);
    if (methods_offset + 12 <= module.size()) {
      WriteU16(module, methods_offset + 10, flags);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildModuleWithGlobalInitConst(const std::vector<uint8_t>& code,
                                                    uint32_t global_count,
                                                    uint16_t local_count,
                                                    uint32_t init_const_id) {
  std::vector<uint8_t> module = BuildModule(code, global_count, local_count);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 6) continue;
    uint32_t globals_offset = ReadU32At(module, off + 4);
    if (globals_offset + 16 <= module.size()) {
      WriteU32(module, globals_offset + 12, init_const_id);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildModuleWithTablesAndGlobalInitConst(const std::vector<uint8_t>& code,
                                                             const std::vector<uint8_t>& const_pool,
                                                             const std::vector<uint8_t>& types_bytes,
                                                             const std::vector<uint8_t>& fields_bytes,
                                                             uint32_t global_count,
                                                             uint16_t local_count,
                                                             uint32_t init_const_id) {
  std::vector<uint8_t> module =
      BuildModuleWithTables(code, const_pool, types_bytes, fields_bytes, global_count, local_count);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 6) continue;
    uint32_t globals_offset = ReadU32At(module, off + 4);
    if (globals_offset + 16 <= module.size()) {
      WriteU32(module, globals_offset + 12, init_const_id);
    }
    break;
  }
  return module;
}

void PatchGlobalTypeId(std::vector<uint8_t>& module, uint32_t global_index, uint32_t type_id) {
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 6) continue;
    uint32_t globals_offset = ReadU32At(module, off + 4);
    size_t entry_offset = static_cast<size_t>(globals_offset) + static_cast<size_t>(global_index) * 16u;
    if (entry_offset + 8 <= module.size()) {
      WriteU32(module, entry_offset + 4, type_id);
    }
    break;
  }
}

std::vector<uint8_t> BuildModuleWithFunctions(const std::vector<std::vector<uint8_t>>& funcs,
                                              const std::vector<uint16_t>& local_counts) {
  std::vector<uint32_t> sig_ids(funcs.size(), 0);
  SigSpec sig_spec;
  sig_spec.ret_type_id = 0;
  sig_spec.param_count = 0;
  return BuildModuleWithFunctionsAndSigs(funcs, local_counts, sig_ids, {sig_spec});
}

std::vector<uint8_t> BuildModuleWithFunctionsAndSig(const std::vector<std::vector<uint8_t>>& funcs,
                                                    const std::vector<uint16_t>& local_counts,
                                                    uint32_t ret_type_id,
                                                    uint16_t param_count,
                                                    const std::vector<uint32_t>& param_types) {
  std::vector<uint32_t> sig_ids(funcs.size(), 0);
  SigSpec sig_spec;
  sig_spec.ret_type_id = ret_type_id;
  sig_spec.param_count = param_count;
  sig_spec.param_types = param_types;
  return BuildModuleWithFunctionsAndSigs(funcs, local_counts, sig_ids, {sig_spec});
}

std::vector<uint8_t> BuildSimpleAddModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildGlobalModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 1, 0);
}

std::vector<uint8_t> BuildDupModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildSwapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Swap));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildRotModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Rot));
  AppendU8(code, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildPopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildDup2Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup2));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildModModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ModI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildLocalsArenaModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {1, 1});
}

std::vector<uint8_t> BuildLocalsArenaTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> mid;
  AppendU8(mid, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(mid, 1);
  AppendU8(mid, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(mid, 5);
  AppendU8(mid, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(mid, 0);
  AppendU8(mid, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(mid, 2);
  AppendU8(mid, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, mid, callee}, {1, 1, 1});
}

std::vector<uint8_t> BuildLeaveModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Leave));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildXorI32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::XorI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildXorI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 12);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::XorI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU32ArithExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildU64ArithExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ModU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildF32ArithExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 9.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 3.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 8.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::DivF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildF64ArithExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 9.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 3.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 8.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::DivF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildCmpI32ExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpNeI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildCmpI64ExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpNeI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGeI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildCmpF32ExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 1.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 1.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpNeF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 1.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 3.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGeF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildCmpF64ExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 1.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 1.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpNeF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 1.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 3.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGeF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildCmpU32ExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpNeU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildCmpU64ExtraModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpNeU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListSetI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListSetF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 1.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 7.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListSetF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 1.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 7.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListSetRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetRef));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t false_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, false_block);
  }
  return BuildModule(code, 0, 2);
}

std::vector<uint8_t> BuildBadNamedMethodSigLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> const_pool;
  uint32_t name_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, "bad_method"));

  std::vector<uint8_t> module = BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {});
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 3) continue;
    uint32_t methods_offset = ReadU32At(module, off + 4);
    WriteU32(module, methods_offset + 0, name_offset);
    WriteU32(module, methods_offset + 4, 1); // sig_id out of range (only 1 sig exists)
    break;
  }
  return module;
}
std::vector<uint8_t> BuildBoolModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t false_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, false_block);
  }
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildCmpModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 20);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t false_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, false_block);
  }
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBranchModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    size_t target = code.size() - 6; // start of false branch const
    PatchRel32(code, site, target);
  }
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildLocalModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildLoopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  size_t loop_start = 0;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  loop_start = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  AppendI32(code, static_cast<int32_t>(static_cast<int64_t>(loop_start) - static_cast<int64_t>(code.size() + 4)));
  size_t exit_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, exit_block);
  }
  return BuildModule(code, 0, 2);
}

std::vector<uint8_t> BuildRecursiveCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 5);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> fib;
  AppendU8(fib, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 2);
  AppendU8(fib, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_else = fib.size();
  AppendI32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::Ret));

  size_t else_pos = fib.size();
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::Call));
  AppendU32(fib, 1);
  AppendU8(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 2);
  AppendU8(fib, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::Call));
  AppendU32(fib, 1);
  AppendU8(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::Ret));

  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_else + 4);
  WriteU32(fib, jmp_else, static_cast<uint32_t>(rel));

  SigSpec entry_sig{0, 0, {}};
  SigSpec fib_sig{0, 1, {0}};
  std::vector<std::vector<uint8_t>> funcs{entry, fib};
  std::vector<uint16_t> locals{0, 1};
  std::vector<uint32_t> sig_ids{0, 1};
  return BuildModuleWithFunctionsAndSigs(funcs, locals, sig_ids, {entry_sig, fib_sig});
}

std::vector<uint8_t> BuildRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t false_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, false_block);
  }
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildUpvalueModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  std::vector<size_t> patch_sites;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreUpvalue));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadUpvalue));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpTrue));
  patch_sites.push_back(callee.size());
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t true_block = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(callee, patch_sites[0], true_block);

  std::vector<std::vector<uint8_t>> funcs = {entry, callee};
  std::vector<uint16_t> locals = {1, 0};
  return BuildModuleWithFunctions(funcs, locals);
}

std::vector<uint8_t> BuildUpvalueObjectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  std::vector<size_t> patch_sites;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadUpvalue));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpTrue));
  patch_sites.push_back(callee.size());
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t true_block = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(callee, patch_sites[0], true_block);

  std::vector<std::vector<uint8_t>> funcs = {entry, callee};
  std::vector<uint16_t> locals = {1, 0};
  return BuildModuleWithFunctions(funcs, locals);
}

std::vector<uint8_t> BuildUpvalueOrderModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(entry, 1);
  AppendU8(entry, 2);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  std::vector<size_t> patch_sites;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadUpvalue));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(callee.size());
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadUpvalue));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::JmpTrue));
  patch_sites.push_back(callee.size());
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  size_t false_block = callee.size();
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(callee, patch_sites[0], false_block);
  PatchRel32(callee, patch_sites[1], false_block);

  std::vector<std::vector<uint8_t>> funcs = {entry, callee};
  std::vector<uint16_t> locals = {1, 0};
  return BuildModuleWithFunctions(funcs, locals);
}

std::vector<uint8_t> BuildBadUpvalueTypeVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(code, 0);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadUpvalueIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::StoreUpvalue));
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Halt));

  std::vector<std::vector<uint8_t>> funcs = {entry, callee};
  std::vector<uint16_t> locals = {1, 0};
  return BuildModuleWithFunctions(funcs, locals);
}

std::vector<uint8_t> BuildNewClosureModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(code, 0);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildJmpTableDefaultEndModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  size_t const_id_offset = code.size();
  AppendU32(code, 0);
  size_t default_offset = code.size();
  AppendI32(code, 0);
  size_t table_base = code.size();

  size_t case0 = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t case1 = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t end_boundary = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  PatchRel32(code, default_offset, end_boundary);

  std::vector<uint8_t> blob;
  AppendU32(blob, 2);
  AppendI32(blob, static_cast<int32_t>(static_cast<int64_t>(case0) - static_cast<int64_t>(table_base)));
  AppendI32(blob, static_cast<int32_t>(static_cast<int64_t>(case1) - static_cast<int64_t>(table_base)));

  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 6, blob, &const_id);
  WriteU32(code, const_id_offset, const_id);

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildJmpTableDefaultStartModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  size_t const_id_offset = code.size();
  AppendU32(code, 0);
  size_t default_offset = code.size();
  AppendI32(code, 0);
  size_t table_base = code.size();

  size_t case0 = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t case1 = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t default_start = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  PatchRel32(code, default_offset, default_start);

  std::vector<uint8_t> blob;
  AppendU32(blob, 2);
  AppendI32(blob, static_cast<int32_t>(static_cast<int64_t>(case0) - static_cast<int64_t>(table_base)));
  AppendI32(blob, static_cast<int32_t>(static_cast<int64_t>(case1) - static_cast<int64_t>(table_base)));

  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 6, blob, &const_id);
  WriteU32(code, const_id_offset, const_id);

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildJmpTableEmptyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  size_t const_id_offset = code.size();
  AppendU32(code, 0);
  size_t default_offset = code.size();
  AppendI32(code, 0);
  size_t default_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  PatchRel32(code, default_offset, default_block);

  std::vector<uint8_t> blob;
  AppendU32(blob, 0);

  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 6, blob, &const_id);
  WriteU32(code, const_id_offset, const_id);

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadJmpTableKindModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  size_t str_offset = AppendStringToPool(const_pool, "x");
  uint32_t const_id = 0;
  AppendConstString(const_pool, static_cast<uint32_t>(str_offset), &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  AppendU32(code, const_id);
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadJmpTableBlobLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 6);
  uint32_t blob_offset = static_cast<uint32_t>(const_pool.size() + 4);
  AppendU32(const_pool, blob_offset);
  AppendU32(const_pool, 8);
  AppendU32(const_pool, 2);
  AppendU32(const_pool, 0);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  AppendU32(code, const_id);
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadJmpTableOobTargetModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  size_t const_id_offset = code.size();
  AppendU32(code, 0);
  AppendI32(code, 0x7FFFFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> blob;
  AppendU32(blob, 1);
  AppendI32(blob, static_cast<int32_t>(0x7FFFFFFF));

  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 6, blob, &const_id);
  WriteU32(code, const_id_offset, const_id);

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadJmpTableVerifyOobTargetModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  size_t const_id_offset = code.size();
  AppendU32(code, 0);
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> blob;
  AppendU32(blob, 1);
  AppendI32(blob, 0x7FFFFFFF);

  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 6, blob, &const_id);
  WriteU32(code, const_id_offset, const_id);

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadJmpTableVerifyDefaultOobModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTable));
  size_t const_id_offset = code.size();
  AppendU32(code, 0);
  AppendI32(code, 0x7FFFFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> blob;
  AppendU32(blob, 1);
  AppendI32(blob, 0);

  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 6, blob, &const_id);
  WriteU32(code, const_id_offset, const_id);

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadNewClosureVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(code, 999);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildArrayModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildArrayI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayI64));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildArrayF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF32));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 3.5f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildArrayF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF64));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 6.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildArrayRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetRef));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t false_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, false_block);
  }
  return BuildModule(code, 0, 2);
}

std::vector<uint8_t> BuildArrayLenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildListI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 20);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 30);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI64));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 2);
}

std::vector<uint8_t> BuildListF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 1.25f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.5f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 3.5f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 2);
}

std::vector<uint8_t> BuildListF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 1.5);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 3.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF64));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 2);
}

std::vector<uint8_t> BuildListRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertRef));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t false_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, false_block);
  }
  return BuildModule(code, 0, 3);
}

std::vector<uint8_t> BuildListInsertModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListRemoveModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 20);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListClearModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListClear));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListLenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildListGrowthModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildGcVmStressModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2000);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  size_t loop_start = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], loop_start);
  return BuildModule(code, 0, 2);
}

std::vector<uint8_t> BuildStringModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t hello_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hi"));
  uint32_t world_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "there"));
  uint32_t hello_const = 0;
  uint32_t world_const = 0;
  AppendConstString(const_pool, hello_off, &hello_const);
  AppendConstString(const_pool, world_off, &world_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, hello_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, world_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringConcat));
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildStringGetCharModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "ABC"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildStringSliceModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hello"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildFieldModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  // type 0: dummy
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  // type 1: object with 1 i32 field at offset 0
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 1);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0); // name_str
  AppendU32(fields, 0); // type_id (unused in VM)
  AppendU32(fields, 0); // offset
  AppendU32(fields, 1); // flags

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 99);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreField));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadField));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Swap));
  AppendU8(code, static_cast<uint8_t>(OpCode::TypeOf));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithTables(code, const_pool, types, fields, 0, 0);
}

std::vector<uint8_t> BuildBadFieldModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadField));
  AppendU32(code, 99);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithTables(code, const_pool, types, fields, 0, 0);
}

std::vector<uint8_t> BuildBadConstStringModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, 9999);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadTypeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadUnknownOpcodeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadOperandOverrunModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendU16(code, 0x1234);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadCodeAlignmentLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstU32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 1234);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstCharModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstChar));
  AppendU16(code, 65);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1234567890LL);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstU64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 9000000000ULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x3FF0000000000000ULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstI128Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> blob(16, 0x11);
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 1, blob, &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI128));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildConstU128Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> blob(16, 0x22);
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 2, blob, &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU128));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildI64ArithModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::DivI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegI32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -5);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, -7);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3FC00000u); // 1.5f
  AppendU8(code, static_cast<uint8_t>(OpCode::NegF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0xBFC00000u); // -1.5f
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x4004000000000000ULL); // 2.5
  AppendU8(code, static_cast<uint8_t>(OpCode::NegF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0xC004000000000000ULL); // -2.5
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecI32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3FC00000u); // 1.5f
  AppendU8(code, static_cast<uint8_t>(OpCode::IncF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3FC00000u); // 1.5f
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x4004000000000000ULL); // 2.5
  AppendU8(code, static_cast<uint8_t>(OpCode::IncF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x4004000000000000ULL); // 2.5
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU32WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU64WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecI8Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncI8));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecI8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecI16Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI16));
  AppendU16(code, 300);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncI16));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecI16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI16));
  AppendU16(code, 300);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU8Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU16Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 500);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU16));
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 500);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU8WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIncDecU16WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 0xFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::DecU16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 0xFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegI8Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -5);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegI16Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI16));
  AppendU16(code, 300);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -300);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU8Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU16Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU8WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU16WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegI8WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(code, 0x80);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -128);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegI16WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI16));
  AppendU16(code, 0x8000);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI16));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -32768);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU32WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildNegU64WrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildI64ModModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ModI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildF32ArithModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3FC00000u); // 1.5f
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x40100000u); // 2.25f
  AppendU8(code, static_cast<uint8_t>(OpCode::AddF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x40700000u); // 3.75f
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildF64ArithModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x3FF8000000000000ULL); // 1.5
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x4002000000000000ULL); // 2.25
  AppendU8(code, static_cast<uint8_t>(OpCode::AddF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x400E000000000000ULL); // 3.75
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConvIntModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI32ToI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConvFloatModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI32ToF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x40400000u); // 3.0f
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x40A00000u); // 5.0f
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x40200000u); // 2.5f
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x4004000000000000ULL); // 2.5
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x4010000000000000ULL); // 4.0
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x40800000u); // 4.0f
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x4018000000000000ULL); // 6.0
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, else_block);
  }
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU32ArithModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::ModU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU64CmpModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU32DivZeroModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::DivU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU32OverflowModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU64DivZeroModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::DivU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU64OverflowModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU32CmpBoundsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU64CmpBoundsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU32CmpMinMaxModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 0xFFFFFFFFu);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGeU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, else_block);
  }
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildU64CmpMinMaxModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0xFFFFFFFFFFFFFFFFULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGeU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  for (size_t site : patch_sites) {
    PatchRel32(code, site, else_block);
  }
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBitwiseI32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0xF0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0x0F);
  AppendU8(code, static_cast<uint8_t>(OpCode::OrI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShlI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0xFF0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0xFF0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShrI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildShiftMaskI32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 33);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShlI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0x40000000);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 33);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShrI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0x20000000);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBitwiseI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0xF0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0x0F);
  AppendU8(code, static_cast<uint8_t>(OpCode::OrI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShlI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0xFF0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0xFF0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShrI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildShiftMaskI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 65);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShlI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0x4000000000000000LL);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 65);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShrI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0x2000000000000000LL);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildReturnRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1); // ref_type
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "ok"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, types, empty, 0, 0);
}

std::vector<uint8_t> BuildDebugNoopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Breakpoint));
  AppendU8(code, static_cast<uint8_t>(OpCode::Line));
  AppendU32(code, 10);
  AppendU32(code, 20);
  AppendU8(code, static_cast<uint8_t>(OpCode::ProfileStart));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ProfileEnd));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildVerifyMetadataModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hi"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Line));
  AppendU32(code, 10);
  AppendU32(code, 20);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Line));
  AppendU32(code, 11);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> empty_params;
  std::vector<uint8_t> module =
      BuildModuleWithTablesAndSig(code, const_pool, types, {}, 1, 1, 0xFFFFFFFFu, 0, 0, 0, empty_params);
  PatchGlobalTypeId(module, 0, 1);
  return module;
}

std::vector<uint8_t> BuildVerifyMetadataNonRefGlobalModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint32_t> empty_params;
  std::vector<uint8_t> module =
      BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 1, 0, 0xFFFFFFFFu, 0, 0, 0, empty_params);
  PatchGlobalTypeId(module, 0, 0);
  return module;
}

std::vector<uint8_t> BuildIntrinsicTrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0000);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadIntrinsicIdVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0xFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadIntrinsicParamVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0020); // core.math.abs_i32
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildIntrinsicReturnVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0030); // core.time.mono_ns -> i64
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I64));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 0);
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, {}, types, {}, 0, 0, 0, 0, 0, 0, empty_params);
}

std::vector<uint8_t> BuildIntrinsicCoreModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hi"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0020); // abs_i32
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0023); // max_i32
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0022); // min_i32
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, -9);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0021); // abs_i64
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 3.5f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 2.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0026); // min_f32
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 1.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0029); // max_f64
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0030); // mono_ns
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0040); // rand_u32
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0041); // rand_u64
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0050); // write_stdout
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithTables(code, const_pool, {}, {}, 0, 1);
}

std::vector<uint8_t> BuildIntrinsicTimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0030); // mono_ns
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 0x0031); // wall_ns
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModuleWithTables(code, {}, {}, {}, 0, 0);
}

std::vector<uint8_t> BuildSysCallTrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::SysCall));
  AppendU32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadSysCallVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::SysCall));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadMergeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  size_t join = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], join);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadMergeHeightModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  size_t join = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], join);
  PatchRel32(code, patch_sites[2], join);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadMergeRefI32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  size_t join = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  PatchRel32(code, patch_sites[1], join);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStackUnderflowVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringConcatVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringConcat));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringGetCharVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringGetCharIdxVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringSliceVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringSliceStartVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringSliceEndVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadIsNullVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadRefEqVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadRefEqMixedVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadRefNeVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefNe));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadRefNeMixedVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefNe));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadTypeOfVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::TypeOf));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadLoadFieldTypeVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadField));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStoreFieldObjectVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreField));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStoreFieldValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreField));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayLenVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetIdxVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetIdxVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetI64ValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF32ValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF64ValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetRefValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListLenVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetIdxVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetI64ValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF32ValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF64ValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetRefValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPushValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveIdxVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListClearVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListClear));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringLenVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadBoolNotVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolNot));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadBoolAndVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadBoolAndMixedVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadBoolOrVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadBoolOrMixedVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadJmpCondVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadJmpFalseCondVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetArrVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetArrVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetListVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetListVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPushListVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopListVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertListVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveListVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListClearListVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListClear));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadLocalUninitModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildBadJumpBoundaryModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_operand = code.size();
  AppendI32(code, 0);
  size_t const_op = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 123);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, jmp_operand, const_op + 2);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadJumpOobModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_operand = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, jmp_operand, code.size() + 4);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadJmpRuntimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_operand = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  PatchRel32(code, jmp_operand, code.size() + 4);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadCallRuntimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 9999);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadJmpCondRuntimeModule(bool invert) {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, invert ? 0 : 1);
  AppendU8(code, static_cast<uint8_t>(invert ? OpCode::JmpFalse : OpCode::JmpTrue));
  size_t jmp_operand = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  PatchRel32(code, jmp_operand, code.size() + 4);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadJmpTrueRuntimeModule() {
  return BuildBadJmpCondRuntimeModule(false);
}

std::vector<uint8_t> BuildBadJmpFalseRuntimeModule() {
  return BuildBadJmpCondRuntimeModule(true);
}

std::vector<uint8_t> BuildBadGlobalUninitModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 1, 0);
}

std::vector<uint8_t> BuildGlobalInitStringModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 0);
  uint32_t str_offset = static_cast<uint32_t>(const_pool.size() + 4);
  AppendU32(const_pool, str_offset);
  const_pool.push_back('h');
  const_pool.push_back('i');
  const_pool.push_back(0);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildGlobalInitF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 3);
  AppendU32(const_pool, 0x3F800000u);

  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildGlobalInitF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 4);
  AppendU64(const_pool, 0x3FF0000000000000ULL);

  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendU64(code, 0x3FF0000000000000ULL);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadGlobalInitConstModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithGlobalInitConst(code, 1, 0, 0xFFFFFFF0u);
}

std::vector<uint8_t> BuildBadStringConstNoNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 0);
  uint32_t str_offset = static_cast<uint32_t>(const_pool.size() + 4);
  AppendU32(const_pool, str_offset);
  const_pool.push_back('a');
  const_pool.push_back('b');
  const_pool.push_back('c');

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadI128BlobLenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  std::vector<uint8_t> blob(8, 0xAA);
  AppendConstBlob(const_pool, 1, blob, &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI128));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadFieldOffsetLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 1);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0);
  AppendU32(fields, 0);
  AppendU32(fields, 8);
  AppendU32(fields, 0);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, empty, types, fields, 0, 0);
}

std::vector<uint8_t> BuildBadFieldSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 1);

  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0);
  AppendU32(fields, 1);
  AppendU32(fields, 2);
  AppendU32(fields, 0);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, empty, types, fields, 0, 0);
}

std::vector<uint8_t> BuildBadFieldAlignmentLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 1);

  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0);
  AppendU32(fields, 1);
  AppendU32(fields, 2);
  AppendU32(fields, 0);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, empty, types, fields, 0, 0);
}

std::vector<uint8_t> BuildBadTypeConstLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 5);
  AppendU32(const_pool, 99);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadGlobalInitTypeRuntimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 5);
  AppendU32(const_pool, 0);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildGoodStringConstLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 0);
  uint32_t str_offset = static_cast<uint32_t>(const_pool.size() + 4);
  AppendU32(const_pool, str_offset);
  const_pool.push_back('o');
  const_pool.push_back('k');
  const_pool.push_back(0);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildGoodI128BlobLenLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = 0;
  std::vector<uint8_t> blob(16, 0xCC);
  AppendConstBlob(const_pool, 1, blob, &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI128));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadParamLocalsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithSigParamCount(code, 0, 0, 1);
}

std::vector<uint8_t> BuildBadSigCallConvLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithSigCallConv(code, 0, 0, 2);
}

std::vector<uint8_t> BuildBadSigParamTypesMissingLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  std::vector<uint32_t> no_params;
  return BuildModuleWithTablesAndSig(code, const_pool, empty, empty, 0, 0, 0, 1, 0, 0, no_params);
}

std::vector<uint8_t> BuildBadSigParamTypeStartLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  std::vector<uint32_t> no_params;
  return BuildModuleWithTablesAndSig(code, const_pool, empty, empty, 0, 0, 0, 1, 0, 1, no_params);
}

std::vector<uint8_t> BuildBadSigParamTypeMisalignedLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  std::vector<uint32_t> one_param = {0};
  std::vector<uint8_t> module =
      BuildModuleWithTablesAndSig(code, const_pool, empty, empty, 0, 0, 0, 1, 0, 0, one_param);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 4) continue;
    uint32_t sig_offset = ReadU32At(module, off + 4);
    uint32_t sig_size = ReadU32At(module, off + 8);
    if (sig_offset + sig_size <= module.size() && sig_size > 0) {
      module[sig_offset + sig_size - 1] = 0;
      WriteU32(module, off + 8, sig_size - 1);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadSigParamTypeIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  std::vector<uint32_t> bad_param = {999};
  return BuildModuleWithTablesAndSig(code, const_pool, empty, empty, 0, 0, 0, 1, 0, 0, bad_param);
}

std::vector<uint8_t> BuildBadSigRetTypeIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndSig(code, const_pool, empty, empty, 0, 0, 999, 0, 0, 0, {});
}

std::vector<uint8_t> BuildBadSigTableTruncatedLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  std::vector<uint32_t> no_params;
  std::vector<uint8_t> module =
      BuildModuleWithTablesAndSig(code, const_pool, empty, empty, 0, 0, 0, 0, 0, 0, no_params);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 4) continue;
    uint32_t sig_size = ReadU32At(module, off + 8);
    if (sig_size > 0) {
      WriteU32(module, off + 8, sig_size - 4);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadSectionAlignmentLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 1) continue;
    uint32_t sec_offset = ReadU32At(module, off + 4);
    if (sec_offset + 1 <= module.size()) {
      WriteU32(module, off + 4, sec_offset + 1);
      module.push_back(0);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadSectionOverlapLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  bool have_types = false;
  bool have_fields = false;
  uint32_t types_off = 0;
  uint32_t types_size = 0;
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id == 1) {
      types_off = ReadU32At(module, off + 4);
      types_size = ReadU32At(module, off + 8);
      have_types = true;
    } else if (id == 2) {
      if (have_types && types_size > 0) {
        WriteU32(module, off + 4, types_off + (types_size > 4 ? types_size - 4 : 0));
        have_fields = true;
        break;
      }
    }
  }
  if (!have_fields && have_types) {
    for (uint32_t i = 0; i < section_count; ++i) {
      size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
      uint32_t id = ReadU32At(module, off + 0);
      if (id == 3) {
        WriteU32(module, off + 4, types_off + (types_size > 4 ? types_size - 4 : 0));
        break;
      }
    }
  }
  return module;
}

std::vector<uint8_t> BuildBadUnknownSectionIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  if (section_count > 0) {
    size_t off = static_cast<size_t>(section_table_offset);
    WriteU32(module, off + 0, 99);
  }
  return module;
}

std::vector<uint8_t> BuildBadDuplicateSectionIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  if (section_count > 1) {
    size_t off0 = static_cast<size_t>(section_table_offset);
    size_t off1 = off0 + 16u;
    uint32_t id0 = ReadU32At(module, off0 + 0);
    WriteU32(module, off1 + 0, id0);
  }
  return module;
}

std::vector<uint8_t> BuildBadSectionTableOobLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  if (section_count > 0) {
    WriteU32(module, 0x08, section_count + 50);
  }
  return module;
}

std::vector<uint8_t> BuildBadEndianHeaderLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  if (module.size() > 0x06) {
    module[0x06] = 0;
  }
  return module;
}

std::vector<uint8_t> BuildBadHeaderMagicLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x00, 0xDEADBEEFu);
  return module;
}

std::vector<uint8_t> BuildBadHeaderVersionLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU16(module, 0x04, 0x0002u);
  return module;
}

std::vector<uint8_t> BuildPastHeaderVersionLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU16(module, 0x04, 0x0000u);
  return module;
}

std::vector<uint8_t> BuildGoodHeaderVersionLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadHeaderReservedLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x14, 1);
  return module;
}

std::vector<uint8_t> BuildBadSectionCountZeroLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x08, 0);
  return module;
}

std::vector<uint8_t> BuildBadSectionTableMisalignedLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x0C, 2);
  return module;
}

std::vector<uint8_t> BuildBadSectionTableOffsetOobLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  if (module.size() > 8) {
    WriteU32(module, 0x0C, static_cast<uint32_t>(module.size() - 8));
  }
  return module;
}

std::vector<uint8_t> BuildBadTypesTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 1) continue;
    uint32_t size = ReadU32At(module, off + 8);
    if (size >= 4) WriteU32(module, off + 8, size - 4);
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadTypeKindLoadModule() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 99);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  return BuildModuleWithTables({}, {}, types, {}, 0, 0);
}

std::vector<uint8_t> BuildBadImportsTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "core.os");
  std::vector<uint8_t> imports;
  AppendU32(imports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     imports, {});
}

std::vector<uint8_t> BuildBadImportsMissingConstPoolLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> imports;
  AppendU32(imports, 0); // module_name_str
  AppendU32(imports, 0); // symbol_name_str
  AppendU32(imports, 0); // sig_id
  AppendU32(imports, 0); // flags
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     imports, {});
}

std::vector<uint8_t> BuildBadExportsTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "main");
  std::vector<uint8_t> exports;
  AppendU32(exports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     {}, exports);
}

std::vector<uint8_t> BuildBadExportsMissingConstPoolLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> exports;
  AppendU32(exports, 0); // symbol_name_str
  AppendU32(exports, 0); // func_id
  AppendU32(exports, 0); // flags
  AppendU32(exports, 0); // reserved
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     {}, exports);
}

std::vector<uint8_t> BuildBadImportNameOffsetLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "core.os");
  std::vector<uint8_t> imports;
  AppendU32(imports, 0xFFFF); // module_name_str invalid
  AppendU32(imports, 0);      // symbol_name_str
  AppendU32(imports, 0);      // sig_id
  AppendU32(imports, 0);      // flags
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     imports, {});
}

std::vector<uint8_t> BuildBadImportSigIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "core.os");
  AppendStringToPool(const_pool, "args_count");
  std::vector<uint8_t> imports;
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 99); // sig_id invalid
  AppendU32(imports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     imports, {});
}

std::vector<uint8_t> BuildBadImportFlagsLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "core.os");
  AppendStringToPool(const_pool, "args_count");
  std::vector<uint8_t> imports;
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0x8000); // flags invalid
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     imports, {});
}

std::vector<uint8_t> BuildBadExportNameOffsetLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "main");
  std::vector<uint8_t> exports;
  AppendU32(exports, 0xFFFF); // name invalid
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     {}, exports);
}

std::vector<uint8_t> BuildBadExportFuncIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "main");
  std::vector<uint8_t> exports;
  AppendU32(exports, 0);
  AppendU32(exports, 99); // func_id invalid
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     {}, exports);
}

std::vector<uint8_t> BuildBadExportFlagsLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "main");
  std::vector<uint8_t> exports;
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0x8000); // flags invalid
  AppendU32(exports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     {}, exports);
}

std::vector<uint8_t> BuildBadExportReservedLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "main");
  std::vector<uint8_t> exports;
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 1); // reserved invalid
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     {}, exports);
}

std::vector<uint8_t> BuildBadImportDuplicateLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "core.os");
  AppendStringToPool(const_pool, "args_count");
  std::vector<uint8_t> imports;
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     imports, {});
}

std::vector<uint8_t> BuildBadExportDuplicateLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "main");
  std::vector<uint8_t> exports;
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  AppendU32(exports, 0);
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, {},
                                     {}, exports);
}

std::vector<uint8_t> BuildImportCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "args_count"));
  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, empty_params,
                                     imports, {});
}

std::vector<uint8_t> BuildImportCallHostModule() {
  using Simple::Byte::OpCode;
  using Simple::Byte::sbc::BuildModuleFromSections;
  using Simple::Byte::sbc::SectionData;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 41);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> methods;
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "host"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "add1"));

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, {}, 0, 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, {}, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});
  return BuildModuleFromSections(sections);
}

std::vector<uint8_t> BuildImportCallIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(code, 0);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "args_count"));
  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, empty_params,
                                     imports, {});
}

std::vector<uint8_t> BuildImportDlOpenNullModule() {
  using Simple::Byte::OpCode;
  using Simple::Byte::sbc::BuildModuleFromSections;
  using Simple::Byte::sbc::SectionData;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I64));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> methods;
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 2);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 2);

  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.dl"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t last_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "last_error"));

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, last_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, {}, 0, 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 3, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, {}, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});
  return BuildModuleFromSections(sections);
}

std::vector<uint8_t> BuildImportTimeMonoModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I64));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 0);
  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "time_mono_ns"));
  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, types, {}, 0, 0, 1, 0, 0, 0, empty_params,
                                     imports, {});
}

std::vector<uint8_t> BuildImportCwdGetModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "cwd_get"));
  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, types, {}, 0, 0, 1, 0, 0, 0, empty_params,
                                     imports, {});
}

std::vector<uint8_t> BuildImportTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(code, 1);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "args_count"));
  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, empty_params,
                                     imports, {});
}

std::vector<uint8_t> BuildImportArgsCountModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> const_pool;
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "args_count"));
  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, {}, {}, 0, 0, 0, 0, 0, 0, empty_params,
                                     imports, {});
}

std::vector<uint8_t> BuildImportArgsGetCharEqModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstChar));
  AppendU16(code, static_cast<uint16_t>('o'));
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "args_get"));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportEnvGetCharEqModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  uint32_t env_const = 0;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "env_get"));
  uint32_t env_name_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "SIMPLEVM_ENV_TEST"));
  AppendConstString(const_pool, env_name_off, &env_const);
  AppendU32(code, env_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstChar));
  AppendU16(code, static_cast<uint16_t>('b'));
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportEnvGetMissingModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  uint32_t env_const = 0;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "env_get"));
  uint32_t env_name_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "SIMPLEVM_ENV_MISSING"));
  AppendConstString(const_pool, env_name_off, &env_const);
  AppendU32(code, env_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportArgsGetIsNullModule(int32_t index_value) {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, index_value);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.os"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "args_get"));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsModule(const std::string& symbol,
                                         uint32_t ret_type_id,
                                         const std::vector<uint32_t>& param_types,
                                         const std::vector<uint8_t>& code) {
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, symbol));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, ret_type_id);
  AppendU16(sigs, static_cast<uint16_t>(param_types.size()));
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  for (uint32_t type_id : param_types) {
    AppendU32(sigs, type_id);
  }

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, sym_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsOpenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildImportFsModule("open", 0, {1, 0}, code);
}

std::vector<uint8_t> BuildImportFsReadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildImportFsModule("read", 0, {0, 1, 0}, code);
}

std::vector<uint8_t> BuildImportFsWriteModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildImportFsModule("write", 0, {0, 1, 0}, code);
}

std::vector<uint8_t> BuildImportFsCloseModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildImportFsModule("close", 0xFFFFFFFFu, {0}, code);
}

std::vector<uint8_t> BuildImportFsRoundTripModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_roundtrip.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'C');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadClampModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_read_clamp.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadBadFdModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildImportFsModule("read", 0, {0, 1, 0}, code);
}

std::vector<uint8_t> BuildImportFsWriteNullBufModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_null_buf.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadNonArrayBufModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_bad_buf.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsWriteBadFdModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildImportFsModule("write", 0, {0, 1, 0}, code);
}

std::vector<uint8_t> BuildImportFsCloseBadFdModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildImportFsModule("close", 0xFFFFFFFFu, {0}, code);
}

std::vector<uint8_t> BuildImportFsWriteClampModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_write_clamp.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsCloseTwiceModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_close_twice.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 1);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 3, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsOpenNullPathModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildImportFsModule("open", 0, {1, 0}, code);
}

std::vector<uint8_t> BuildImportFsReadZeroLenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_zero_len.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadAfterCloseModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_read_after_close.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsWriteAfterCloseModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_write_after_close.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsOpenCloseReopenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_reopen.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 1);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 3, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsWriteZeroLenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_write_zero.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail4 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);
  PatchRel32(code, patch_fail4, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadZeroBufModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_read_zero_buf.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Q');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsWriteZeroBufModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_write_zero_buf.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadClampNoOverwriteModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_read_no_overwrite.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'X');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Y');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Y');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail4 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);
  PatchRel32(code, patch_fail4, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsWriteAfterReadOnlyOpenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_readonly.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsOpenCloseLoopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_open_close_loop.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

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
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGeI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_done = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  AppendI32(code, static_cast<int32_t>(loop_start) - static_cast<int32_t>(code.size() + 4));

  size_t done_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  PatchRel32(code, patch_done, done_block);
  PatchRel32(code, patch_fail, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 1);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 3, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsOpenCloseStressModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_open_close_stress.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

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
  AppendI32(code, 50);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGeI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_done = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  AppendI32(code, static_cast<int32_t>(loop_start) - static_cast<int32_t>(code.size() + 4));

  size_t done_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  PatchRel32(code, patch_done, done_block);
  PatchRel32(code, patch_fail, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 1);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 3, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsWriteClampCountModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_write_clamp_count.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail4 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail5 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);
  PatchRel32(code, patch_fail4, fail_block);
  PatchRel32(code, patch_fail5, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadZeroLenPreserveModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_read_zero_preserve.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsWriteReadPersistModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_persist.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail4 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);
  PatchRel32(code, patch_fail4, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadWriteReopenCycleModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t write_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "write"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_rw_cycle.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail2 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail3 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail4 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'C');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'D');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::Pop));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail5 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'C');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail6 = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'D');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail7 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 4);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail, fail_block);
  PatchRel32(code, patch_fail2, fail_block);
  PatchRel32(code, patch_fail3, fail_block);
  PatchRel32(code, patch_fail4, fail_block);
  PatchRel32(code, patch_fail5, fail_block);
  PatchRel32(code, patch_fail6, fail_block);
  PatchRel32(code, patch_fail7, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 8);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, write_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 4);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 5, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportFsReadZeroLenNonEmptyBufModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.fs"));
  uint32_t open_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "open"));
  uint32_t read_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "read"));
  uint32_t close_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "close"));
  uint32_t path_off =
      static_cast<uint32_t>(AppendStringToPool(const_pool, "Tests/bin/sbc_fs_read_zero_nonempty.bin"));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_off, &path_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail_open = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, path_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_fail_open2 = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 2);
  AppendU8(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail_read = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'Z');
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t patch_fail_buf = code.size();
  AppendI32(code, 0);

  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 3);
  AppendU8(code, 1);

  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t fail_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_fail_open, fail_block);
  PatchRel32(code, patch_fail_open2, fail_block);
  PatchRel32(code, patch_fail_read, fail_block);
  PatchRel32(code, patch_fail_buf, fail_block);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 2);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU16(sigs, 3);
  AppendU16(sigs, 0);
  AppendU32(sigs, 2);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 1);
  AppendU16(sigs, 0);
  AppendU32(sigs, 5);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 12);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, open_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, read_off);
  AppendU32(imports, 2);
  AppendU32(imports, 0);
  AppendU32(imports, mod_off);
  AppendU32(imports, close_off);
  AppendU32(imports, 3);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 4, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildImportCoreLogModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t main_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "main"));
  uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "core.log"));
  uint32_t log_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "log"));

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'A');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 'B');
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;

  std::vector<uint8_t> methods;
  AppendU32(methods, main_off);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 1);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0xFFFFFFFFu);
  AppendU16(sigs, 2);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);
  AppendU32(sigs, 1);
  AppendU32(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off);
  AppendU32(imports, log_off);
  AppendU32(imports, 1);
  AppendU32(imports, 0);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, static_cast<uint32_t>(imports.size() / 16), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    const uint32_t size = static_cast<uint32_t>(sec.bytes.size());
    WriteU32(module, off + 8, size);
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildBadImportCallParamVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 1);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  std::vector<uint8_t> const_pool;
  AppendStringToPool(const_pool, "core.os");
  AppendStringToPool(const_pool, "args_count");
  std::vector<uint8_t> imports;
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  AppendU32(imports, 0);
  std::vector<uint32_t> params;
  params.push_back(0);
  return BuildModuleWithTablesAndSig(code, const_pool, types, {}, 0, 0, 0xFFFFFFFFu, 1, 0, 0, params,
                                     imports, {});
}

std::vector<uint8_t> BuildBadTypeKindSizeLoadModule() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 0);
  return BuildModuleWithTables({}, {}, types, {}, 0, 0);
}

std::vector<uint8_t> BuildBadTypeKindRefSizeLoadModule() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 12);
  AppendU32(types, 0);
  AppendU32(types, 0);
  return BuildModuleWithTables({}, {}, types, {}, 0, 0);
}

std::vector<uint8_t> BuildBadTypeKindFieldsLoadModule() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 1);
  AppendU32(types, 1);
  return BuildModuleWithTables({}, {}, types, {}, 0, 0);
}

std::vector<uint8_t> BuildBadTypeKindRefFieldsLoadModule() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 2);
  AppendU32(types, 1);
  return BuildModuleWithTables({}, {}, types, {}, 0, 0);
}

std::vector<uint8_t> BuildGoodTypeKindRefSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Ref));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  return BuildModuleWithTables(code, {}, types, {}, 0, 0);
}

std::vector<uint8_t> BuildBadFieldsTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 1);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0);
  AppendU32(fields, 0);
  AppendU32(fields, 0);
  AppendU32(fields, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModuleWithTables(code, const_pool, types, fields, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 2) continue;
    uint32_t size = ReadU32At(module, off + 8);
    if (size >= 4) WriteU32(module, off + 8, size - 4);
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadMethodsTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 3) continue;
    uint32_t size = ReadU32At(module, off + 8);
    if (size >= 4) WriteU32(module, off + 8, size - 4);
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadSigsTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 4) continue;
    uint32_t size = ReadU32At(module, off + 8);
    if (size >= 4) WriteU32(module, off + 8, size - 4);
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadGlobalsTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 1, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 6) continue;
    uint32_t size = ReadU32At(module, off + 8);
    if (size >= 4) WriteU32(module, off + 8, size - 4);
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadFunctionsTableSizeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 7) continue;
    uint32_t size = ReadU32At(module, off + 8);
    if (size >= 4) WriteU32(module, off + 8, size - 4);
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadTypeFieldRangeLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 1);

  std::vector<uint8_t> fields;
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithTables(code, const_pool, types, fields, 0, 0);
}

std::vector<uint8_t> BuildBadFieldTypeIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0);
  AppendU32(fields, 999);
  AppendU32(fields, 0);
  AppendU32(fields, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithTables(code, const_pool, types, fields, 0, 0);
}

std::vector<uint8_t> BuildBadGlobalTypeIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 1, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 6) continue;
    uint32_t globals_offset = ReadU32At(module, off + 4);
    if (globals_offset + 8 <= module.size()) {
      WriteU32(module, globals_offset + 4, 999);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadFunctionMethodIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 7) continue;
    uint32_t func_offset = ReadU32At(module, off + 4);
    if (func_offset + 4 <= module.size()) {
      WriteU32(module, func_offset + 0, 99);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildBadMethodSigIdLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 3) continue;
    uint32_t methods_offset = ReadU32At(module, off + 4);
    if (methods_offset + 8 <= module.size()) {
      WriteU32(module, methods_offset + 4, 99);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildMissingCodeSectionLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;
  std::vector<uint8_t> methods;
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;
  std::vector<uint8_t> functions;
  AppendU32(functions, 0);
  AppendU32(functions, 0);
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 1, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, 1, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    WriteU32(module, off + 8, static_cast<uint32_t>(sec.bytes.size()));
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildMissingFunctionsSectionLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> fields;
  std::vector<uint8_t> methods;
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, 0);
  AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);
  AppendU16(sigs, 0);
  AppendU16(sigs, 0);
  AppendU32(sigs, 0);

  std::vector<uint8_t> globals;

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 1, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u);
  WriteU16(module, 0x04, 0x0001u);
  WriteU8(module, 0x06, 1);
  WriteU8(module, 0x07, 0);
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    size_t off = table_off;
    WriteU32(module, off + 0, sec.id);
    WriteU32(module, off + 4, sec.offset);
    WriteU32(module, off + 8, static_cast<uint32_t>(sec.bytes.size()));
    WriteU32(module, off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildBadConstStringOffsetLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 0);
  AppendU32(const_pool, 0xFFFFFFF0u);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadConstI128OffsetLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 1);
  AppendU32(const_pool, 0xFFFFFFF0u);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadConstF64TruncatedLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 4);
  AppendU32(const_pool, 0x3FF00000u);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTablesAndGlobalInitConst(code, const_pool, empty, empty, 1, 0, const_id);
}

std::vector<uint8_t> BuildBadMethodFlagsLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithMethodFlags(code, 0, 0, 0x10);
}

std::vector<uint8_t> BuildBadHeaderFlagsLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithHeaderFlags(code, 0, 0, 1);
}

std::vector<uint8_t> BuildJumpToEndModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t jmp_operand = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, jmp_operand, code.size());
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStackMaxModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithStackMax(code, 0, 0, 1);
}

std::vector<uint8_t> BuildBadStackMaxZeroLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithStackMax(code, 0, 0, 0);
}

std::vector<uint8_t> BuildBadEntryMethodLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithEntryMethodId(code, 0, 0, 1);
}

std::vector<uint8_t> BuildBadFunctionOffsetLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithFunctionOffset(code, 0, 0, 4);
}

std::vector<uint8_t> BuildBadMethodOffsetLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithMethodCodeOffset(code, 0, 0, 4);
}

std::vector<uint8_t> BuildBadFunctionOverlapLoadModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 2);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> module = BuildModuleWithFunctions({entry, callee}, {0, 0});
  uint32_t section_count = ReadU32At(module, 0x08);
  uint32_t section_table_offset = ReadU32At(module, 0x0C);
  for (uint32_t i = 0; i < section_count; ++i) {
    size_t off = static_cast<size_t>(section_table_offset) + i * 16u;
    uint32_t id = ReadU32At(module, off + 0);
    if (id != 7) continue;
    uint32_t func_offset = ReadU32At(module, off + 4);
    if (func_offset + 32 <= module.size()) {
      WriteU32(module, func_offset + 4, 0);
      WriteU32(module, func_offset + 8, 8);
      WriteU32(module, func_offset + 16 + 4, 4);
      WriteU32(module, func_offset + 16 + 8, 8);
    }
    break;
  }
  return module;
}

std::vector<uint8_t> BuildCallCheckModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CallCheck));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildCallParamTypeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
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

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildCallIndirectModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
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
  AppendI32(callee, 9);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildCallIndirectParamTypeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildTailCallModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(entry, 1);
  AppendU8(entry, 0);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 42);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildBadCallIndirectVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(code, 0);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadCallIndirectFuncModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 99);
  AppendU8(code, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(code, 0);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadCallIndirectTypeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3f800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(code, 0);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildLineTrapModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Line));
  AppendU32(code, 10);
  AppendU32(code, 20);
  AppendU8(code, static_cast<uint8_t>(OpCode::Trap));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadCallVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 0);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadCallParamTypeVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(entry, 1);
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

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildBadCallParamI8ToI32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(entry, 7);
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

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildBadCallIndirectParamTypeVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildBadCallIndirectParamI8ToI32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildBadTailCallParamTypeVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(entry, 1);
  AppendU8(entry, 1);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildBadTailCallParamI8ToI32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(entry, 7);
  AppendU8(entry, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(entry, 1);
  AppendU8(entry, 1);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint32_t> param_types = {0};
  return BuildModuleWithFunctionsAndSig({entry, callee}, {1, 1}, 0, 1, param_types);
}

std::vector<uint8_t> BuildCmpMixedSmallTypesModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI16));
  AppendU16(code, 0xFFFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 0xFF);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU16));
  AppendU16(code, 0x00FF);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildArraySetI32WithCharModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  std::vector<size_t> patch_sites;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstChar));
  AppendU16(code, 65);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 65);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  patch_sites.push_back(code.size());
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_sites[0], else_block);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetI32BoolValueVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadTailCallVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(code, 0);
  AppendU8(code, 1);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadReturnVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadConvVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI32ToF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadConvRuntimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF32ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadConstI128KindModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> blob(16, 0x33);
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 2, blob, &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI128));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadConstU128BlobModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> blob(8, 0x44);
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 2, blob, &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU128));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadBitwiseVerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::AndI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadU32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadNegI32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadNegF32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadIncI32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadIncF32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadIncU32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadIncI8VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncI8));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadNegI8VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegI8));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadNegU32VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendU32(code, 0x3F800000u);
  AppendU8(code, static_cast<uint8_t>(OpCode::NegU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}
std::vector<uint8_t> BuildBadU64VerifyModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadBitwiseRuntimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::AndI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadU32RuntimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadU64RuntimeModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayLenNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetI64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetI64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 7.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF32NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 7.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF32NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 7.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 7.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 7.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetF64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 7.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetRefNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArraySetRefNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ArraySetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetI64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetI64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetF32NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetF32NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetF64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetF64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetRefNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetRefNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewArrayRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListLenNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetI64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetI64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 4.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetF32NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetF32NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 4.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 4.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetF64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetF64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 4.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetRefNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetRefNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListGetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetI64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetI64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 4.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 9.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF32NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 9.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF32NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 4.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 9.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 4.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 9.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 9.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetF64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 4.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 9.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetRefNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListSetRefNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListSetRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPushNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopI64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopF32NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopF64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListPopRefNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertI64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertI64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 9.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertF32NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 9.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertF32NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 9.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 9.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertF64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 9.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertF64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 9.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertRefNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertRefNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 9);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListInsertI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveI64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveI64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveI64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListI64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveF32Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 4.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveF32NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveF32NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF32));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(code, 4.0f);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveF64Module() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 4.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveF64NullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveF64NegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListF64));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 4.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveRefModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveRefNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListRemoveRefNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewListRef));
  AppendU32(code, 0);
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListRemoveRef));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListClearNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListClear));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringGetCharModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "A"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadStringLenNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringConcatNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::StringConcat));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringGetCharNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringGetCharNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "A"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringGetChar));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadStringSliceModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "abc"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildBadStringSliceNullModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringSliceNegIndexModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "abc"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, text_const);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, 0, 0);
}

std::vector<uint8_t> BuildGcModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  for (int i = 0; i < 1200; ++i) {
    AppendU8(code, static_cast<uint8_t>(OpCode::NewObject));
    AppendU32(code, 0);
    AppendU8(code, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpTrue));
  size_t patch_site = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t null_block = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  PatchRel32(code, patch_site, null_block);
  return BuildModule(code, 0, 1);
}
bool RunAddTest() {
  std::vector<uint8_t> module_bytes = BuildSimpleAddModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    if (!load.module.sigs.empty()) {
      std::cerr << "sig0 ret=" << load.module.sigs[0].ret_type_id
                << " params=" << load.module.sigs[0].param_count << "\n";
    }
    if (load.module.types.size() > 1) {
      const auto& t = load.module.types[1];
      std::cerr << "type1 flags=" << static_cast<int>(t.flags)
                << " size=" << t.size << "\n";
    }
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 42) {
    std::cerr << "expected 42, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunGlobalTest() {
  std::vector<uint8_t> module_bytes = BuildGlobalModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\n";
    std::cerr << "sigs=" << load.module.sigs.size() << " types=" << load.module.types.size()
              << " param_types=" << load.module.param_types.size() << "\n";
    if (!load.module.sigs.empty()) {
      const auto& sig = load.module.sigs[0];
      std::cerr << "sig0 ret=" << sig.ret_type_id << " params=" << sig.param_count
                << " param_start=" << sig.param_type_start << "\n";
    }
    if (load.module.types.size() > 1) {
      const auto& t = load.module.types[1];
      std::cerr << "type1 kind=" << static_cast<int>(t.kind)
                << " flags=" << static_cast<int>(t.flags)
                << " size=" << t.size << "\n";
    }
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunDupTest() {
  std::vector<uint8_t> module_bytes = BuildDupModule();
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
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 10) {
    std::cerr << "expected 10, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunSwapTest() {
  std::vector<uint8_t> module_bytes = BuildSwapModule();
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
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunRotTest() {
  std::vector<uint8_t> module_bytes = BuildRotModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 4) {
    std::cerr << "expected 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunPopTest() {
  std::vector<uint8_t> module_bytes = BuildPopModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunDup2Test() {
  std::vector<uint8_t> module_bytes = BuildDup2Module();
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
  if (exec.exit_code != 6) {
    std::cerr << "expected 6, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunLocalTest() {
  std::vector<uint8_t> module_bytes = BuildLocalModule();
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
  if (exec.exit_code != 9) {
    std::cerr << "expected 9, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunLoopTest() {
  std::vector<uint8_t> module_bytes = BuildLoopModule();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunFixtureTest(const char* path, int32_t expected_exit) {
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromFile(path);
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
  if (exec.exit_code != expected_exit) {
    std::cerr << "expected " << expected_exit << ", got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunFixtureAddTest() {
  return RunFixtureTest("Tests/tests/fixtures/add_i32.sbc", 9);
}

bool RunFixtureLoopTest() {
  return RunFixtureTest("Tests/tests/fixtures/loop.sbc", 3);
}

bool RunFixtureFibIterTest() {
  return RunFixtureTest("Tests/tests/fixtures/fib_iter.sbc", 55);
}

bool RunFixtureFibRecTest() {
  return RunFixtureTest("Tests/tests/fixtures/fib_rec.sbc", 5);
}

bool RunFixtureUuidLenTest() {
  return RunFixtureTest("Tests/tests/fixtures/uuid_len.sbc", 36);
}

bool RunRecursiveCallTest() {
  std::vector<uint8_t> module_bytes = BuildRecursiveCallModule();
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
  if (exec.exit_code != 5) {
    std::cerr << "expected 5, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunRecursiveCallJitTest() {
  std::vector<uint8_t> module_bytes = BuildRecursiveCallModule();
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
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.exit_code != 5) {
    std::cerr << "expected 5, got " << exec.exit_code << "\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs for recursive callee\n";
    return false;
  }
  return true;
}

bool RunRefTest() {
  std::vector<uint8_t> module_bytes = BuildRefModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunUpvalueTest() {
  std::vector<uint8_t> module_bytes = BuildUpvalueModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunUpvalueObjectTest() {
  std::vector<uint8_t> module_bytes = BuildUpvalueObjectModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunUpvalueOrderTest() {
  std::vector<uint8_t> module_bytes = BuildUpvalueOrderModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNewClosureTest() {
  std::vector<uint8_t> module_bytes = BuildNewClosureModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunArrayTest() {
  std::vector<uint8_t> module_bytes = BuildArrayModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunArrayI64Test() {
  std::vector<uint8_t> module_bytes = BuildArrayI64Module();
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
  if (exec.exit_code != 42) {
    std::cerr << "expected 42, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunArrayF32Test() {
  std::vector<uint8_t> module_bytes = BuildArrayF32Module();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunArrayF64Test() {
  std::vector<uint8_t> module_bytes = BuildArrayF64Module();
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
  if (exec.exit_code != 6) {
    std::cerr << "expected 6, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunArrayRefTest() {
  std::vector<uint8_t> module_bytes = BuildArrayRefModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunArrayLenTest() {
  std::vector<uint8_t> module_bytes = BuildArrayLenModule();
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
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 4) {
    std::cerr << "expected 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListTest() {
  std::vector<uint8_t> module_bytes = BuildListModule();
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
  if (exec.exit_code != 11) {
    std::cerr << "expected 11, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListI64Test() {
  std::vector<uint8_t> module_bytes = BuildListI64Module();
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
  if (exec.exit_code != 30) {
    std::cerr << "expected 30, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListF32Test() {
  std::vector<uint8_t> module_bytes = BuildListF32Module();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListF64Test() {
  std::vector<uint8_t> module_bytes = BuildListF64Module();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListRefTest() {
  std::vector<uint8_t> module_bytes = BuildListRefModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListLenTest() {
  std::vector<uint8_t> module_bytes = BuildListLenModule();
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
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListInsertTest() {
  std::vector<uint8_t> module_bytes = BuildListInsertModule();
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
  if (exec.exit_code != 5) {
    std::cerr << "expected 5, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListRemoveTest() {
  std::vector<uint8_t> module_bytes = BuildListRemoveModule();
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
  if (exec.exit_code != 10) {
    std::cerr << "expected 10, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListClearTest() {
  std::vector<uint8_t> module_bytes = BuildListClearModule();
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
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunStringTest() {
  std::vector<uint8_t> module_bytes = BuildStringModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunStringGetCharTest() {
  std::vector<uint8_t> module_bytes = BuildStringGetCharModule();
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
  if (exec.exit_code != 66) {
    std::cerr << "expected 66, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunStringSliceTest() {
  std::vector<uint8_t> module_bytes = BuildStringSliceModule();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstU32Test() {
  std::vector<uint8_t> module_bytes = BuildConstU32Module();
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
  if (exec.exit_code != 1234) {
    std::cerr << "expected 1234, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstCharTest() {
  std::vector<uint8_t> module_bytes = BuildConstCharModule();
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
  if (exec.exit_code != 65) {
    std::cerr << "expected 65, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstI64Test() {
  std::vector<uint8_t> module_bytes = BuildConstI64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstU64Test() {
  std::vector<uint8_t> module_bytes = BuildConstU64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstF32Test() {
  std::vector<uint8_t> module_bytes = BuildConstF32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstF64Test() {
  std::vector<uint8_t> module_bytes = BuildConstF64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstI128Test() {
  std::vector<uint8_t> module_bytes = BuildConstI128Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstU128Test() {
  std::vector<uint8_t> module_bytes = BuildConstU128Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunI64ArithTest() {
  std::vector<uint8_t> module_bytes = BuildI64ArithModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunI64ModTest() {
  std::vector<uint8_t> module_bytes = BuildI64ModModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegI32Test() {
  std::vector<uint8_t> module_bytes = BuildNegI32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegI64Test() {
  std::vector<uint8_t> module_bytes = BuildNegI64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegF32Test() {
  std::vector<uint8_t> module_bytes = BuildNegF32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegF64Test() {
  std::vector<uint8_t> module_bytes = BuildNegF64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecI32Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecI32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecI64Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecI64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecF32Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecF32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecF64Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecF64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU32Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecU32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU64Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecU64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU32WrapTest() {
  std::vector<uint8_t> module_bytes = BuildIncDecU32WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU64WrapTest() {
  std::vector<uint8_t> module_bytes = BuildIncDecU64WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecI8Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecI8Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecI16Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecI16Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU8Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecU8Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU16Test() {
  std::vector<uint8_t> module_bytes = BuildIncDecU16Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU8WrapTest() {
  std::vector<uint8_t> module_bytes = BuildIncDecU8WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIncDecU16WrapTest() {
  std::vector<uint8_t> module_bytes = BuildIncDecU16WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegI8Test() {
  std::vector<uint8_t> module_bytes = BuildNegI8Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegI16Test() {
  std::vector<uint8_t> module_bytes = BuildNegI16Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegU8Test() {
  std::vector<uint8_t> module_bytes = BuildNegU8Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegU16Test() {
  std::vector<uint8_t> module_bytes = BuildNegU16Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegU8WrapTest() {
  std::vector<uint8_t> module_bytes = BuildNegU8WrapModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\\n";
    return false;
  }
  return true;
}

bool RunNegU16WrapTest() {
  std::vector<uint8_t> module_bytes = BuildNegU16WrapModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "verify failed: " << vr.error << "\\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\\n";
    return false;
  }
  return true;
}
bool RunNegI8WrapTest() {
  std::vector<uint8_t> module_bytes = BuildNegI8WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegI16WrapTest() {
  std::vector<uint8_t> module_bytes = BuildNegI16WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}
bool RunF32ArithTest() {
  std::vector<uint8_t> module_bytes = BuildF32ArithModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegU32Test() {
  std::vector<uint8_t> module_bytes = BuildNegU32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegU64Test() {
  std::vector<uint8_t> module_bytes = BuildNegU64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegU32WrapTest() {
  std::vector<uint8_t> module_bytes = BuildNegU32WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunNegU64WrapTest() {
  std::vector<uint8_t> module_bytes = BuildNegU64WrapModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}
bool RunF64ArithTest() {
  std::vector<uint8_t> module_bytes = BuildF64ArithModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConvIntTest() {
  std::vector<uint8_t> module_bytes = BuildConvIntModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConvFloatTest() {
  std::vector<uint8_t> module_bytes = BuildConvFloatModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU32ArithTest() {
  std::vector<uint8_t> module_bytes = BuildU32ArithModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU64CmpTest() {
  std::vector<uint8_t> module_bytes = BuildU64CmpModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU32CmpBoundsTest() {
  std::vector<uint8_t> module_bytes = BuildU32CmpBoundsModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU64CmpBoundsTest() {
  std::vector<uint8_t> module_bytes = BuildU64CmpBoundsModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU32CmpMinMaxTest() {
  std::vector<uint8_t> module_bytes = BuildU32CmpMinMaxModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU64CmpMinMaxTest() {
  std::vector<uint8_t> module_bytes = BuildU64CmpMinMaxModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU32DivZeroTest() {
  std::vector<uint8_t> module_bytes = BuildU32DivZeroModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU32OverflowTest() {
  std::vector<uint8_t> module_bytes = BuildU32OverflowModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU64DivZeroTest() {
  std::vector<uint8_t> module_bytes = BuildU64DivZeroModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunU64OverflowTest() {
  std::vector<uint8_t> module_bytes = BuildU64OverflowModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBitwiseI32Test() {
  std::vector<uint8_t> module_bytes = BuildBitwiseI32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunShiftMaskI32Test() {
  std::vector<uint8_t> module_bytes = BuildShiftMaskI32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBitwiseI64Test() {
  std::vector<uint8_t> module_bytes = BuildBitwiseI64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunShiftMaskI64Test() {
  std::vector<uint8_t> module_bytes = BuildShiftMaskI64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunReturnRefTest() {
  std::vector<uint8_t> module_bytes = BuildReturnRefModule();
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
  return true;
}

bool RunDebugNoopTest() {
  std::vector<uint8_t> module_bytes = BuildDebugNoopModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunVerifyMetadataTest() {
  std::vector<uint8_t> module_bytes = BuildVerifyMetadataModule();
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
  if (vr.methods.size() != 1) {
    std::cerr << "expected 1 method info\n";
    return false;
  }
  const auto& info = vr.methods[0];
  if (info.locals.size() != 1 || info.locals[0] != Simple::Byte::VmType::Ref) {
    std::cerr << "expected local 0 to be ref\n";
    return false;
  }
  if (info.locals_ref_bits.empty() || (info.locals_ref_bits[0] & 0x1u) == 0) {
    std::cerr << "expected local ref bit set\n";
    return false;
  }
  if (vr.globals_ref_bits.empty() || (vr.globals_ref_bits[0] & 0x1u) == 0) {
    std::cerr << "expected global ref bit set\n";
    return false;
  }
  if (info.stack_maps.size() < 2) {
    std::cerr << "expected at least 2 stack maps\n";
    return false;
  }
  bool saw_empty = false;
  bool saw_ref = false;
  for (const auto& map : info.stack_maps) {
    if (map.stack_height == 0) {
      saw_empty = true;
    }
    if (map.stack_height == 1 && !map.ref_bits.empty() && (map.ref_bits[0] & 0x1u) != 0) {
      saw_ref = true;
    }
  }
  if (!saw_empty || !saw_ref) {
    std::cerr << "expected stack maps for empty and ref stack states\n";
    return false;
  }
  return true;
}

bool RunVerifyMetadataNonRefGlobalTest() {
  std::vector<uint8_t> module_bytes = BuildVerifyMetadataNonRefGlobalModule();
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
  if (vr.globals_ref_bits.empty()) {
    std::cerr << "expected globals ref bitmap\n";
    return false;
  }
  if ((vr.globals_ref_bits[0] & 0x1u) != 0) {
    std::cerr << "expected non-ref global bit to be clear\n";
    return false;
  }
  return true;
}

bool RunFieldTest() {
  std::vector<uint8_t> module_bytes = BuildFieldModule();
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
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 100) {
    std::cerr << "expected 100, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBadFieldVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadConstStringVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstStringModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadUnknownOpcodeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadUnknownOpcodeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  if (load.error.find("unknown opcode") == std::string::npos) {
    std::cerr << "expected unknown opcode error, got: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadOperandOverrunLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadOperandOverrunModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  if (load.error.find("opcode operands out of bounds") == std::string::npos) {
    std::cerr << "expected operand bounds error, got: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadCodeAlignmentLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadCodeAlignmentLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  if (load.error.find("opcode operands out of bounds") == std::string::npos) {
    std::cerr << "expected operand bounds error, got: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadMergeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadMergeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadMergeHeightVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadMergeHeightModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadMergeRefI32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadMergeRefI32Module();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStackUnderflowVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStackUnderflowVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringConcatVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringConcatVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringGetCharVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringGetCharVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringGetCharIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringGetCharIdxVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringSliceVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringSliceVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadNewClosureVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadNewClosureVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadUpvalueTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadUpvalueTypeVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringSliceStartVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringSliceStartVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringSliceEndVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringSliceEndVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadIsNullVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadIsNullVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefEqVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefEqVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefEqMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefEqMixedVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefNeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefNeVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefNeMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefNeMixedVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeOfVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeOfVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadLoadFieldTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadLoadFieldTypeVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStoreFieldObjectVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStoreFieldObjectVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStoreFieldValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStoreFieldValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArrayLenVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArrayLenVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArrayGetIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArrayGetIdxVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetIdxVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetI64ValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetI64ValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetF32ValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetF32ValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetF64ValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetF64ValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetRefValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetRefValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListLenVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListLenVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListGetIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListGetIdxVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetI64ValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetI64ValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetF32ValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetF32ValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetF64ValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetF64ValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetRefValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetRefValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPushValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPushValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPopVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPopVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListInsertValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListInsertValueVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListRemoveIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListRemoveIdxVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListClearVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListClearVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringLenVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringLenVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolNotVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolNotVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolAndVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolAndVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolAndMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolAndMixedVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolOrVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolOrVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolOrMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolOrMixedVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpCondVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpCondVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpFalseCondVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpFalseCondVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArrayGetArrVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArrayGetArrVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetArrVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetArrVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListGetListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListGetListVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetListVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPushListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPushListVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPopListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPopListVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListInsertListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListInsertListVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListRemoveListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListRemoveListVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListClearListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListClearListVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadLocalUninitVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadLocalUninitModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJumpBoundaryVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJumpBoundaryModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJumpOobVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJumpOobModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpRuntimeTrapTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpRuntimeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_jmp_runtime load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, false);
  if (exec.status != Simple::VM::ExecStatus::Trapped) {
    std::cerr << "bad_jmp_runtime expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunBadJmpTrueRuntimeTrapTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpTrueRuntimeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_jmp_true_runtime load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, false);
  if (exec.status != Simple::VM::ExecStatus::Trapped) {
    std::cerr << "bad_jmp_true_runtime expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunBadJmpFalseRuntimeTrapTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpFalseRuntimeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_jmp_false_runtime load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, false);
  if (exec.status != Simple::VM::ExecStatus::Trapped) {
    std::cerr << "bad_jmp_false_runtime expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunBadGlobalUninitVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalUninitModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunGlobalInitStringTest() {
  std::vector<uint8_t> module_bytes = BuildGlobalInitStringModule();
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
  if (exec.exit_code != 2) {
    std::cerr << "expected 2, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunGlobalInitF32Test() {
  std::vector<uint8_t> module_bytes = BuildGlobalInitF32Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunGlobalInitF64Test() {
  std::vector<uint8_t> module_bytes = BuildGlobalInitF64Module();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBadGlobalInitConstLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalInitConstModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadStringConstNoNullLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringConstNoNullModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadI128BlobLenLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadI128BlobLenModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldOffsetLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldAlignmentLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldAlignmentLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeConstLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeConstLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadGlobalInitTypeRuntimeTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalInitTypeRuntimeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_global_init_type load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "bad_global_init_type verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Trapped) {
    std::cerr << "bad_global_init_type expected trap, got status="
              << static_cast<int>(exec.status) << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunGoodStringConstLoadTest() {
  std::vector<uint8_t> module_bytes = BuildGoodStringConstLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunGoodI128BlobLenLoadTest() {
  std::vector<uint8_t> module_bytes = BuildGoodI128BlobLenLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadParamLocalsVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadParamLocalsModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadSigCallConvLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigCallConvLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypesMissingLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypesMissingLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypeStartLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypeStartLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypeMisalignedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypeMisalignedLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypeIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypeIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigRetTypeIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigRetTypeIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigTableTruncatedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigTableTruncatedLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionAlignmentLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionAlignmentLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionOverlapLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionOverlapLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadUnknownSectionIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadUnknownSectionIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadDuplicateSectionIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadDuplicateSectionIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionTableOobLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionTableOobLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadEndianHeaderLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadEndianHeaderLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadHeaderFlagsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderFlagsLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadHeaderMagicLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderMagicLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadHeaderVersionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderVersionLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunPastHeaderVersionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildPastHeaderVersionLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunGoodHeaderVersionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildGoodHeaderVersionLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "expected load success: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadHeaderReservedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderReservedLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadDebugHeaderLoadTest() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> debug = BuildDebugSection(1, 0, 0, 1, 0, 0, 0, 1, 1);
  std::vector<uint8_t> module = BuildModuleWithDebugSection(code, debug);
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module);
  if (load.ok) {
    std::cerr << "expected debug header load failure\n";
    return false;
  }
  return true;
}

bool RunBadDebugLineOobLoadTest() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> debug = BuildDebugSection(1, 1, 0, 0, 0, 99, 0, 1, 1);
  std::vector<uint8_t> module = BuildModuleWithDebugSection(code, debug);
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module);
  if (load.ok) {
    std::cerr << "expected debug line load failure\n";
    return false;
  }
  return true;
}

bool RunGoodDebugLoadTest() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> debug = BuildDebugSection(1, 1, 0, 0, 0, 0, 0, 1, 1);
  std::vector<uint8_t> module = BuildModuleWithDebugSection(code, debug);
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module);
  if (!load.ok) {
    std::cerr << "debug load failed: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadSectionCountZeroLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionCountZeroLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionTableMisalignedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionTableMisalignedLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionTableOffsetOobLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionTableOffsetOobLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypesTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypesTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeKindLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeKindLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadImportsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadImportsTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadImportsMissingConstPoolLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadImportsMissingConstPoolLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadExportsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadExportsTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadExportsMissingConstPoolLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadExportsMissingConstPoolLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadImportNameOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadImportNameOffsetLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadImportSigIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadImportSigIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadImportFlagsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadImportFlagsLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadExportNameOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadExportNameOffsetLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadExportFuncIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadExportFuncIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadExportFlagsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadExportFlagsLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadExportReservedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadExportReservedLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadImportDuplicateLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadImportDuplicateLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadExportDuplicateLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadExportDuplicateLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunImportCallTest() {
  std::vector<uint8_t> module_bytes = BuildImportCallModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportCallHostResolverTest() {
  std::vector<uint8_t> module_bytes = BuildImportCallHostModule();
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
  Simple::VM::ExecOptions options;
  options.import_resolver =
      [](const std::string& mod, const std::string& sym, const std::vector<uint64_t>& args,
         uint64_t& out_ret, bool& out_has_ret, std::string& out_error) -> bool {
        if (mod != "host" || sym != "add1") {
          return false;
        }
        if (args.size() != 1) {
          out_error = "host.add1 arg count mismatch";
          return false;
        }
        int32_t value = static_cast<int32_t>(static_cast<uint32_t>(args[0]));
        out_ret = static_cast<uint32_t>(value + 1);
        out_has_ret = true;
        return true;
      };
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, true, options);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 42) {
    std::cerr << "expected 42, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportCallIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildImportCallIndirectModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportDlOpenNullTest() {
  std::vector<uint8_t> module_bytes = BuildImportDlOpenNullModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportTimeMonoTest() {
  std::vector<uint8_t> module_bytes = BuildImportTimeMonoModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportCwdGetTest() {
  std::vector<uint8_t> module_bytes = BuildImportCwdGetModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildImportTailCallModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportArgsCountTest() {
  std::vector<uint8_t> module_bytes = BuildImportArgsCountModule();
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
  Simple::VM::ExecOptions options;
  options.argv = {"one", "two", "three"};
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, true, options);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportArgsGetCharEqTest() {
  std::vector<uint8_t> module_bytes = BuildImportArgsGetCharEqModule();
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
  Simple::VM::ExecOptions options;
  options.argv = {"one"};
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, true, options);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportEnvGetCharEqTest() {
  std::vector<uint8_t> module_bytes = BuildImportEnvGetCharEqModule();
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
  SetEnvVar("SIMPLEVM_ENV_TEST", "abc");
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  UnsetEnvVar("SIMPLEVM_ENV_TEST");
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportEnvGetMissingTest() {
  std::vector<uint8_t> module_bytes = BuildImportEnvGetMissingModule();
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
  UnsetEnvVar("SIMPLEVM_ENV_MISSING");
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportArgsGetOobTest() {
  std::vector<uint8_t> module_bytes = BuildImportArgsGetIsNullModule(10);
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
  Simple::VM::ExecOptions options;
  options.argv = {"one"};
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, true, options);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportArgsGetNegTest() {
  std::vector<uint8_t> module_bytes = BuildImportArgsGetIsNullModule(-1);
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
  Simple::VM::ExecOptions options;
  options.argv = {"one"};
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, true, options);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsOpenStubTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsOpenModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != -1) {
    std::cerr << "expected -1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadClampTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadClampModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadBadFdTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadBadFdModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != -1) {
    std::cerr << "expected -1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteNullBufTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteNullBufModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadNonArrayBufTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadNonArrayBufModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteBadFdTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteBadFdModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != -1) {
    std::cerr << "expected -1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsCloseBadFdTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsCloseBadFdModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteClampTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteClampModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsCloseTwiceTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsCloseTwiceModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsOpenNullPathTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsOpenNullPathModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != -1) {
    std::cerr << "expected -1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadZeroLenTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadZeroLenModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadAfterCloseTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadAfterCloseModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteAfterCloseTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteAfterCloseModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsOpenCloseReopenTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsOpenCloseReopenModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteZeroLenTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteZeroLenModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadZeroBufTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadZeroBufModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteZeroBufTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteZeroBufModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadClampNoOverwriteTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadClampNoOverwriteModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteAfterReadOnlyOpenTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteAfterReadOnlyOpenModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsOpenCloseLoopTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsOpenCloseLoopModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteClampCountTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteClampCountModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsOpenCloseStressTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsOpenCloseStressModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadZeroLenPreserveTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadZeroLenPreserveModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteReadPersistTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteReadPersistModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadWriteCycleTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadWriteReopenCycleModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadZeroLenNonEmptyBufTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadZeroLenNonEmptyBufModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportCoreLogTest() {
  std::vector<uint8_t> module_bytes = BuildImportCoreLogModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsReadStubTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsReadModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != -1) {
    std::cerr << "expected -1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsWriteStubTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsWriteModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != -1) {
    std::cerr << "expected -1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsCloseStubTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsCloseModule();
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
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunImportFsRoundTripTest() {
  std::vector<uint8_t> module_bytes = BuildImportFsRoundTripModule();
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
  std::remove("Tests/bin/sbc_fs_roundtrip.bin");
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed status " << static_cast<int>(exec.status);
    if (!exec.error.empty()) {
      std::cerr << ": " << exec.error;
    }
    std::cerr << "\n";
    return false;
  }
  std::remove("Tests/bin/sbc_fs_roundtrip.bin");
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBadImportCallParamVerifyTest() {
  return RunExpectVerifyFail(BuildBadImportCallParamVerifyModule(), "bad_import_call_param_verify");
}

bool RunBadTypeKindSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeKindSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeKindRefSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeKindRefSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeKindFieldsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeKindFieldsLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeKindRefFieldsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeKindRefFieldsLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunGoodTypeKindRefSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildGoodTypeKindRefSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "expected load success\n";
    return false;
  }
  return true;
}

bool RunBadFieldsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldsTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodsTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigsTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadGlobalsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalsTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionsTableSizeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeFieldRangeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeFieldRangeLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldTypeIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldTypeIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadGlobalTypeIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalTypeIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionMethodIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionMethodIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodSigIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodSigIdLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunMissingCodeSectionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildMissingCodeSectionLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunMissingFunctionsSectionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildMissingFunctionsSectionLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadConstStringOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstStringOffsetLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadConstI128OffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstI128OffsetLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadConstF64TruncatedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstF64TruncatedLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodFlagsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodFlagsLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunJumpToEndTest() {
  std::vector<uint8_t> module_bytes = BuildJumpToEndModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBadStackMaxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStackMaxModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStackMaxZeroLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadStackMaxZeroLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadEntryMethodLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadEntryMethodLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionOffsetLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodOffsetLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionOverlapLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionOverlapLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunCallCheckTest() {
  std::vector<uint8_t> module_bytes = BuildCallCheckModule();
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
    std::cerr << "exec failed: status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunCallParamTypeTest() {
  std::vector<uint8_t> module_bytes = BuildCallParamTypeModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunCallIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildCallIndirectModule();
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
  if (exec.exit_code != 9) {
    std::cerr << "expected 9, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBadCallIndirectVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallIndirectVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadCallVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadCallParamTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallParamTypeVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadCallIndirectParamTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallIndirectParamTypeVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadTailCallParamTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTailCallParamTypeVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadCallParamI8ToI32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallParamI8ToI32VerifyModule();
  return RunExpectVerifyFail(module_bytes, "bad_call_param_i8_to_i32_verify");
}

bool RunBadCallIndirectParamI8ToI32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallIndirectParamI8ToI32VerifyModule();
  return RunExpectVerifyFail(module_bytes, "bad_call_indirect_param_i8_to_i32_verify");
}

bool RunBadTailCallParamI8ToI32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTailCallParamI8ToI32VerifyModule();
  return RunExpectVerifyFail(module_bytes, "bad_tailcall_param_i8_to_i32_verify");
}

bool RunCmpMixedSmallTypesTest() {
  return RunExpectExit(BuildCmpMixedSmallTypesModule(), 1);
}

bool RunArraySetI32WithCharTest() {
  return RunExpectExit(BuildArraySetI32WithCharModule(), 1);
}

bool RunBadArraySetI32BoolValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetI32BoolValueVerifyModule();
  return RunExpectVerifyFail(module_bytes, "bad_array_set_i32_bool_value_verify");
}

bool RunBadTailCallVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTailCallVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadReturnVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadReturnVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadConvVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadConvVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunCallIndirectParamTypeTest() {
  std::vector<uint8_t> module_bytes = BuildCallIndirectParamTypeModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildTailCallModule();
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
  if (exec.exit_code != 42) {
    std::cerr << "expected 42, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIntrinsicTrapTest() {
  return RunExpectTrap(BuildIntrinsicTrapModule(), "intrinsic");
}

bool RunSysCallTrapTest() {
  return RunExpectTrapNoVerify(BuildSysCallTrapModule(), "syscall");
}

bool RunBadIntrinsicIdVerifyTest() {
  return RunExpectVerifyFail(BuildBadIntrinsicIdVerifyModule(), "bad_intrinsic_id");
}

bool RunBadIntrinsicParamVerifyTest() {
  return RunExpectVerifyFail(BuildBadIntrinsicParamVerifyModule(), "bad_intrinsic_param");
}

bool RunIntrinsicReturnVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildIntrinsicReturnVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "intrinsic_return load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "intrinsic_return verify failed: " << vr.error << "\n";
    return false;
  }
  return true;
}

bool RunIntrinsicCoreTest() {
  std::vector<uint8_t> module_bytes = BuildIntrinsicCoreModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "intrinsic_core load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "intrinsic_core expected halt, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 18) {
    std::cerr << "intrinsic_core expected 18, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunIntrinsicTimeTest() {
  std::vector<uint8_t> module_bytes = BuildIntrinsicTimeModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "intrinsic_time load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "intrinsic_time expected halt, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.exit_code != 1) {
    std::cerr << "intrinsic_time expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBadSysCallVerifyTest() {
  return RunExpectVerifyFail(BuildBadSysCallVerifyModule(), "bad_syscall_verify");
}

bool RunBadArrayGetTrapTest() {
  return RunExpectTrap(BuildBadArrayGetModule(), "bad_array_get");
}

bool RunBadArrayLenNullTrapTest() {
  return RunExpectTrap(BuildBadArrayLenNullModule(), "bad_array_len_null");
}

bool RunBadArrayGetNullTrapTest() {
  return RunExpectTrap(BuildBadArrayGetNullModule(), "bad_array_get_null");
}

bool RunBadArraySetNullTrapTest() {
  return RunExpectTrap(BuildBadArraySetNullModule(), "bad_array_set_null");
}

bool RunBadArraySetTrapTest() {
  return RunExpectTrap(BuildBadArraySetModule(), "bad_array_set");
}

bool RunBadArrayGetNegIndexTrapTest() {
  return RunExpectTrap(BuildBadArrayGetNegIndexModule(), "bad_array_get_neg_index");
}

bool RunBadArraySetNegIndexTrapTest() {
  return RunExpectTrap(BuildBadArraySetNegIndexModule(), "bad_array_set_neg_index");
}

bool RunBadArrayGetI64TrapTest() {
  return RunExpectTrap(BuildBadArrayGetI64Module(), "bad_array_get_i64");
}

bool RunBadArrayGetI64NullTrapTest() {
  return RunExpectTrap(BuildBadArrayGetI64NullModule(), "bad_array_get_i64_null");
}

bool RunBadArrayGetI64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadArrayGetI64NegIndexModule(), "bad_array_get_i64_neg_index");
}

bool RunBadArrayGetF32TrapTest() {
  return RunExpectTrap(BuildBadArrayGetF32Module(), "bad_array_get_f32");
}

bool RunBadArrayGetF32NullTrapTest() {
  return RunExpectTrap(BuildBadArrayGetF32NullModule(), "bad_array_get_f32_null");
}

bool RunBadArrayGetF32NegIndexTrapTest() {
  return RunExpectTrap(BuildBadArrayGetF32NegIndexModule(), "bad_array_get_f32_neg_index");
}

bool RunBadArrayGetF64TrapTest() {
  return RunExpectTrap(BuildBadArrayGetF64Module(), "bad_array_get_f64");
}

bool RunBadArrayGetF64NullTrapTest() {
  return RunExpectTrap(BuildBadArrayGetF64NullModule(), "bad_array_get_f64_null");
}

bool RunBadArrayGetF64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadArrayGetF64NegIndexModule(), "bad_array_get_f64_neg_index");
}

bool RunBadArrayGetRefTrapTest() {
  return RunExpectTrap(BuildBadArrayGetRefModule(), "bad_array_get_ref");
}

bool RunBadArrayGetRefNullTrapTest() {
  return RunExpectTrap(BuildBadArrayGetRefNullModule(), "bad_array_get_ref_null");
}

bool RunBadArrayGetRefNegIndexTrapTest() {
  return RunExpectTrap(BuildBadArrayGetRefNegIndexModule(), "bad_array_get_ref_neg_index");
}

bool RunBadArraySetI64TrapTest() {
  return RunExpectTrap(BuildBadArraySetI64Module(), "bad_array_set_i64");
}

bool RunBadArraySetI64NullTrapTest() {
  return RunExpectTrap(BuildBadArraySetI64NullModule(), "bad_array_set_i64_null");
}

bool RunBadArraySetI64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadArraySetI64NegIndexModule(), "bad_array_set_i64_neg_index");
}

bool RunBadArraySetF32TrapTest() {
  return RunExpectTrap(BuildBadArraySetF32Module(), "bad_array_set_f32");
}

bool RunBadArraySetF32NullTrapTest() {
  return RunExpectTrap(BuildBadArraySetF32NullModule(), "bad_array_set_f32_null");
}

bool RunBadArraySetF32NegIndexTrapTest() {
  return RunExpectTrap(BuildBadArraySetF32NegIndexModule(), "bad_array_set_f32_neg_index");
}

bool RunBadArraySetF64TrapTest() {
  return RunExpectTrap(BuildBadArraySetF64Module(), "bad_array_set_f64");
}

bool RunBadArraySetF64NullTrapTest() {
  return RunExpectTrap(BuildBadArraySetF64NullModule(), "bad_array_set_f64_null");
}

bool RunBadArraySetF64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadArraySetF64NegIndexModule(), "bad_array_set_f64_neg_index");
}

bool RunBadArraySetRefTrapTest() {
  return RunExpectTrap(BuildBadArraySetRefModule(), "bad_array_set_ref");
}

bool RunBadArraySetRefNullTrapTest() {
  return RunExpectTrap(BuildBadArraySetRefNullModule(), "bad_array_set_ref_null");
}

bool RunBadArraySetRefNegIndexTrapTest() {
  return RunExpectTrap(BuildBadArraySetRefNegIndexModule(), "bad_array_set_ref_neg_index");
}

bool RunBadListGetTrapTest() {
  return RunExpectTrap(BuildBadListGetModule(), "bad_list_get");
}

bool RunBadListLenNullTrapTest() {
  return RunExpectTrap(BuildBadListLenNullModule(), "bad_list_len_null");
}

bool RunBadListGetNullTrapTest() {
  return RunExpectTrap(BuildBadListGetNullModule(), "bad_list_get_null");
}

bool RunBadListGetNegIndexTrapTest() {
  return RunExpectTrap(BuildBadListGetNegIndexModule(), "bad_list_get_neg_index");
}

bool RunBadListGetI64TrapTest() {
  return RunExpectTrap(BuildBadListGetI64Module(), "bad_list_get_i64");
}

bool RunBadListGetI64NullTrapTest() {
  return RunExpectTrap(BuildBadListGetI64NullModule(), "bad_list_get_i64_null");
}

bool RunBadListGetI64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListGetI64NegIndexModule(), "bad_list_get_i64_neg_index");
}

bool RunBadListGetF32TrapTest() {
  return RunExpectTrap(BuildBadListGetF32Module(), "bad_list_get_f32");
}

bool RunBadListGetF32NullTrapTest() {
  return RunExpectTrap(BuildBadListGetF32NullModule(), "bad_list_get_f32_null");
}

bool RunBadListGetF32NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListGetF32NegIndexModule(), "bad_list_get_f32_neg_index");
}

bool RunBadListGetF64TrapTest() {
  return RunExpectTrap(BuildBadListGetF64Module(), "bad_list_get_f64");
}

bool RunBadListGetF64NullTrapTest() {
  return RunExpectTrap(BuildBadListGetF64NullModule(), "bad_list_get_f64_null");
}

bool RunBadListGetF64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListGetF64NegIndexModule(), "bad_list_get_f64_neg_index");
}

bool RunBadListGetRefTrapTest() {
  return RunExpectTrap(BuildBadListGetRefModule(), "bad_list_get_ref");
}

bool RunBadListGetRefNullTrapTest() {
  return RunExpectTrap(BuildBadListGetRefNullModule(), "bad_list_get_ref_null");
}

bool RunBadListGetRefNegIndexTrapTest() {
  return RunExpectTrap(BuildBadListGetRefNegIndexModule(), "bad_list_get_ref_neg_index");
}

bool RunBadListSetTrapTest() {
  return RunExpectTrap(BuildBadListSetModule(), "bad_list_set");
}

bool RunBadListSetNullTrapTest() {
  return RunExpectTrap(BuildBadListSetNullModule(), "bad_list_set_null");
}

bool RunBadListSetNegIndexTrapTest() {
  return RunExpectTrap(BuildBadListSetNegIndexModule(), "bad_list_set_neg_index");
}

bool RunBadListSetI64TrapTest() {
  return RunExpectTrap(BuildBadListSetI64Module(), "bad_list_set_i64");
}

bool RunBadListSetI64NullTrapTest() {
  return RunExpectTrap(BuildBadListSetI64NullModule(), "bad_list_set_i64_null");
}

bool RunBadListSetI64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListSetI64NegIndexModule(), "bad_list_set_i64_neg_index");
}

bool RunBadListSetF32TrapTest() {
  return RunExpectTrap(BuildBadListSetF32Module(), "bad_list_set_f32");
}

bool RunBadListSetF32NullTrapTest() {
  return RunExpectTrap(BuildBadListSetF32NullModule(), "bad_list_set_f32_null");
}

bool RunBadListSetF32NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListSetF32NegIndexModule(), "bad_list_set_f32_neg_index");
}

bool RunBadListSetF64TrapTest() {
  return RunExpectTrap(BuildBadListSetF64Module(), "bad_list_set_f64");
}

bool RunBadListSetF64NullTrapTest() {
  return RunExpectTrap(BuildBadListSetF64NullModule(), "bad_list_set_f64_null");
}

bool RunBadListSetF64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListSetF64NegIndexModule(), "bad_list_set_f64_neg_index");
}

bool RunBadListSetRefTrapTest() {
  return RunExpectTrap(BuildBadListSetRefModule(), "bad_list_set_ref");
}

bool RunBadListSetRefNullTrapTest() {
  return RunExpectTrap(BuildBadListSetRefNullModule(), "bad_list_set_ref_null");
}

bool RunBadListSetRefNegIndexTrapTest() {
  return RunExpectTrap(BuildBadListSetRefNegIndexModule(), "bad_list_set_ref_neg_index");
}

bool RunBadListPopTrapTest() {
  return RunExpectTrap(BuildBadListPopModule(), "bad_list_pop");
}

bool RunBadListPopI64TrapTest() {
  return RunExpectTrap(BuildBadListPopI64Module(), "bad_list_pop_i64");
}

bool RunBadListPopI64NullTrapTest() {
  return RunExpectTrap(BuildBadListPopI64NullModule(), "bad_list_pop_i64_null");
}

bool RunBadListPopF32TrapTest() {
  return RunExpectTrap(BuildBadListPopF32Module(), "bad_list_pop_f32");
}

bool RunBadListPopF32NullTrapTest() {
  return RunExpectTrap(BuildBadListPopF32NullModule(), "bad_list_pop_f32_null");
}

bool RunBadListPopF64TrapTest() {
  return RunExpectTrap(BuildBadListPopF64Module(), "bad_list_pop_f64");
}

bool RunBadListPopF64NullTrapTest() {
  return RunExpectTrap(BuildBadListPopF64NullModule(), "bad_list_pop_f64_null");
}

bool RunBadListPopRefTrapTest() {
  return RunExpectTrap(BuildBadListPopRefModule(), "bad_list_pop_ref");
}

bool RunBadListPopRefNullTrapTest() {
  return RunExpectTrap(BuildBadListPopRefNullModule(), "bad_list_pop_ref_null");
}

bool RunBadListPushNullTrapTest() {
  return RunExpectTrap(BuildBadListPushNullModule(), "bad_list_push_null");
}

bool RunBadListPopNullTrapTest() {
  return RunExpectTrap(BuildBadListPopNullModule(), "bad_list_pop_null");
}

bool RunBadListInsertTrapTest() {
  return RunExpectTrap(BuildBadListInsertModule(), "bad_list_insert");
}

bool RunBadListInsertI64TrapTest() {
  return RunExpectTrap(BuildBadListInsertI64Module(), "bad_list_insert_i64");
}

bool RunBadListInsertI64NullTrapTest() {
  return RunExpectTrap(BuildBadListInsertI64NullModule(), "bad_list_insert_i64_null");
}

bool RunBadListInsertI64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListInsertI64NegIndexModule(), "bad_list_insert_i64_neg_index");
}

bool RunBadListInsertF32TrapTest() {
  return RunExpectTrap(BuildBadListInsertF32Module(), "bad_list_insert_f32");
}

bool RunBadListInsertF32NullTrapTest() {
  return RunExpectTrap(BuildBadListInsertF32NullModule(), "bad_list_insert_f32_null");
}

bool RunBadListInsertF32NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListInsertF32NegIndexModule(), "bad_list_insert_f32_neg_index");
}

bool RunBadListInsertF64TrapTest() {
  return RunExpectTrap(BuildBadListInsertF64Module(), "bad_list_insert_f64");
}

bool RunBadListInsertF64NullTrapTest() {
  return RunExpectTrap(BuildBadListInsertF64NullModule(), "bad_list_insert_f64_null");
}

bool RunBadListInsertF64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListInsertF64NegIndexModule(), "bad_list_insert_f64_neg_index");
}

bool RunBadListInsertRefTrapTest() {
  return RunExpectTrap(BuildBadListInsertRefModule(), "bad_list_insert_ref");
}

bool RunBadListInsertRefNullTrapTest() {
  return RunExpectTrap(BuildBadListInsertRefNullModule(), "bad_list_insert_ref_null");
}

bool RunBadListInsertRefNegIndexTrapTest() {
  return RunExpectTrap(BuildBadListInsertRefNegIndexModule(), "bad_list_insert_ref_neg_index");
}

bool RunBadListInsertNullTrapTest() {
  return RunExpectTrap(BuildBadListInsertNullModule(), "bad_list_insert_null");
}

bool RunBadListRemoveTrapTest() {
  return RunExpectTrap(BuildBadListRemoveModule(), "bad_list_remove");
}

bool RunBadListRemoveI64TrapTest() {
  return RunExpectTrap(BuildBadListRemoveI64Module(), "bad_list_remove_i64");
}

bool RunBadListRemoveI64NullTrapTest() {
  return RunExpectTrap(BuildBadListRemoveI64NullModule(), "bad_list_remove_i64_null");
}

bool RunBadListRemoveI64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListRemoveI64NegIndexModule(), "bad_list_remove_i64_neg_index");
}

bool RunBadListRemoveF32TrapTest() {
  return RunExpectTrap(BuildBadListRemoveF32Module(), "bad_list_remove_f32");
}

bool RunBadListRemoveF32NullTrapTest() {
  return RunExpectTrap(BuildBadListRemoveF32NullModule(), "bad_list_remove_f32_null");
}

bool RunBadListRemoveF32NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListRemoveF32NegIndexModule(), "bad_list_remove_f32_neg_index");
}

bool RunBadListRemoveF64TrapTest() {
  return RunExpectTrap(BuildBadListRemoveF64Module(), "bad_list_remove_f64");
}

bool RunBadListRemoveF64NullTrapTest() {
  return RunExpectTrap(BuildBadListRemoveF64NullModule(), "bad_list_remove_f64_null");
}

bool RunBadListRemoveF64NegIndexTrapTest() {
  return RunExpectTrap(BuildBadListRemoveF64NegIndexModule(), "bad_list_remove_f64_neg_index");
}

bool RunBadListRemoveRefTrapTest() {
  return RunExpectTrap(BuildBadListRemoveRefModule(), "bad_list_remove_ref");
}

bool RunBadListRemoveRefNullTrapTest() {
  return RunExpectTrap(BuildBadListRemoveRefNullModule(), "bad_list_remove_ref_null");
}

bool RunBadListRemoveRefNegIndexTrapTest() {
  return RunExpectTrap(BuildBadListRemoveRefNegIndexModule(), "bad_list_remove_ref_neg_index");
}

bool RunBadListRemoveNullTrapTest() {
  return RunExpectTrap(BuildBadListRemoveNullModule(), "bad_list_remove_null");
}

bool RunBadListClearNullTrapTest() {
  return RunExpectTrap(BuildBadListClearNullModule(), "bad_list_clear_null");
}

bool RunBadStringGetCharNegIndexTrapTest() {
  return RunExpectTrap(BuildBadStringGetCharNegIndexModule(), "bad_string_get_char_neg_index");
}

bool RunBadStringSliceNegIndexTrapTest() {
  return RunExpectTrap(BuildBadStringSliceNegIndexModule(), "bad_string_slice_neg_index");
}

bool RunBadConvRuntimeTrapTest() {
  return RunExpectVerifyFail(BuildBadConvRuntimeModule(), "bad_conv_runtime");
}

bool RunBadConstI128KindTrapTest() {
  return RunExpectTrap(BuildBadConstI128KindModule(), "bad_const_i128_kind");
}

bool RunBadConstU128BlobTrapTest() {
  return RunExpectTrap(BuildBadConstU128BlobModule(), "bad_const_u128_blob");
}

bool RunBadBitwiseVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBitwiseVerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadU32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadU32VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadNegI32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadNegI32VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadNegF32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadNegF32VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadIncI32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadIncI32VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadIncF32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadIncF32VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadIncU32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadIncU32VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadIncI8VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadIncI8VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadNegI8VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadNegI8VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadNegU32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadNegU32VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpTableKindLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpTableKindModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  if (load.error.find("JMP_TABLE const kind mismatch") == std::string::npos) {
    std::cerr << "expected JMP_TABLE kind error, got: " << load.error << "\n";
    return false;
  }
  return true;
}
bool RunBadU64VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadU64VerifyModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpTableBlobLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpTableBlobLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  if (load.error.find("JMP_TABLE blob") == std::string::npos) {
    std::cerr << "expected JMP_TABLE blob error, got: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadJmpTableVerifyOobTargetTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpTableVerifyOobTargetModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpTableVerifyDefaultOobTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpTableVerifyDefaultOobModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}
bool RunBadJmpTableOobTargetTrapTest() {
  return RunExpectTrapNoVerify(BuildBadJmpTableOobTargetModule(), "bad_jmp_table_oob_runtime");
}
bool RunBadBitwiseRuntimeTrapTest() {
  return RunExpectVerifyFail(BuildBadBitwiseRuntimeModule(), "bad_bitwise_runtime");
}

bool RunBadU32RuntimeTrapTest() {
  return RunExpectVerifyFail(BuildBadU32RuntimeModule(), "bad_u32_runtime");
}

bool RunBadU64RuntimeTrapTest() {
  return RunExpectVerifyFail(BuildBadU64RuntimeModule(), "bad_u64_runtime");
}

bool RunBadUpvalueIndexTrapTest() {
  return RunExpectTrap(BuildBadUpvalueIndexModule(), "bad_upvalue_index");
}

bool RunBadCallIndirectTrapTest() {
  return RunExpectTrap(BuildBadCallIndirectFuncModule(), "bad_call_indirect");
}

bool RunBadCallIndirectTypeTrapTest() {
  return RunExpectVerifyFail(BuildBadCallIndirectTypeModule(), "bad_call_indirect_type");
}

bool RunLineTrapDiagTest() {
  std::vector<uint8_t> module_bytes = BuildLineTrapModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "line_trap load failed: " << load.error << "\n";
    return false;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "line_trap verify failed: " << vr.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Trapped) {
    std::cerr << "line_trap expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  if (exec.error.find("line 10:20") == std::string::npos) {
    std::cerr << "line_trap missing line info: " << exec.error << "\n";
    return false;
  }
  if (exec.error.find("pc ") == std::string::npos) {
    std::cerr << "line_trap missing pc info: " << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunTrapOperandDiagTest() {
  auto run_no_verify = [&](const std::vector<uint8_t>& bytes,
                           const char* label,
                           const char* needle1,
                           const char* needle2) -> bool {
    Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(bytes);
    if (!load.ok) {
      std::cerr << label << " load failed: " << load.error << "\n";
      return false;
    }
    Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, false);
    if (exec.status != Simple::VM::ExecStatus::Trapped) {
      std::cerr << label << " expected trap, got status=" << static_cast<int>(exec.status)
                << " error=" << exec.error << "\n";
      return false;
    }
    if (exec.error.find(needle1) == std::string::npos) {
      std::cerr << label << " missing '" << needle1 << "': " << exec.error << "\n";
      return false;
    }
    if (exec.error.find(needle2) == std::string::npos) {
      std::cerr << label << " missing '" << needle2 << "': " << exec.error << "\n";
      return false;
    }
    return true;
  };

  if (!run_no_verify(BuildBadJmpRuntimeModule(),
                     "diag_trap_jmp",
                     "last_op 0x04 Jmp",
                     "operands rel=")) {
    return false;
  }
  if (!run_no_verify(BuildBadJmpTableOobTargetModule(),
                     "diag_trap_jmp_table",
                     "last_op 0x07 JmpTable",
                     "table_const=")) {
    return false;
  }
  if (!run_no_verify(BuildBadCallRuntimeModule(),
                     "diag_trap_call",
                     "last_op 0x70 Call",
                     "func_id=")) {
    return false;
  }
  return true;
}

bool RunBadStringLenNullTrapTest() {
  return RunExpectTrap(BuildBadStringLenNullModule(), "bad_string_len_null");
}

bool RunBadStringConcatNullTrapTest() {
  return RunExpectTrap(BuildBadStringConcatNullModule(), "bad_string_concat_null");
}

bool RunBadStringGetCharNullTrapTest() {
  return RunExpectTrap(BuildBadStringGetCharNullModule(), "bad_string_get_char_null");
}

bool RunBadStringGetCharTrapTest() {
  return RunExpectTrap(BuildBadStringGetCharModule(), "bad_string_get_char");
}

bool RunBadStringSliceNullTrapTest() {
  return RunExpectTrap(BuildBadStringSliceNullModule(), "bad_string_slice_null");
}

bool RunBadStringSliceTrapTest() {
  return RunExpectTrap(BuildBadStringSliceModule(), "bad_string_slice");
}

bool RunListGrowthTest() {
  return RunExpectExit(BuildListGrowthModule(), 2);
}

bool RunHeapReuseTest() {
  Simple::VM::Heap heap;
  uint32_t first = heap.Allocate(Simple::VM::ObjectKind::String, 0, 8);
  uint32_t second = heap.Allocate(Simple::VM::ObjectKind::Array, 0, 16);
  heap.ResetMarks();
  heap.Mark(second);
  heap.Sweep();
  if (heap.Get(first) != nullptr) {
    std::cerr << "expected freed handle to be invalid\n";
    return false;
  }
  uint32_t reused = heap.Allocate(Simple::VM::ObjectKind::List, 0, 12);
  if (reused != first) {
    std::cerr << "expected reuse of freed handle\n";
    return false;
  }
  if (!heap.Get(reused)) {
    std::cerr << "expected reused handle to be valid\n";
    return false;
  }
  return true;
}

bool RunScratchArenaTest() {
  Simple::VM::ScratchArena arena(16);
  if (arena.Used() != 0) {
    std::cerr << "scratch arena should start empty\n";
    return false;
  }
  std::size_t mark0 = arena.Mark();
  uint8_t* a = arena.Allocate(4, 4);
  if (!a) {
    std::cerr << "scratch arena alloc failed\n";
    return false;
  }
  std::size_t used1 = arena.Used();
  if (used1 < 4) {
    std::cerr << "scratch arena used size too small\n";
    return false;
  }
  std::size_t mark1 = arena.Mark();
  uint8_t* b = arena.Allocate(8, 8);
  if (!b) {
    std::cerr << "scratch arena alloc 2 failed\n";
    return false;
  }
  if (arena.Used() <= used1) {
    std::cerr << "scratch arena used size did not grow\n";
    return false;
  }
  arena.Reset(mark1);
  if (arena.Used() != mark1) {
    std::cerr << "scratch arena reset failed\n";
    return false;
  }
  uint8_t* c = arena.Allocate(8, 8);
  if (!c) {
    std::cerr << "scratch arena alloc after reset failed\n";
    return false;
  }
  arena.Reset(mark0);
  if (arena.Used() != mark0) {
    std::cerr << "scratch arena reset to start failed\n";
    return false;
  }
  return true;
}

bool RunScratchScopeTest() {
  Simple::VM::ScratchArena arena(32);
  std::size_t before = arena.Used();
  {
    Simple::VM::ScratchScope scope(arena);
    uint8_t* a = arena.Allocate(12, 4);
    if (!a) {
      std::cerr << "scratch scope alloc failed\n";
      return false;
    }
    if (arena.Used() <= before) {
      std::cerr << "scratch scope did not advance\n";
      return false;
    }
  }
  if (arena.Used() != before) {
    std::cerr << "scratch scope did not reset\n";
    return false;
  }
  return true;
}

bool RunScratchArenaAlignmentTest() {
  Simple::VM::ScratchArena arena(8);
  uint8_t* a = arena.Allocate(1, 16);
  if (!a) {
    std::cerr << "scratch arena align alloc failed\n";
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(a) & 15u) != 0u) {
    std::cerr << "scratch arena alignment failed\n";
    return false;
  }
  uint8_t* b = arena.Allocate(7, 8);
  if (!b) {
    std::cerr << "scratch arena second alloc failed\n";
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(b) & 7u) != 0u) {
    std::cerr << "scratch arena second alignment failed\n";
    return false;
  }
  return true;
}

bool RunScratchScopeEnforcedTest() {
  Simple::VM::ScratchArena arena(16);
  arena.SetRequireScope(true);
  uint8_t* fail = arena.Allocate(4, 4);
  if (fail != nullptr) {
    std::cerr << "scratch arena should reject alloc without scope\n";
    return false;
  }
  {
    Simple::VM::ScratchScope scope(arena);
    uint8_t* ok = arena.Allocate(4, 4);
    if (!ok) {
      std::cerr << "scratch arena scoped alloc failed\n";
      return false;
    }
  }
  return true;
}

bool RunScratchArenaPoisonTest() {
  Simple::VM::ScratchArena arena(8);
  arena.SetDebugPoison(true);
  uint8_t* ptr = nullptr;
  {
    Simple::VM::ScratchScope scope(arena);
    ptr = arena.Allocate(4, 1);
    if (!ptr) {
      std::cerr << "scratch arena poison alloc failed\n";
      return false;
    }
    std::memset(ptr, 0xAB, 4);
  }
  for (int i = 0; i < 4; ++i) {
    if (ptr[i] != 0xCD) {
      std::cerr << "scratch arena poison did not overwrite buffer\n";
      return false;
    }
  }
  return true;
}

bool RunHeapClosureMarkTest() {
  Simple::VM::Heap heap;
  uint32_t target = heap.Allocate(Simple::VM::ObjectKind::String, 0, 8);
  uint32_t closure = heap.Allocate(Simple::VM::ObjectKind::Closure, 0, 12);
  uint32_t dead = heap.Allocate(Simple::VM::ObjectKind::List, 0, 8);
  if (!heap.Get(closure) || !heap.Get(target) || !heap.Get(dead)) {
    std::cerr << "heap allocation failed\n";
    return false;
  }
  Simple::VM::HeapObject* obj = heap.Get(closure);
  WriteU32Payload(obj->payload, 0, 0);
  WriteU32Payload(obj->payload, 4, 1);
  WriteU32Payload(obj->payload, 8, target);
  heap.ResetMarks();
  heap.Mark(closure);
  heap.Sweep();
  if (!heap.Get(closure)) {
    std::cerr << "closure should remain alive\n";
    return false;
  }
  if (!heap.Get(target)) {
    std::cerr << "closure upvalue target should remain alive\n";
    return false;
  }
  if (heap.Get(dead)) {
    std::cerr << "unreferenced object should be collected\n";
    return false;
  }
  return true;
}

bool RunGcStressTest() {
  Simple::VM::Heap heap;
  std::vector<uint32_t> handles;
  handles.reserve(1000);
  for (uint32_t i = 0; i < 1000; ++i) {
    Simple::VM::ObjectKind kind = (i % 2 == 0) ? Simple::VM::ObjectKind::String : Simple::VM::ObjectKind::Array;
    uint32_t handle = heap.Allocate(kind, 0, 8);
    handles.push_back(handle);
  }
  heap.ResetMarks();
  for (uint32_t i = 0; i < handles.size(); ++i) {
    if (i % 10 == 0) {
      heap.Mark(handles[i]);
    }
  }
  heap.Sweep();
  for (uint32_t i = 0; i < handles.size(); ++i) {
    bool should_live = (i % 10 == 0);
    if (should_live && !heap.Get(handles[i])) {
      std::cerr << "expected live object to remain\n";
      return false;
    }
    if (!should_live && heap.Get(handles[i])) {
      std::cerr << "expected dead object to be collected\n";
      return false;
    }
  }
  return true;
}

bool RunGcVmStressTest() {
  std::vector<uint8_t> module_bytes = BuildGcVmStressModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunGcTest() {
  std::vector<uint8_t> module_bytes = BuildGcModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}


bool RunModTest() {
  std::vector<uint8_t> module_bytes = BuildModModule();
  return RunExpectExit(module_bytes, 1);
}

bool RunLocalsArenaPreserveTest() {
  return RunExpectExit(BuildLocalsArenaModule(), 7);
}

bool RunLocalsArenaTailCallTest() {
  return RunExpectExit(BuildLocalsArenaTailCallModule(), 7);
}

bool RunLeaveTest() {
  return RunExpectExit(BuildLeaveModule(), 1);
}

bool RunXorI32Test() {
  return RunExpectExit(BuildXorI32Module(), 5);
}

bool RunXorI64Test() {
  return RunExpectExit(BuildXorI64Module(), 6);
}

bool RunU32ArithExtraTest() {
  return RunExpectExit(BuildU32ArithExtraModule(), 4);
}

bool RunU64ArithExtraTest() {
  return RunExpectExit(BuildU64ArithExtraModule(), 7);
}

bool RunF32ArithExtraTest() {
  return RunExpectExit(BuildF32ArithExtraModule(), 7);
}

bool RunF64ArithExtraTest() {
  return RunExpectExit(BuildF64ArithExtraModule(), 7);
}

bool RunCmpI32ExtraTest() {
  return RunExpectExit(BuildCmpI32ExtraModule(), 1);
}

bool RunCmpI64ExtraTest() {
  return RunExpectExit(BuildCmpI64ExtraModule(), 1);
}

bool RunCmpF32ExtraTest() {
  return RunExpectExit(BuildCmpF32ExtraModule(), 1);
}

bool RunCmpF64ExtraTest() {
  return RunExpectExit(BuildCmpF64ExtraModule(), 1);
}

bool RunCmpU32ExtraTest() {
  return RunExpectExit(BuildCmpU32ExtraModule(), 1);
}

bool RunCmpU64ExtraTest() {
  return RunExpectExit(BuildCmpU64ExtraModule(), 1);
}

bool RunListSetI64Test() {
  return RunExpectExit(BuildListSetI64Module(), 7);
}

bool RunListSetF32Test() {
  return RunExpectExit(BuildListSetF32Module(), 7);
}

bool RunListSetF64Test() {
  return RunExpectExit(BuildListSetF64Module(), 7);
}

bool RunListSetRefTest() {
  return RunExpectExit(BuildListSetRefModule(), 1);
}

bool RunBadNamedMethodSigLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadNamedMethodSigLoadModule();
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "bad_named_method_sig expected load fail\n";
    return false;
  }
  if (load.error.find("bad_method") == std::string::npos) {
    std::cerr << "bad_named_method_sig missing method name: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBoolTest() {
  std::vector<uint8_t> module_bytes = BuildBoolModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunCmpTest() {
  std::vector<uint8_t> module_bytes = BuildCmpModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBranchTest() {
  std::vector<uint8_t> module_bytes = BuildBranchModule();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJmpTableCase0Test() {
  std::vector<uint8_t> module_bytes = BuildJmpTableModule(0);
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJmpTableCase1Test() {
  std::vector<uint8_t> module_bytes = BuildJmpTableModule(1);
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
  if (exec.exit_code != 2) {
    std::cerr << "expected 2, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJmpTableDefaultTest() {
  std::vector<uint8_t> module_bytes = BuildJmpTableModule(7);
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJmpTableDefaultEndTest() {
  std::vector<uint8_t> module_bytes = BuildJmpTableDefaultEndModule();
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
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJmpTableDefaultStartTest() {
  std::vector<uint8_t> module_bytes = BuildJmpTableDefaultStartModule();
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
  if (exec.exit_code != 2) {
    std::cerr << "expected 2, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJmpTableEmptyTest() {
  std::vector<uint8_t> module_bytes = BuildJmpTableEmptyModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

static const TestCase kCoreTests[] = {
  {"add_i32", RunAddTest},
  {"globals", RunGlobalTest},
  {"dup", RunDupTest},
  {"dup2", RunDup2Test},
  {"pop", RunPopTest},
  {"swap", RunSwapTest},
  {"rot", RunRotTest},
  {"mod_i32", RunModTest},
  {"locals_arena_preserve", RunLocalsArenaPreserveTest},
  {"locals_arena_tailcall", RunLocalsArenaTailCallTest},
  {"leave", RunLeaveTest},
  {"bool_ops", RunBoolTest},
  {"cmp_i32", RunCmpTest},
  {"cmp_i32_extra", RunCmpI32ExtraTest},
  {"cmp_i64_extra", RunCmpI64ExtraTest},
  {"cmp_f32_extra", RunCmpF32ExtraTest},
  {"cmp_f64_extra", RunCmpF64ExtraTest},
  {"cmp_u32_extra", RunCmpU32ExtraTest},
  {"cmp_u64_extra", RunCmpU64ExtraTest},
  {"branch", RunBranchTest},
  {"jmp_table_case0", RunJmpTableCase0Test},
  {"jmp_table_case1", RunJmpTableCase1Test},
  {"jmp_table_default", RunJmpTableDefaultTest},
  {"jmp_table_default_end", RunJmpTableDefaultEndTest},
  {"jmp_table_default_start", RunJmpTableDefaultStartTest},
  {"jmp_table_empty", RunJmpTableEmptyTest},
  {"locals", RunLocalTest},
  {"loop", RunLoopTest},
  {"fixture_add", RunFixtureAddTest},
  {"fixture_loop", RunFixtureLoopTest},
  {"fixture_fib_iter", RunFixtureFibIterTest},
  {"fixture_fib_rec", RunFixtureFibRecTest},
  {"fixture_uuid_len", RunFixtureUuidLenTest},
  {"recursive_call", RunRecursiveCallTest},
  {"recursive_call_jit", RunRecursiveCallJitTest},
  {"ref_ops", RunRefTest},
  {"upvalue_ops", RunUpvalueTest},
  {"upvalue_object", RunUpvalueObjectTest},
  {"upvalue_order", RunUpvalueOrderTest},
  {"new_closure", RunNewClosureTest},
  {"array_i32", RunArrayTest},
  {"array_i64", RunArrayI64Test},
  {"array_f32", RunArrayF32Test},
  {"array_f64", RunArrayF64Test},
  {"array_ref", RunArrayRefTest},
  {"array_len", RunArrayLenTest},
  {"list_i32", RunListTest},
  {"list_i64", RunListI64Test},
  {"list_f32", RunListF32Test},
  {"list_f64", RunListF64Test},
  {"list_ref", RunListRefTest},
  {"list_len", RunListLenTest},
  {"list_set_i64", RunListSetI64Test},
  {"list_set_f32", RunListSetF32Test},
  {"list_set_f64", RunListSetF64Test},
  {"list_set_ref", RunListSetRefTest},
  {"list_insert", RunListInsertTest},
  {"list_remove", RunListRemoveTest},
  {"list_clear", RunListClearTest},
  {"string_ops", RunStringTest},
  {"string_get_char", RunStringGetCharTest},
  {"string_slice", RunStringSliceTest},
  {"const_u32", RunConstU32Test},
  {"const_char", RunConstCharTest},
  {"const_i64", RunConstI64Test},
  {"const_u64", RunConstU64Test},
  {"const_f32", RunConstF32Test},
  {"const_f64", RunConstF64Test},
  {"const_i128", RunConstI128Test},
  {"const_u128", RunConstU128Test},
  {"i64_arith", RunI64ArithTest},
  {"xor_i32", RunXorI32Test},
  {"xor_i64", RunXorI64Test},
  {"u32_arith_extra", RunU32ArithExtraTest},
  {"u64_arith_extra", RunU64ArithExtraTest},
  {"f32_arith_extra", RunF32ArithExtraTest},
  {"f64_arith_extra", RunF64ArithExtraTest},
  {"neg_i32", RunNegI32Test},
  {"neg_i64", RunNegI64Test},
  {"neg_f32", RunNegF32Test},
  {"neg_f64", RunNegF64Test},
  {"incdec_i32", RunIncDecI32Test},
  {"incdec_i64", RunIncDecI64Test},
  {"incdec_f32", RunIncDecF32Test},
  {"incdec_f64", RunIncDecF64Test},
  {"incdec_u32", RunIncDecU32Test},
  {"incdec_u64", RunIncDecU64Test},
  {"incdec_u32_wrap", RunIncDecU32WrapTest},
  {"incdec_u64_wrap", RunIncDecU64WrapTest},
  {"incdec_i8", RunIncDecI8Test},
  {"incdec_i16", RunIncDecI16Test},
  {"incdec_u8", RunIncDecU8Test},
  {"incdec_u16", RunIncDecU16Test},
  {"incdec_u8_wrap", RunIncDecU8WrapTest},
  {"incdec_u16_wrap", RunIncDecU16WrapTest},
  {"neg_i8", RunNegI8Test},
  {"neg_i16", RunNegI16Test},
  {"neg_u8", RunNegU8Test},
  {"neg_u16", RunNegU16Test},
  {"neg_i8_wrap", RunNegI8WrapTest},
  {"neg_i16_wrap", RunNegI16WrapTest},
  {"neg_u32", RunNegU32Test},
  {"neg_u64", RunNegU64Test},
  {"neg_u32_wrap", RunNegU32WrapTest},
  {"neg_u64_wrap", RunNegU64WrapTest},
  {"neg_u8_wrap", RunNegU8WrapTest},
  {"neg_u16_wrap", RunNegU16WrapTest},
  {"i64_mod", RunI64ModTest},
  {"locals_arena_preserve", RunLocalsArenaPreserveTest},
  {"f32_arith", RunF32ArithTest},
  {"f64_arith", RunF64ArithTest},
  {"conv_int", RunConvIntTest},
  {"conv_float", RunConvFloatTest},
  {"u32_arith", RunU32ArithTest},
  {"u64_cmp", RunU64CmpTest},
  {"u32_cmp_bounds", RunU32CmpBoundsTest},
  {"u64_cmp_bounds", RunU64CmpBoundsTest},
  {"u32_cmp_minmax", RunU32CmpMinMaxTest},
  {"u64_cmp_minmax", RunU64CmpMinMaxTest},
  {"u32_div_zero", RunU32DivZeroTest},
  {"u32_overflow", RunU32OverflowTest},
  {"u64_div_zero", RunU64DivZeroTest},
  {"u64_overflow", RunU64OverflowTest},
  {"bitwise_i32", RunBitwiseI32Test},
  {"shift_mask_i32", RunShiftMaskI32Test},
  {"bitwise_i64", RunBitwiseI64Test},
  {"shift_mask_i64", RunShiftMaskI64Test},
  {"return_ref", RunReturnRefTest},
  {"debug_noop", RunDebugNoopTest},
  {"diag_line_trap", RunLineTrapDiagTest},
  {"diag_trap_operands", RunTrapOperandDiagTest},
  {"verify_metadata", RunVerifyMetadataTest},
  {"verify_metadata_nonref_global", RunVerifyMetadataNonRefGlobalTest},
  {"heap_reuse", RunHeapReuseTest},
  {"scratch_arena", RunScratchArenaTest},
  {"scratch_scope", RunScratchScopeTest},
  {"scratch_align", RunScratchArenaAlignmentTest},
  {"scratch_scope_enforced", RunScratchScopeEnforcedTest},
  {"scratch_poison", RunScratchArenaPoisonTest},
  {"heap_closure_mark", RunHeapClosureMarkTest},
  {"gc_stress", RunGcStressTest},
  {"gc_vm_stress", RunGcVmStressTest},
  {"gc_smoke", RunGcTest},
  {"field_ops", RunFieldTest},
  {"bad_field_verify", RunBadFieldVerifyTest},
  {"bad_const_string", RunBadConstStringVerifyTest},
  {"bad_type_verify", RunBadTypeVerifyTest},
  {"bad_intrinsic_id_verify", RunBadIntrinsicIdVerifyTest},
  {"bad_intrinsic_param_verify", RunBadIntrinsicParamVerifyTest},
  {"intrinsic_return_verify", RunIntrinsicReturnVerifyTest},
  {"bad_syscall_verify", RunBadSysCallVerifyTest},
  {"bad_merge_verify", RunBadMergeVerifyTest},
  {"bad_merge_height_verify", RunBadMergeHeightVerifyTest},
  {"bad_merge_ref_i32_verify", RunBadMergeRefI32VerifyTest},
  {"bad_local_uninit_verify", RunBadLocalUninitVerifyTest},
  {"bad_stack_underflow_verify", RunBadStackUnderflowVerifyTest},
  {"bad_string_concat_verify", RunBadStringConcatVerifyTest},
  {"bad_string_get_char_verify", RunBadStringGetCharVerifyTest},
  {"bad_string_get_char_idx_verify", RunBadStringGetCharIdxVerifyTest},
  {"bad_string_slice_verify", RunBadStringSliceVerifyTest},
  {"bad_new_closure_verify", RunBadNewClosureVerifyTest},
  {"bad_upvalue_type_verify", RunBadUpvalueTypeVerifyTest},
  {"bad_string_slice_start_verify", RunBadStringSliceStartVerifyTest},
  {"bad_string_slice_end_verify", RunBadStringSliceEndVerifyTest},
  {"bad_is_null_verify", RunBadIsNullVerifyTest},
  {"bad_ref_eq_verify", RunBadRefEqVerifyTest},
  {"bad_ref_eq_mixed_verify", RunBadRefEqMixedVerifyTest},
  {"bad_ref_ne_verify", RunBadRefNeVerifyTest},
  {"bad_ref_ne_mixed_verify", RunBadRefNeMixedVerifyTest},
  {"bad_typeof_verify", RunBadTypeOfVerifyTest},
  {"bad_load_field_type_verify", RunBadLoadFieldTypeVerifyTest},
  {"bad_store_field_object_verify", RunBadStoreFieldObjectVerifyTest},
  {"bad_store_field_value_verify", RunBadStoreFieldValueVerifyTest},
  {"bad_array_len_verify", RunBadArrayLenVerifyTest},
  {"bad_array_get_idx_verify", RunBadArrayGetIdxVerifyTest},
  {"bad_array_set_idx_verify", RunBadArraySetIdxVerifyTest},
  {"bad_array_set_value_verify", RunBadArraySetValueVerifyTest},
  {"bad_array_set_i32_bool_value_verify", RunBadArraySetI32BoolValueVerifyTest},
  {"bad_array_set_i64_value_verify", RunBadArraySetI64ValueVerifyTest},
  {"bad_array_set_f32_value_verify", RunBadArraySetF32ValueVerifyTest},
  {"bad_array_set_f64_value_verify", RunBadArraySetF64ValueVerifyTest},
  {"bad_array_set_ref_value_verify", RunBadArraySetRefValueVerifyTest},
  {"bad_list_len_verify", RunBadListLenVerifyTest},
  {"bad_list_get_idx_verify", RunBadListGetIdxVerifyTest},
  {"bad_list_set_value_verify", RunBadListSetValueVerifyTest},
  {"bad_list_set_i64_value_verify", RunBadListSetI64ValueVerifyTest},
  {"bad_list_set_f32_value_verify", RunBadListSetF32ValueVerifyTest},
  {"bad_list_set_f64_value_verify", RunBadListSetF64ValueVerifyTest},
  {"bad_list_set_ref_value_verify", RunBadListSetRefValueVerifyTest},
  {"bad_list_push_value_verify", RunBadListPushValueVerifyTest},
  {"bad_list_pop_verify", RunBadListPopVerifyTest},
  {"bad_list_insert_value_verify", RunBadListInsertValueVerifyTest},
  {"bad_list_remove_idx_verify", RunBadListRemoveIdxVerifyTest},
  {"bad_list_clear_verify", RunBadListClearVerifyTest},
  {"bad_string_len_verify", RunBadStringLenVerifyTest},
  {"bad_bool_not_verify", RunBadBoolNotVerifyTest},
  {"bad_bool_and_verify", RunBadBoolAndVerifyTest},
  {"bad_bool_and_mixed_verify", RunBadBoolAndMixedVerifyTest},
  {"bad_bool_or_verify", RunBadBoolOrVerifyTest},
  {"bad_bool_or_mixed_verify", RunBadBoolOrMixedVerifyTest},
  {"bad_jmp_cond_verify", RunBadJmpCondVerifyTest},
  {"bad_jmp_false_cond_verify", RunBadJmpFalseCondVerifyTest},
  {"bad_array_get_arr_verify", RunBadArrayGetArrVerifyTest},
  {"bad_array_set_arr_verify", RunBadArraySetArrVerifyTest},
  {"bad_list_get_list_verify", RunBadListGetListVerifyTest},
  {"bad_list_set_list_verify", RunBadListSetListVerifyTest},
  {"bad_list_push_list_verify", RunBadListPushListVerifyTest},
  {"bad_list_pop_list_verify", RunBadListPopListVerifyTest},
  {"bad_list_insert_list_verify", RunBadListInsertListVerifyTest},
  {"bad_list_remove_list_verify", RunBadListRemoveListVerifyTest},
  {"bad_list_clear_list_verify", RunBadListClearListVerifyTest},
  {"bad_jump_boundary_verify", RunBadJumpBoundaryVerifyTest},
  {"bad_jump_oob_verify", RunBadJumpOobVerifyTest},
  {"bad_jmp_runtime", RunBadJmpRuntimeTrapTest},
  {"bad_jmp_true_runtime", RunBadJmpTrueRuntimeTrapTest},
  {"bad_jmp_false_runtime", RunBadJmpFalseRuntimeTrapTest},
  {"bad_global_uninit_verify", RunBadGlobalUninitVerifyTest},
  {"global_init_string", RunGlobalInitStringTest},
  {"global_init_f32", RunGlobalInitF32Test},
  {"global_init_f64", RunGlobalInitF64Test},
  {"bad_global_init_const_load", RunBadGlobalInitConstLoadTest},
  {"bad_string_const_nul_load", RunBadStringConstNoNullLoadTest},
  {"bad_i128_blob_len_load", RunBadI128BlobLenLoadTest},
  {"bad_field_offset_load", RunBadFieldOffsetLoadTest},
  {"bad_field_size_load", RunBadFieldSizeLoadTest},
  {"bad_field_align_load", RunBadFieldAlignmentLoadTest},
  {"bad_type_const_load", RunBadTypeConstLoadTest},
  {"bad_global_init_type_runtime", RunBadGlobalInitTypeRuntimeTest},
  {"good_string_const_load", RunGoodStringConstLoadTest},
  {"good_i128_blob_len_load", RunGoodI128BlobLenLoadTest},
  {"bad_sig_callconv_load", RunBadSigCallConvLoadTest},
  {"bad_sig_param_types_missing_load", RunBadSigParamTypesMissingLoadTest},
  {"bad_sig_param_type_start_load", RunBadSigParamTypeStartLoadTest},
  {"bad_sig_call_conv_load", RunBadSigCallConvLoadTest},
  {"bad_sig_param_type_misaligned_load", RunBadSigParamTypeMisalignedLoadTest},
  {"bad_sig_param_type_id_load", RunBadSigParamTypeIdLoadTest},
  {"bad_sig_ret_type_id_load", RunBadSigRetTypeIdLoadTest},
  {"bad_sig_table_truncated_load", RunBadSigTableTruncatedLoadTest},
  {"bad_section_alignment_load", RunBadSectionAlignmentLoadTest},
  {"bad_section_overlap_load", RunBadSectionOverlapLoadTest},
  {"bad_unknown_section_id_load", RunBadUnknownSectionIdLoadTest},
  {"bad_duplicate_section_id_load", RunBadDuplicateSectionIdLoadTest},
  {"bad_section_table_oob_load", RunBadSectionTableOobLoadTest},
  {"bad_endian_header_load", RunBadEndianHeaderLoadTest},
  {"bad_header_flags_load", RunBadHeaderFlagsLoadTest},
  {"bad_header_magic_load", RunBadHeaderMagicLoadTest},
  {"bad_header_version_load", RunBadHeaderVersionLoadTest},
  {"bad_header_version_past_load", RunPastHeaderVersionLoadTest},
  {"good_header_version_load", RunGoodHeaderVersionLoadTest},
  {"bad_header_reserved_load", RunBadHeaderReservedLoadTest},
  {"bad_debug_header_load", RunBadDebugHeaderLoadTest},
  {"bad_debug_line_oob_load", RunBadDebugLineOobLoadTest},
  {"good_debug_load", RunGoodDebugLoadTest},
  {"bad_section_count_zero_load", RunBadSectionCountZeroLoadTest},
  {"bad_section_table_misaligned_load", RunBadSectionTableMisalignedLoadTest},
  {"bad_section_table_offset_oob_load", RunBadSectionTableOffsetOobLoadTest},
  {"bad_types_table_size_load", RunBadTypesTableSizeLoadTest},
  {"bad_type_kind_load", RunBadTypeKindLoadTest},
  {"bad_type_kind_size_load", RunBadTypeKindSizeLoadTest},
  {"bad_type_kind_ref_size_load", RunBadTypeKindRefSizeLoadTest},
  {"bad_type_kind_fields_load", RunBadTypeKindFieldsLoadTest},
  {"bad_type_kind_ref_fields_load", RunBadTypeKindRefFieldsLoadTest},
  {"good_type_kind_ref_size_load", RunGoodTypeKindRefSizeLoadTest},
  {"bad_unknown_opcode_load", RunBadUnknownOpcodeLoadTest},
  {"bad_operand_overrun_load", RunBadOperandOverrunLoadTest},
  {"bad_code_alignment_load", RunBadCodeAlignmentLoadTest},
  {"bad_imports_table_size_load", RunBadImportsTableSizeLoadTest},
  {"bad_imports_missing_const_pool_load", RunBadImportsMissingConstPoolLoadTest},
  {"bad_exports_table_size_load", RunBadExportsTableSizeLoadTest},
  {"bad_exports_missing_const_pool_load", RunBadExportsMissingConstPoolLoadTest},
  {"bad_import_name_offset_load", RunBadImportNameOffsetLoadTest},
  {"bad_import_sig_id_load", RunBadImportSigIdLoadTest},
  {"bad_import_flags_load", RunBadImportFlagsLoadTest},
  {"bad_export_name_offset_load", RunBadExportNameOffsetLoadTest},
  {"bad_export_func_id_load", RunBadExportFuncIdLoadTest},
  {"bad_export_flags_load", RunBadExportFlagsLoadTest},
  {"bad_export_reserved_load", RunBadExportReservedLoadTest},
  {"bad_import_duplicate_load", RunBadImportDuplicateLoadTest},
  {"bad_export_duplicate_load", RunBadExportDuplicateLoadTest},
  {"import_call", RunImportCallTest},
  {"import_call_host", RunImportCallHostResolverTest},
  {"import_call_indirect", RunImportCallIndirectTest},
  {"import_dl_open_null", RunImportDlOpenNullTest},
  {"import_time_mono", RunImportTimeMonoTest},
  {"import_cwd_get", RunImportCwdGetTest},
  {"import_tailcall", RunImportTailCallTest},
  {"import_args_count", RunImportArgsCountTest},
  {"import_args_get_char", RunImportArgsGetCharEqTest},
  {"import_env_get_char", RunImportEnvGetCharEqTest},
  {"import_env_get_missing", RunImportEnvGetMissingTest},
  {"import_args_get_oob", RunImportArgsGetOobTest},
  {"import_args_get_neg", RunImportArgsGetNegTest},
  {"import_fs_open_stub", RunImportFsOpenStubTest},
  {"import_fs_open_null_path", RunImportFsOpenNullPathTest},
  {"import_fs_read_bad_fd", RunImportFsReadBadFdTest},
  {"import_fs_read_after_close", RunImportFsReadAfterCloseTest},
  {"import_fs_read_no_overwrite", RunImportFsReadClampNoOverwriteTest},
  {"import_fs_persist_write_read", RunImportFsWriteReadPersistTest},
  {"import_fs_write_readonly", RunImportFsWriteAfterReadOnlyOpenTest},
  {"import_fs_open_close_loop", RunImportFsOpenCloseLoopTest},
  {"import_fs_open_close_stress", RunImportFsOpenCloseStressTest},
  {"import_fs_write_clamp_count", RunImportFsWriteClampCountTest},
  {"import_fs_read_zero_preserve", RunImportFsReadZeroLenPreserveTest},
  {"import_fs_read_write_cycle", RunImportFsReadWriteCycleTest},
  {"import_fs_read_zero_nonempty", RunImportFsReadZeroLenNonEmptyBufTest},
  {"import_core_log", RunImportCoreLogTest},
  {"import_fs_read_clamp", RunImportFsReadClampTest},
  {"import_fs_read_stub", RunImportFsReadStubTest},
  {"import_fs_read_non_array", RunImportFsReadNonArrayBufTest},
  {"import_fs_read_zero_len", RunImportFsReadZeroLenTest},
  {"import_fs_write_clamp", RunImportFsWriteClampTest},
  {"import_fs_write_stub", RunImportFsWriteStubTest},
  {"import_fs_write_bad_fd", RunImportFsWriteBadFdTest},
  {"import_fs_write_after_close", RunImportFsWriteAfterCloseTest},
  {"import_fs_open_reopen", RunImportFsOpenCloseReopenTest},
  {"import_fs_write_zero_len", RunImportFsWriteZeroLenTest},
  {"import_fs_read_zero_buf", RunImportFsReadZeroBufTest},
  {"import_fs_write_zero_buf", RunImportFsWriteZeroBufTest},
  {"import_fs_write_null_buf", RunImportFsWriteNullBufTest},
  {"import_fs_close_stub", RunImportFsCloseStubTest},
  {"import_fs_close_bad_fd", RunImportFsCloseBadFdTest},
  {"import_fs_close_twice", RunImportFsCloseTwiceTest},
  {"import_fs_round_trip", RunImportFsRoundTripTest},
  {"bad_import_call_param_verify", RunBadImportCallParamVerifyTest},
  {"bad_fields_table_size_load", RunBadFieldsTableSizeLoadTest},
  {"bad_methods_table_size_load", RunBadMethodsTableSizeLoadTest},
  {"bad_named_method_sig_load", RunBadNamedMethodSigLoadTest},
  {"bad_sigs_table_size_load", RunBadSigsTableSizeLoadTest},
  {"bad_globals_table_size_load", RunBadGlobalsTableSizeLoadTest},
  {"bad_functions_table_size_load", RunBadFunctionsTableSizeLoadTest},
  {"bad_type_field_range_load", RunBadTypeFieldRangeLoadTest},
  {"bad_field_type_id_load", RunBadFieldTypeIdLoadTest},
  {"bad_global_type_id_load", RunBadGlobalTypeIdLoadTest},
  {"bad_function_method_id_load", RunBadFunctionMethodIdLoadTest},
  {"bad_method_sig_id_load", RunBadMethodSigIdLoadTest},
  {"missing_code_section_load", RunMissingCodeSectionLoadTest},
  {"missing_functions_section_load", RunMissingFunctionsSectionLoadTest},
  {"bad_const_string_offset_load", RunBadConstStringOffsetLoadTest},
  {"bad_const_i128_offset_load", RunBadConstI128OffsetLoadTest},
  {"bad_const_f64_truncated_load", RunBadConstF64TruncatedLoadTest},
  {"bad_method_flags_load", RunBadMethodFlagsLoadTest},
  {"bad_param_locals_verify", RunBadParamLocalsVerifyTest},
  {"bad_stack_max_zero_load", RunBadStackMaxZeroLoadTest},
  {"bad_entry_method_load", RunBadEntryMethodLoadTest},
  {"bad_function_offset_load", RunBadFunctionOffsetLoadTest},
  {"bad_method_offset_load", RunBadMethodOffsetLoadTest},
  {"bad_function_overlap_load", RunBadFunctionOverlapLoadTest},
  {"bad_stack_max_verify", RunBadStackMaxVerifyTest},
  {"bad_call_indirect_verify", RunBadCallIndirectVerifyTest},
  {"bad_call_verify", RunBadCallVerifyTest},
  {"bad_call_param_type_verify", RunBadCallParamTypeVerifyTest},
  {"bad_call_param_i8_to_i32_verify", RunBadCallParamI8ToI32VerifyTest},
  {"bad_call_indirect_param_type_verify", RunBadCallIndirectParamTypeVerifyTest},
  {"bad_call_indirect_param_i8_to_i32_verify", RunBadCallIndirectParamI8ToI32VerifyTest},
  {"bad_tailcall_param_type_verify", RunBadTailCallParamTypeVerifyTest},
  {"bad_tailcall_param_i8_to_i32_verify", RunBadTailCallParamI8ToI32VerifyTest},
  {"bad_tailcall_verify", RunBadTailCallVerifyTest},
  {"bad_return_verify", RunBadReturnVerifyTest},
  {"bad_conv_verify", RunBadConvVerifyTest},
  {"bad_bitwise_verify", RunBadBitwiseVerifyTest},
  {"bad_u32_verify", RunBadU32VerifyTest},
  {"bad_neg_i32_verify", RunBadNegI32VerifyTest},
  {"bad_neg_f32_verify", RunBadNegF32VerifyTest},
  {"bad_inc_i32_verify", RunBadIncI32VerifyTest},
  {"bad_inc_f32_verify", RunBadIncF32VerifyTest},
  {"bad_inc_u32_verify", RunBadIncU32VerifyTest},
  {"bad_inc_i8_verify", RunBadIncI8VerifyTest},
  {"bad_neg_i8_verify", RunBadNegI8VerifyTest},
  {"bad_neg_u32_verify", RunBadNegU32VerifyTest},
  {"bad_jmp_table_kind_load", RunBadJmpTableKindLoadTest},
  {"bad_jmp_table_blob_load", RunBadJmpTableBlobLoadTest},
  {"bad_jmp_table_oob_verify", RunBadJmpTableVerifyOobTargetTest},
  {"bad_jmp_table_default_oob_verify", RunBadJmpTableVerifyDefaultOobTest},
  {"bad_jmp_table_oob_runtime", RunBadJmpTableOobTargetTrapTest},
  {"bad_u64_verify", RunBadU64VerifyTest},
  {"callcheck", RunCallCheckTest},
  {"call_param_types", RunCallParamTypeTest},
  {"cmp_mixed_small_types", RunCmpMixedSmallTypesTest},
  {"array_set_i32_char", RunArraySetI32WithCharTest},
  {"call_indirect", RunCallIndirectTest},
  {"call_indirect_param_types", RunCallIndirectParamTypeTest},
  {"tailcall", RunTailCallTest},
  {"jump_to_end", RunJumpToEndTest},
  {"intrinsic_trap", RunIntrinsicTrapTest},
  {"intrinsic_core", RunIntrinsicCoreTest},
  {"intrinsic_time", RunIntrinsicTimeTest},
  {"syscall_trap", RunSysCallTrapTest},
  {"bad_call_indirect", RunBadCallIndirectTrapTest},
  {"bad_call_indirect_type", RunBadCallIndirectTypeTrapTest},
  {"bad_conv_runtime", RunBadConvRuntimeTrapTest},
  {"bad_bitwise_runtime", RunBadBitwiseRuntimeTrapTest},
  {"bad_u32_runtime", RunBadU32RuntimeTrapTest},
  {"bad_u64_runtime", RunBadU64RuntimeTrapTest},
  {"bad_upvalue_index", RunBadUpvalueIndexTrapTest},
  {"bad_const_i128_kind", RunBadConstI128KindTrapTest},
  {"bad_const_u128_blob", RunBadConstU128BlobTrapTest},
  {"bad_array_get", RunBadArrayGetTrapTest},
  {"bad_array_len_null", RunBadArrayLenNullTrapTest},
  {"bad_array_get_null", RunBadArrayGetNullTrapTest},
  {"bad_array_set_null", RunBadArraySetNullTrapTest},
  {"bad_array_set", RunBadArraySetTrapTest},
  {"bad_array_get_neg_index", RunBadArrayGetNegIndexTrapTest},
  {"bad_array_set_neg_index", RunBadArraySetNegIndexTrapTest},
  {"bad_array_get_i64", RunBadArrayGetI64TrapTest},
  {"bad_array_get_i64_null", RunBadArrayGetI64NullTrapTest},
  {"bad_array_get_i64_neg_index", RunBadArrayGetI64NegIndexTrapTest},
  {"bad_array_get_f32", RunBadArrayGetF32TrapTest},
  {"bad_array_get_f32_null", RunBadArrayGetF32NullTrapTest},
  {"bad_array_get_f32_neg_index", RunBadArrayGetF32NegIndexTrapTest},
  {"bad_array_get_f64", RunBadArrayGetF64TrapTest},
  {"bad_array_get_f64_null", RunBadArrayGetF64NullTrapTest},
  {"bad_array_get_f64_neg_index", RunBadArrayGetF64NegIndexTrapTest},
  {"bad_array_get_ref", RunBadArrayGetRefTrapTest},
  {"bad_array_get_ref_null", RunBadArrayGetRefNullTrapTest},
  {"bad_array_get_ref_neg_index", RunBadArrayGetRefNegIndexTrapTest},
  {"bad_array_set_i64", RunBadArraySetI64TrapTest},
  {"bad_array_set_i64_null", RunBadArraySetI64NullTrapTest},
  {"bad_array_set_i64_neg_index", RunBadArraySetI64NegIndexTrapTest},
  {"bad_array_set_f32", RunBadArraySetF32TrapTest},
  {"bad_array_set_f32_null", RunBadArraySetF32NullTrapTest},
  {"bad_array_set_f32_neg_index", RunBadArraySetF32NegIndexTrapTest},
  {"bad_array_set_f64", RunBadArraySetF64TrapTest},
  {"bad_array_set_f64_null", RunBadArraySetF64NullTrapTest},
  {"bad_array_set_f64_neg_index", RunBadArraySetF64NegIndexTrapTest},
  {"bad_array_set_ref", RunBadArraySetRefTrapTest},
  {"bad_array_set_ref_null", RunBadArraySetRefNullTrapTest},
  {"bad_array_set_ref_neg_index", RunBadArraySetRefNegIndexTrapTest},
  {"bad_list_get", RunBadListGetTrapTest},
  {"bad_list_len_null", RunBadListLenNullTrapTest},
  {"bad_list_get_null", RunBadListGetNullTrapTest},
  {"bad_list_get_i64", RunBadListGetI64TrapTest},
  {"bad_list_get_i64_null", RunBadListGetI64NullTrapTest},
  {"bad_list_get_i64_neg_index", RunBadListGetI64NegIndexTrapTest},
  {"bad_list_get_f32", RunBadListGetF32TrapTest},
  {"bad_list_get_f32_null", RunBadListGetF32NullTrapTest},
  {"bad_list_get_f32_neg_index", RunBadListGetF32NegIndexTrapTest},
  {"bad_list_get_f64", RunBadListGetF64TrapTest},
  {"bad_list_get_f64_null", RunBadListGetF64NullTrapTest},
  {"bad_list_get_f64_neg_index", RunBadListGetF64NegIndexTrapTest},
  {"bad_list_get_ref", RunBadListGetRefTrapTest},
  {"bad_list_get_ref_null", RunBadListGetRefNullTrapTest},
  {"bad_list_get_ref_neg_index", RunBadListGetRefNegIndexTrapTest},
  {"bad_list_set", RunBadListSetTrapTest},
  {"bad_list_set_null", RunBadListSetNullTrapTest},
  {"bad_list_get_neg_index", RunBadListGetNegIndexTrapTest},
  {"bad_list_set_neg_index", RunBadListSetNegIndexTrapTest},
  {"bad_list_set_i64", RunBadListSetI64TrapTest},
  {"bad_list_set_i64_null", RunBadListSetI64NullTrapTest},
  {"bad_list_set_i64_neg_index", RunBadListSetI64NegIndexTrapTest},
  {"bad_list_set_f32", RunBadListSetF32TrapTest},
  {"bad_list_set_f32_null", RunBadListSetF32NullTrapTest},
  {"bad_list_set_f32_neg_index", RunBadListSetF32NegIndexTrapTest},
  {"bad_list_set_f64", RunBadListSetF64TrapTest},
  {"bad_list_set_f64_null", RunBadListSetF64NullTrapTest},
  {"bad_list_set_f64_neg_index", RunBadListSetF64NegIndexTrapTest},
  {"bad_list_set_ref", RunBadListSetRefTrapTest},
  {"bad_list_set_ref_null", RunBadListSetRefNullTrapTest},
  {"bad_list_set_ref_neg_index", RunBadListSetRefNegIndexTrapTest},
  {"bad_list_pop", RunBadListPopTrapTest},
  {"bad_list_pop_i64", RunBadListPopI64TrapTest},
  {"bad_list_pop_i64_null", RunBadListPopI64NullTrapTest},
  {"bad_list_pop_f32", RunBadListPopF32TrapTest},
  {"bad_list_pop_f32_null", RunBadListPopF32NullTrapTest},
  {"bad_list_pop_f64", RunBadListPopF64TrapTest},
  {"bad_list_pop_f64_null", RunBadListPopF64NullTrapTest},
  {"bad_list_pop_ref", RunBadListPopRefTrapTest},
  {"bad_list_pop_ref_null", RunBadListPopRefNullTrapTest},
  {"bad_list_push_null", RunBadListPushNullTrapTest},
  {"bad_list_pop_null", RunBadListPopNullTrapTest},
  {"bad_list_insert", RunBadListInsertTrapTest},
  {"bad_list_insert_i64", RunBadListInsertI64TrapTest},
  {"bad_list_insert_i64_null", RunBadListInsertI64NullTrapTest},
  {"bad_list_insert_i64_neg_index", RunBadListInsertI64NegIndexTrapTest},
  {"bad_list_insert_f32", RunBadListInsertF32TrapTest},
  {"bad_list_insert_f32_null", RunBadListInsertF32NullTrapTest},
  {"bad_list_insert_f32_neg_index", RunBadListInsertF32NegIndexTrapTest},
  {"bad_list_insert_f64", RunBadListInsertF64TrapTest},
  {"bad_list_insert_f64_null", RunBadListInsertF64NullTrapTest},
  {"bad_list_insert_f64_neg_index", RunBadListInsertF64NegIndexTrapTest},
  {"bad_list_insert_ref", RunBadListInsertRefTrapTest},
  {"bad_list_insert_ref_null", RunBadListInsertRefNullTrapTest},
  {"bad_list_insert_ref_neg_index", RunBadListInsertRefNegIndexTrapTest},
  {"bad_list_insert_null", RunBadListInsertNullTrapTest},
  {"bad_list_remove", RunBadListRemoveTrapTest},
  {"bad_list_remove_i64", RunBadListRemoveI64TrapTest},
  {"bad_list_remove_i64_null", RunBadListRemoveI64NullTrapTest},
  {"bad_list_remove_i64_neg_index", RunBadListRemoveI64NegIndexTrapTest},
  {"bad_list_remove_f32", RunBadListRemoveF32TrapTest},
  {"bad_list_remove_f32_null", RunBadListRemoveF32NullTrapTest},
  {"bad_list_remove_f32_neg_index", RunBadListRemoveF32NegIndexTrapTest},
  {"bad_list_remove_f64", RunBadListRemoveF64TrapTest},
  {"bad_list_remove_f64_null", RunBadListRemoveF64NullTrapTest},
  {"bad_list_remove_f64_neg_index", RunBadListRemoveF64NegIndexTrapTest},
  {"bad_list_remove_ref", RunBadListRemoveRefTrapTest},
  {"bad_list_remove_ref_null", RunBadListRemoveRefNullTrapTest},
  {"bad_list_remove_ref_neg_index", RunBadListRemoveRefNegIndexTrapTest},
  {"bad_list_remove_null", RunBadListRemoveNullTrapTest},
  {"bad_list_clear_null", RunBadListClearNullTrapTest},
  {"bad_string_len_null", RunBadStringLenNullTrapTest},
  {"bad_string_concat_null", RunBadStringConcatNullTrapTest},
  {"bad_string_get_char_null", RunBadStringGetCharNullTrapTest},
  {"bad_string_get_char_neg_index", RunBadStringGetCharNegIndexTrapTest},
  {"bad_string_slice_neg_index", RunBadStringSliceNegIndexTrapTest},
  {"bad_string_get_char", RunBadStringGetCharTrapTest},
  {"bad_string_slice_null", RunBadStringSliceNullTrapTest},
  {"bad_string_slice", RunBadStringSliceTrapTest},
  {"list_growth", RunListGrowthTest},
};

static const TestCase kRuntimeSmokeTests[] = {
  {"import_call", RunImportCallTest},
  {"import_call_indirect", RunImportCallIndirectTest},
  {"import_dl_open_null", RunImportDlOpenNullTest},
  {"import_args_count", RunImportArgsCountTest},
  {"import_env_get_missing", RunImportEnvGetMissingTest},
  {"import_fs_open_null_path", RunImportFsOpenNullPathTest},
  {"import_fs_read_bad_fd", RunImportFsReadBadFdTest},
  {"import_fs_write_bad_fd", RunImportFsWriteBadFdTest},
  {"import_fs_close_bad_fd", RunImportFsCloseBadFdTest},
  {"diag_line_trap", RunLineTrapDiagTest},
  {"diag_trap_operands", RunTrapOperandDiagTest},
  {"intrinsic_trap", RunIntrinsicTrapTest},
  {"syscall_trap", RunSysCallTrapTest},
};

static const TestSection kCoreSections[] = {
  {"core", kCoreTests, sizeof(kCoreTests) / sizeof(kCoreTests[0])},
};

static const TestSection kRuntimeSmokeSections[] = {
  {"runtime_smoke", kRuntimeSmokeTests, sizeof(kRuntimeSmokeTests) / sizeof(kRuntimeSmokeTests[0])},
};

const TestSection* GetCoreSections(size_t* count) {
  if (count) {
    *count = sizeof(kCoreSections) / sizeof(kCoreSections[0]);
  }
  return kCoreSections;
}

const TestSection* GetRuntimeSmokeSections(size_t* count) {
  if (count) {
    *count = sizeof(kRuntimeSmokeSections) / sizeof(kRuntimeSmokeSections[0]);
  }
  return kRuntimeSmokeSections;
}
} // namespace Simple::VM::Tests
