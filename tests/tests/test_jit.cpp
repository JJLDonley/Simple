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
#include "jit/call_context.h"
#include "jit/llvm_backend.h"
#include "intrinsic_ids.h"
#include "runtime/execution_stats.h"
#include "runtime/values.h"
#include "test_utils.h"
#include "vm.h"

namespace Simple::VM::Tests {

using Simple::Byte::sbc::AppendI32;
using Simple::Byte::sbc::AppendI64;
using Simple::Byte::sbc::AppendU8;
using Simple::Byte::sbc::AppendU16;
using Simple::Byte::sbc::AppendU32;
using Simple::Byte::sbc::AppendU64;
using Simple::Byte::sbc::AppendConstString;
using Simple::Byte::sbc::AppendStringToPool;
using Simple::Byte::sbc::BuildModule;
using Simple::Byte::sbc::BuildModuleFromSections;
using Simple::Byte::sbc::BuildModuleWithTables;
using Simple::Byte::sbc::BuildModuleWithFunctionsAndSigs;
using Simple::Byte::sbc::SigSpec;
using Simple::Byte::sbc::SectionData;
using Simple::Byte::sbc::WriteU32;

extern "C" int32_t SimpleVmLlvmTestAddOneI32(int32_t value) {
  return value + 1;
}

extern "C" int64_t SimpleVmLlvmTestAddOneI64(int64_t value) {
  return value + 1;
}

extern "C" int32_t SimpleVmLlvmTestCStringLength(const char* value) {
  return value ? static_cast<int32_t>(std::strlen(value)) : -1;
}


std::vector<uint8_t> BuildModuleWithFunctions(const std::vector<std::vector<uint8_t>>& funcs,
                                              const std::vector<uint16_t>& locals);
std::vector<uint8_t> BuildModuleWithFunctionsAndSig(const std::vector<std::vector<uint8_t>>& funcs,
                                                    const std::vector<uint16_t>& locals,
                                                    uint32_t ret_type_id,
                                                    uint16_t param_count,
                                                    const std::vector<uint32_t>& param_types);
std::vector<uint8_t> BuildModuleWithFunctionsAndSigsWithTables(
    const std::vector<std::vector<uint8_t>>& funcs,
    const std::vector<uint16_t>& local_counts,
    const std::vector<uint32_t>& method_sig_ids,
    const std::vector<SigSpec>& sig_specs,
    const std::vector<uint8_t>& const_pool,
    const std::vector<uint8_t>& types);

std::vector<uint8_t> BuildTypesI32RefString();

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

std::vector<uint8_t> BuildModuleWithFunctionsAndSigsWithTables(
    const std::vector<std::vector<uint8_t>>& funcs,
    const std::vector<uint16_t>& local_counts,
    const std::vector<uint32_t>& method_sig_ids,
    const std::vector<SigSpec>& sig_specs,
    const std::vector<uint8_t>& const_pool,
    const std::vector<uint8_t>& types) {
  std::vector<uint8_t> fields;

  std::vector<uint8_t> sigs;
  std::vector<uint32_t> param_types;
  for (const auto& spec : sig_specs) {
    uint32_t param_type_start = static_cast<uint32_t>(param_types.size());
    AppendU32(sigs, spec.ret_type_id);
    AppendU16(sigs, spec.param_count);
    AppendU16(sigs, 0);
    AppendU32(sigs, param_type_start);
    for (uint32_t type_id : spec.param_types) {
      param_types.push_back(type_id);
    }
  }
  for (uint32_t type_id : param_types) {
    AppendU32(sigs, type_id);
  }

  std::vector<uint8_t> methods;
  std::vector<uint8_t> functions;
  std::vector<uint8_t> code;
  size_t offset = 0;
  for (size_t i = 0; i < funcs.size(); ++i) {
    uint16_t locals = 0;
    if (i < local_counts.size()) locals = local_counts[i];
    uint32_t sig_id = 0;
    if (i < method_sig_ids.size()) sig_id = method_sig_ids[i];
    AppendU32(methods, 0);
    AppendU32(methods, sig_id);
    AppendU32(methods, static_cast<uint32_t>(offset));
    AppendU16(methods, locals);
    AppendU16(methods, 0);

    AppendU32(functions, static_cast<uint32_t>(i));
    AppendU32(functions, static_cast<uint32_t>(offset));
    AppendU32(functions, static_cast<uint32_t>(funcs[i].size()));
    AppendU32(functions, 12);

    code.insert(code.end(), funcs[i].begin(), funcs[i].end());
    offset += funcs[i].size();
  }

  std::vector<uint8_t> globals;
  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, static_cast<uint32_t>(funcs.size()), 0});
  sections.push_back({4, sigs, static_cast<uint32_t>(sig_specs.size()), 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, static_cast<uint32_t>(funcs.size()), 0});
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = 32;
  const size_t table_size = section_count * 16u;
  size_t cursor = Simple::Byte::sbc::Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Simple::Byte::sbc::Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);
  WriteU32(module, 0x00, 0x30434253u);
  module[0x04] = 0x01;
  module[0x05] = 0x00;
  module[0x06] = 1;
  module[0x07] = 0;
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : sections) {
    WriteU32(module, table_off + 0, sec.id);
    WriteU32(module, table_off + 4, sec.offset);
    WriteU32(module, table_off + 8, static_cast<uint32_t>(sec.bytes.size()));
    WriteU32(module, table_off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

std::vector<uint8_t> BuildTypesI32Void() {
  std::vector<uint8_t> types;
  auto append_type = [&](Simple::Byte::TypeKind kind, uint32_t size) {
    AppendU32(types, 0);
    AppendU8(types, static_cast<uint8_t>(kind));
    AppendU8(types, 0);
    AppendU16(types, 0);
    AppendU32(types, size);
    AppendU32(types, 0);
    AppendU32(types, 0);
  };
  append_type(Simple::Byte::TypeKind::I32, 4);
  append_type(Simple::Byte::TypeKind::Void, 0);
  return types;
}

std::vector<uint8_t> BuildTypesUnspecifiedI32F32() {
  std::vector<uint8_t> types;
  auto append_type = [&](Simple::Byte::TypeKind kind, uint32_t size) {
    AppendU32(types, 0);
    AppendU8(types, static_cast<uint8_t>(kind));
    AppendU8(types, 0);
    AppendU16(types, 0);
    AppendU32(types, size);
    AppendU32(types, 0);
    AppendU32(types, 0);
  };
  append_type(Simple::Byte::TypeKind::Unspecified, 0);
  append_type(Simple::Byte::TypeKind::I32, 4);
  append_type(Simple::Byte::TypeKind::F32, 4);
  return types;
}

std::vector<uint8_t> BuildTypesI32RefString() {
  std::vector<uint8_t> types;
  auto append_type = [&](Simple::Byte::TypeKind kind, uint32_t size) {
    AppendU32(types, 0);
    AppendU8(types, static_cast<uint8_t>(kind));
    AppendU8(types, 0);
    AppendU16(types, 0);
    AppendU32(types, size);
    AppendU32(types, 0);
    AppendU32(types, 0);
  };
  append_type(Simple::Byte::TypeKind::I32, 4);
  append_type(Simple::Byte::TypeKind::Ref, 4);
  append_type(Simple::Byte::TypeKind::String, 4);
  return types;
}

std::vector<uint8_t> BuildTypesI32RefStringI64() {
  std::vector<uint8_t> types = BuildTypesI32RefString();
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I64));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 0);
  return types;
}

std::vector<uint8_t> BuildSingleImportFunctionModuleWithTypes(
    const std::vector<uint8_t>& main_code,
    uint16_t main_locals,
    const std::string& module_name,
    const std::string& symbol_name,
    const SigSpec& import_sig,
    const std::vector<uint8_t>& types,
    std::vector<uint8_t> const_pool = {}) {
  const uint32_t mod_off = static_cast<uint32_t>(AppendStringToPool(const_pool, module_name));
  const uint32_t sym_off = static_cast<uint32_t>(AppendStringToPool(const_pool, symbol_name));

  std::vector<uint8_t> methods;
  AppendU32(methods, 0); AppendU32(methods, 0); AppendU32(methods, 0); AppendU16(methods, main_locals); AppendU16(methods, 0);

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0); AppendU16(sigs, 0); AppendU16(sigs, 0); AppendU32(sigs, 0);
  AppendU32(sigs, import_sig.ret_type_id); AppendU16(sigs, import_sig.param_count); AppendU16(sigs, 0); AppendU32(sigs, 0);
  for (uint32_t type_id : import_sig.param_types) AppendU32(sigs, type_id);

  std::vector<uint8_t> functions;
  AppendU32(functions, 0); AppendU32(functions, 0); AppendU32(functions, static_cast<uint32_t>(main_code.size())); AppendU32(functions, 16);

  std::vector<uint8_t> imports;
  AppendU32(imports, mod_off); AppendU32(imports, sym_off); AppendU32(imports, 1); AppendU32(imports, 0);

  std::vector<uint8_t> code = main_code;
  AppendU8(code, static_cast<uint8_t>(Simple::Byte::OpCode::Nop));

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, {}, 0, 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 2, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, {}, 0, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({10, imports, 1, 0});
  sections.push_back({8, code, 0, 0});
  return BuildModuleFromSections(sections);
}

std::vector<uint8_t> BuildSingleImportFunctionModule(const std::vector<uint8_t>& main_code,
                                                     uint16_t main_locals,
                                                     const std::string& module_name,
                                                     const std::string& symbol_name,
                                                     const SigSpec& import_sig) {
  return BuildSingleImportFunctionModuleWithTypes(main_code, main_locals, module_name, symbol_name,
                                                  import_sig, BuildTypesI32Void());
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
  uint32_t const_id = 0;
  std::vector<uint8_t> const_pool;
  uint32_t str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, "hot"));
  AppendConstString(const_pool, str_offset, &const_id);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(callee, const_id);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  SigSpec entry_sig{0, 0, {}};
  std::vector<std::vector<uint8_t>> funcs{entry, callee};
  std::vector<uint16_t> locals{0, 0};
  std::vector<uint32_t> sig_ids{0, 0};
  std::vector<uint8_t> types = BuildTypesI32RefString();
  return BuildModuleWithFunctionsAndSigsWithTables(funcs, locals, sig_ids, {entry_sig}, const_pool, types);
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

std::vector<uint8_t> BuildJitCompiledRefOpsModule() {
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
  AppendU8(callee, static_cast<uint8_t>(OpCode::Line));
  AppendU32(callee, 1);
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::RefNe));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledRefOpsNoStackMapModule() {
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
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
}

std::vector<uint8_t> BuildJitCompiledArrayOpsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewArray));
  AppendU32(entry, 0);
  AppendU32(entry, 2);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 11);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ArraySetI32));
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 42);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ArraySetI32));
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
    AppendU32(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Line));
  AppendU32(callee, 1);
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Dup));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ArrayLen));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ArrayGetI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  SigSpec entry_sig{0, 0, {}};
  SigSpec callee_sig{0, 1, {1}};
  std::vector<std::vector<uint8_t>> funcs{entry, callee};
  std::vector<uint16_t> locals{1, 1};
  std::vector<uint32_t> sig_ids{0, 1};
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> types = BuildTypesI32RefString();
  return BuildModuleWithFunctionsAndSigsWithTables(funcs, locals, sig_ids, {entry_sig, callee_sig}, const_pool, types);
}

std::vector<uint8_t> BuildJitCompiledListOpsModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewList));
  AppendU32(entry, 0);
  AppendU32(entry, 2);
  AppendU8(entry, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 5);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 9);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ListPushI32));
  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
    AppendU32(entry, 0);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Line));
  AppendU32(callee, 1);
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ListLen));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ListSetI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ListGetI32));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  SigSpec entry_sig{0, 0, {}};
  SigSpec callee_sig{0, 1, {1}};
  std::vector<std::vector<uint8_t>> funcs{entry, callee};
  std::vector<uint16_t> locals{1, 1};
  std::vector<uint32_t> sig_ids{0, 1};
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> types = BuildTypesI32RefString();
  return BuildModuleWithFunctionsAndSigsWithTables(funcs, locals, sig_ids, {entry_sig, callee_sig}, const_pool, types);
}

std::vector<uint8_t> BuildJitCompiledStringLenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  uint32_t const_id = 0;
  std::vector<uint8_t> const_pool;
  uint32_t str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, "hi"));
  AppendConstString(const_pool, str_offset, &const_id);

  for (uint32_t i = 0; i + 1 < Simple::VM::kJitTier1Threshold; ++i) {
    AppendU8(entry, static_cast<uint8_t>(OpCode::ConstString));
    AppendU32(entry, const_id);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
    AppendU32(entry, 1);
    AppendU8(entry, 1);
    AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  }
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(entry, const_id);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(entry, const_id);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Line));
  AppendU32(callee, 1);
  AppendU32(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  SigSpec entry_sig{0, 0, {}};
  SigSpec callee_sig{0, 1, {2}};
  std::vector<std::vector<uint8_t>> funcs{entry, callee};
  std::vector<uint16_t> locals{0, 1};
  std::vector<uint32_t> sig_ids{0, 1};
  std::vector<uint8_t> types = BuildTypesI32RefString();
  return BuildModuleWithFunctionsAndSigsWithTables(funcs, locals, sig_ids, {entry_sig, callee_sig}, const_pool, types);
}

std::vector<uint8_t> BuildJitCompiledCallModule() {
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

  std::vector<uint8_t> caller;
  AppendU8(caller, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(caller, 0);
  AppendU8(caller, static_cast<uint8_t>(OpCode::Call));
  AppendU32(caller, 2);
  AppendU8(caller, 0);
  AppendU8(caller, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(caller, 1);
  AppendU8(caller, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(caller, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 6);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, caller, callee}, {0, 0, 0});
}

std::vector<uint8_t> BuildJitEnvThresholdModule() {
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
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 3);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  return BuildModuleWithFunctions({entry, callee}, {0, 0});
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

bool ExpectTier1CompiledCallee(const std::vector<uint8_t>& module_bytes,
                               int32_t expected_exit,
                               const char* name) {
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
  const Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << name << ": execution failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2 || exec.call_counts[1] != Simple::VM::kJitTier1Threshold) {
    std::cerr << name << ": callee did not reach the tier1 call threshold\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2 || exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << name << ": callee did not reach tier1\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2 || exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << name << ": callee did not execute compiled code\n";
    return false;
  }
  if (exec.exit_code != expected_exit) {
    std::cerr << name << ": expected exit code " << expected_exit << ", got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitTierTest() {
  std::vector<uint8_t> module_bytes = BuildJitTierModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  return ExpectTier1CompiledCallee(BuildJitCompiledI32ArithmeticModule(), 4, "RunJitCompiledI32ArithmeticTest");
}

bool RunJitCompiledScalarI32Test() {
  return ExpectTier1CompiledCallee(BuildJitCompiledScalarI32Module(), 3, "RunJitCompiledScalarI32Test");
}

bool RunJitCompiledI64U64Test() {
  return ExpectTier1CompiledCallee(BuildJitCompiledI64U64Module(), 1, "RunJitCompiledI64U64Test");
}

bool RunJitCompiledFloatOpsTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledFloatOpsModule(), 4, "RunJitCompiledFloatOpsTest");
}

bool RunJitCompiledConversionsTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledConversionsModule(), 12, "RunJitCompiledConversionsTest");
}

bool RunJitCompiledCompareScalarTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledCompareScalarModule(), 0, "RunJitCompiledCompareScalarTest");
}

bool RunJitCompiledI32LocalsArithmeticTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledI32LocalsArithmeticModule(), 4, "RunJitCompiledI32LocalsArithmeticTest");
}

bool RunJitCompiledI32CompareTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledI32CompareModule(), 1, "RunJitCompiledI32CompareTest");
}

bool RunJitCompiledCompareBoolIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledCompareBoolIndirectModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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

bool RunJitCompiledRefOpsTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledRefOpsModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] < Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count >= " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for ref-ops callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for ref-ops callee\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledRefOpsNoStackMapTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledRefOpsNoStackMapModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
    std::cerr << "expected Tier1 for no-stack-map callee\n";
    return false;
  }
  if (exec.jit_dispatch_counts.size() < 2) {
    std::cerr << "expected jit dispatch counts for functions\n";
    return false;
  }
  if (exec.jit_dispatch_counts[1] == 0) {
    std::cerr << "expected jit dispatch count for no-stack-map callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] != 0) {
    std::cerr << "expected no compiled execs for no-stack-map callee\n";
    return false;
  }
  if (exec.exit_code != 0) {
    std::cerr << "expected exit code 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledArrayOpsTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledArrayOpsModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] < Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count >= " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for array ops callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for array ops callee\n";
    return false;
  }
  if (exec.exit_code != 42) {
    std::cerr << "expected exit code 42, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledListOpsTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledListOpsModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] < Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count >= " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for list ops callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for list ops callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledStringLenTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledStringLenModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.call_counts.size() < 2) {
    std::cerr << "expected call counts for functions\n";
    return false;
  }
  if (exec.call_counts[1] < Simple::VM::kJitTier1Threshold) {
    std::cerr << "expected callee call count >= " << Simple::VM::kJitTier1Threshold
              << ", got " << exec.call_counts[1] << "\n";
    return false;
  }
  if (exec.jit_tiers.size() < 2) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for string len callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for string len callee\n";
    return false;
  }
  if (exec.exit_code != 2) {
    std::cerr << "expected exit code 2, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitCompiledCallTest() {
  std::vector<uint8_t> module_bytes = BuildJitCompiledCallModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module);
  if (exec.status != Simple::VM::ExecStatus::Halted) {
    std::cerr << "exec failed\n";
    return false;
  }
  if (exec.jit_tiers.size() < 3) {
    std::cerr << "expected jit tiers for functions\n";
    return false;
  }
  if (exec.jit_tiers[1] != Simple::VM::JitTier::Tier1) {
    std::cerr << "expected Tier1 for compiled caller\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 3) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for caller\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[2] == 0) {
    std::cerr << "expected compiled exec count for callee\n";
    return false;
  }
  if (exec.exit_code != 7) {
    std::cerr << "expected exit code 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitEnvThresholdTest() {
  struct EnvGuard {
    std::string name;
    explicit EnvGuard(std::string name) : name(std::move(name)) {}
    ~EnvGuard() { UnsetEnvVar(name); }
  };
  SetEnvVar("SIMPLE_JIT_TIER0", "1");
  SetEnvVar("SIMPLE_JIT_TIER1", "1");
  SetEnvVar("SIMPLE_JIT_OPCODE", "1");
  EnvGuard guard0("SIMPLE_JIT_TIER0");
  EnvGuard guard1("SIMPLE_JIT_TIER1");
  EnvGuard guard2("SIMPLE_JIT_OPCODE");

  std::vector<uint8_t> module_bytes = BuildJitEnvThresholdModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
    std::cerr << "expected Tier1 for env threshold callee\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts.size() < 2) {
    std::cerr << "expected compiled exec counts for functions\n";
    return false;
  }
  if (exec.jit_compiled_exec_counts[1] == 0) {
    std::cerr << "expected compiled exec count for env threshold callee\n";
    return false;
  }
  if (exec.exit_code != 3) {
    std::cerr << "expected exit code 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunJitParamCalleeTest() {
  std::vector<uint8_t> module_bytes = BuildJitParamCalleeModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
    std::cout << bench_case.name << " " << (enable_jit ? "tiered" : "nojit")
              << " iterations=" << iterations << " ms=" << ms << "\n";
    return true;
  };

  for (const auto& bench_case : cases) {
    if (!run_case(bench_case, false)) return 1;
    if (!run_case(bench_case, true)) return 1;
  }
  return 0;
}

int RunBenchHotLoop(size_t iterations) {
  struct BenchCase {
    const char* name;
    std::vector<uint8_t> bytes;
  };
  std::vector<BenchCase> cases;
  cases.push_back({"single_type", BuildJitCompiledLoopModule()});
  cases.push_back({"mixed_ops", BuildBenchMixedOpsModule()});
  cases.push_back({"calls", BuildBenchCallsModule()});

  struct EnvGuard {
    std::string name;
    explicit EnvGuard(std::string name) : name(std::move(name)) {}
    ~EnvGuard() { UnsetEnvVar(name); }
  };

  auto run_case = [&](const BenchCase& bench_case) {
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
    SetEnvVar("SIMPLE_JIT_TIER0", "1");
    SetEnvVar("SIMPLE_JIT_TIER1", "1");
    SetEnvVar("SIMPLE_JIT_OPCODE", "1");
    EnvGuard tier0_guard("SIMPLE_JIT_TIER0");
    EnvGuard tier1_guard("SIMPLE_JIT_TIER1");
    EnvGuard opcode_guard("SIMPLE_JIT_OPCODE");

    Simple::VM::ExecResult warmup = Simple::VM::ExecuteModule(load.module, true, true);
    if (warmup.status != Simple::VM::ExecStatus::Halted) {
      std::cerr << "bench warmup failed (" << bench_case.name << ")\n";
      return false;
    }

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
      Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, true, true);
      if (exec.status != Simple::VM::ExecStatus::Halted) {
        std::cerr << "bench hot exec failed (" << bench_case.name << ")\n";
        return false;
      }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << bench_case.name << " hot iterations=" << iterations << " ms=" << ms << "\n";
    return true;
  };

  for (const auto& bench_case : cases) {
    if (!run_case(bench_case)) return 1;
  }
  return 0;
}

bool RunJitOpcodeHotI32CompareTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotI32CompareModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  return ExpectTier1CompiledCallee(BuildJitCompiledBoolOpsModule(), 1, "RunJitCompiledBoolOpsTest");
}

bool RunJitCompiledLocalsBoolChainTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledLocalsBoolChainModule(), 1, "RunJitCompiledLocalsBoolChainTest");
}

bool RunJitCompiledLocalBoolStoreTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledLocalBoolStoreModule(), 1, "RunJitCompiledLocalBoolStoreTest");
}

bool RunJitCompiledLocalBoolAndOrTest() {
  return ExpectTier1CompiledCallee(BuildJitCompiledLocalBoolAndOrModule(), 1, "RunJitCompiledLocalBoolAndOrTest");
}

bool RunJitOpcodeHotLocalBoolAndOrTest() {
  std::vector<uint8_t> module_bytes = BuildJitOpcodeHotLocalBoolAndOrModule();
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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
  Simple::Byte::LoadResult load;
  if (!LoadAndVerifyModule(module_bytes, &load)) return false;
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

bool RunLlvmJitLeafI32SmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

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

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }

  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitLocalsAndParamsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSig({code}, {3}, 0, 2, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }

  std::vector<Simple::VM::Interpreter::Slot> args = {
      Simple::VM::Runtime::PackI32(20), Simple::VM::Runtime::PackI32(1)};
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, args, ret, has_ret, error)) {
    std::cerr << "LLVM JIT locals/params run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT locals/params return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCacheSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSig({code}, {2}, 0, 2, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }

  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  std::vector<Simple::VM::Interpreter::Slot> args1 = {
      Simple::VM::Runtime::PackI32(20), Simple::VM::Runtime::PackI32(22)};
  if (!backend.TryRunFunction(load.module, 0, args1, ret, has_ret, error) ||
      !has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected first cached LLVM JIT return 42: " << error << "\n";
    return false;
  }
  std::vector<Simple::VM::Interpreter::Slot> args2 = {
      Simple::VM::Runtime::PackI32(100), Simple::VM::Runtime::PackI32(23)};
  if (!backend.TryRunFunction(load.module, 0, args2, ret, has_ret, error) ||
      !has_ret || Simple::VM::Runtime::UnpackI32(ret) != 123) {
    std::cerr << "expected second cached LLVM JIT return 123: " << error << "\n";
    return false;
  }
  return true;
}

bool RunLlvmJitFloatSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 20.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 2.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddF64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT float run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT float return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitYieldSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Yield));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT yield run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT yield return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitHaltSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT halt run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT halt return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitJmpTableSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  for (const auto& [index, expected] : {std::pair<int32_t, int32_t>{0, 1},
                                       std::pair<int32_t, int32_t>{1, 2},
                                       std::pair<int32_t, int32_t>{7, 3}}) {
    Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildJmpTableModule(index));
    if (!load.ok) {
      std::cerr << "load failed: " << load.error << "\n";
      return false;
    }
    Simple::VM::Interpreter::Slot ret = 0;
    bool has_ret = false;
    std::string error;
    if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
      std::cerr << "LLVM JIT jmptable run failed: " << error << "\n";
      return false;
    }
    if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != expected) {
      std::cerr << "expected LLVM JIT jmptable return " << expected << "\n";
      return false;
    }
  }
  return true;
}

bool RunLlvmJitSqrtIntrinsicSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstF64));
  AppendF64(code, 1764.0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, Simple::VM::kIntrinsicSqrtF64);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT sqrt intrinsic run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT sqrt intrinsic return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitIntrinsicSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, -40);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, Simple::VM::kIntrinsicAbsI32);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, Simple::VM::kIntrinsicMaxI32);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT intrinsic run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 40) {
    std::cerr << "expected LLVM JIT intrinsic return 40\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCallIndirectHelperSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

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
  AppendI32(callee, 42);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModuleWithFunctions({entry, callee}, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT indirect call helper run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT indirect call helper return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitNoCaptureClosureSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::NewClosure));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(entry, 0);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 42);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModuleWithFunctions({entry, callee}, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT no-capture closure run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT no-capture closure return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitVoidCallHelperSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> module = BuildModuleWithFunctionsAndSigsWithTables(
      {entry, callee}, {0, 0}, {0, 0}, {SigSpec{1, 0, {}}, SigSpec{1, 0, {}}}, {}, BuildTypesI32Void());
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(module);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = true;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT void call helper run failed: " << error << "\n";
    return false;
  }
  if (has_ret) {
    std::cerr << "expected LLVM JIT void call helper to return no value\n";
    return false;
  }
  return true;
}

bool RunLlvmJitDirectTailCallHelperSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 42);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(main_code, 1);
  AppendU8(main_code, 1);

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  SigSpec main_sig{0, 0, {}};
  SigSpec callee_sig{0, 1, {0}};
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSigs({main_code, callee}, {0, 1}, {0, 1}, {main_sig, callee_sig}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT direct-tail-call helper run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT direct-tail-call helper return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitDirectCallHelperSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(main_code, 1);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 42);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctions({main_code, callee}, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT direct-call helper run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT direct-call helper return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCallImportHelperSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 42);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctions({main_code, callee}, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT call-import helper run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT call-import helper return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCallNativeHelperSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallNative));
  AppendU32(main_code, 1);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 42);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctions({main_code, callee}, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT call-native helper run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT call-native helper return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitSelfTailCallSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(code, 0);
  AppendU8(code, 2);
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(code, jmp_offset, static_cast<uint32_t>(rel));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSig({code}, {2}, 0, 2, {0, 0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  std::vector<Simple::VM::Interpreter::Slot> args = {
      Simple::VM::Runtime::PackI32(42), Simple::VM::Runtime::PackI32(0)};
  if (!backend.TryRunFunction(load.module, 0, args, ret, has_ret, error)) {
    std::cerr << "LLVM JIT self-tail-call run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT self-tail-call return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitSelfCallSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLeI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(code, 0);
  AppendU8(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(code, jmp_offset, static_cast<uint32_t>(rel));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSig({code}, {1}, 0, 1, {0}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {Simple::VM::Runtime::PackI32(42)}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT self-call run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT self-call return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitNullRefSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0x00000001FFFFFFFFull);
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 0x0000000100000001ull);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::RefEq));
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolAnd));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT null/ref run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 1) {
    std::cerr << "expected LLVM JIT null/ref return true\n";
    return false;
  }
  return true;
}

bool RunLlvmJitNarrowIntegerSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU8));
  AppendU8(code, 255);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncU8));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI8));
  AppendU8(code, 127);
  AppendU8(code, static_cast<uint8_t>(OpCode::IncI8));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 170);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT narrow int run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT narrow int return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitUnsignedSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpEqU32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT unsigned run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 1) {
    std::cerr << "expected LLVM JIT unsigned return true\n";
    return false;
  }
  return true;
}

bool RunLlvmJitBitopsConversionsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ShlI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::OrI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI32ToI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::AndI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT bitops/conversions run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT bitops/conversions return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitI64SmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 10);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::MulI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI64));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT i64 run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI64(ret) != 42) {
    std::cerr << "expected LLVM JIT i64 return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitGlobalSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 1, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT global run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT global return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitInitializedGlobalSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvF64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 1, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  uint32_t const_id = static_cast<uint32_t>(load.module.const_pool.size());
  AppendU32(load.module.const_pool, 4);
  AppendF64(load.module.const_pool, 42.0);
  load.module.globals[0].init_const_id = const_id;

  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT initialized global run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT initialized global return 42\n";
    return false;
  }
  return true;
}

void AppendExtendedOpcode(std::vector<uint8_t>& code, Simple::Byte::ExtendedOpCode ext) {
  AppendU8(code, static_cast<uint8_t>(Simple::Byte::OpCode::CallNative));
  AppendU32(code, Simple::Byte::kExtendedOpcodeSentinel);
  AppendU8(code, Simple::Byte::kExtendedOpcodeArgSentinel);
  AppendU16(code, static_cast<uint16_t>(ext));
}

bool RunLlvmJitCheckedI32SmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 50);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 8);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedSubI32);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedMulI32);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedDivI32);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT checked i32 run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT checked i32 return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCheckedU32SmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 4);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedAddU32);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedSubU32);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedMulU32);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedDivU32);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT checked u32 run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT checked u32 return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitChecked64SmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 50);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(code, 8);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedSubI64);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedMulU64);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU64));
  AppendU64(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedDivU64);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT checked 64 run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT checked 64 return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCheckedConvSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedConvI32ToI64);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedConvI64ToI32);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT checked conv run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT checked conv return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCheckedFloatConvSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedConvI32ToF32);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedConvF32ToF64);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedConvF64ToF32);
  AppendExtendedOpcode(code, ExtendedOpCode::CheckedConvF32ToI32);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT checked float conv run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT checked float conv return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCheckedBoundsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CheckedBounds));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 3));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT checked bounds run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT checked bounds return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitGuardBoundsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::GuardBounds);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 3));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT guard bounds run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT guard bounds return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitPointerOpsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::PtrAdd);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 100);
  AppendExtendedOpcode(code, ExtendedOpCode::PtrCheckBounds);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 3));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT pointer ops run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT pointer ops return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitMemoryPseudoOpsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 123);
  AppendExtendedOpcode(code, ExtendedOpCode::LoadPtr);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendExtendedOpcode(code, ExtendedOpCode::StorePtr);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendExtendedOpcode(code, ExtendedOpCode::MemCompare);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 4));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT memory pseudo ops run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT memory pseudo ops return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitConst128SmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> blob(16, 0x7B);
  uint32_t const_id = 0;
  AppendConstBlob(const_pool, 1, blob, &const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI128));
  AppendU32(code, const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::IsNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 41);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> empty;
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModuleWithTables(code, const_pool, empty, empty, 0, 0));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT const128 run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT const128 return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitTrapFallbackSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, Simple::VM::kIntrinsicTrap);

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 1));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "expected LLVM JIT intrinsic trap to fail/fallback\n";
    return false;
  }
  return error == "unsupported";
}

bool RunLlvmJitSysCallFallbackSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::SysCall));
  AppendU32(code, 123);

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 0));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "expected LLVM JIT syscall to fail/fallback\n";
    return false;
  }
  return error == "unsupported";
}

bool RunLlvmJitAddressTaskOpsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendExtendedOpcode(code, ExtendedOpCode::AddressOfLocal);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendExtendedOpcode(code, ExtendedOpCode::CaptureLocal);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendExtendedOpcode(code, ExtendedOpCode::MakeFuture);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 3));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT address/task ops run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT address/task ops return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitPseudoExtendedOpsSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::ExtendedOpCode;
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 40);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendExtendedOpcode(code, ExtendedOpCode::ResultOk);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendExtendedOpcode(code, ExtendedOpCode::AtomicSub);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendExtendedOpcode(code, ExtendedOpCode::EnumMake);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 4));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT pseudo extended ops run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 43) {
    std::cerr << "expected LLVM JIT pseudo extended ops return 43\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCompareBoolSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpGtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstBool));
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::BoolOr));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT compare/bool run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 1) {
    std::cerr << "expected LLVM JIT compare/bool return 1\n";
    return false;
  }
  return true;
}

bool RunLlvmJitForwardBranchSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_offset = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  size_t else_pos = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_offset + 4);
  WriteU32(code, jmp_offset, static_cast<uint32_t>(rel));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 0, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "LLVM JIT branch run failed: " << error << "\n";
    return false;
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 42) {
    std::cerr << "expected LLVM JIT branch return 42\n";
    return false;
  }
  return true;
}

bool RunLlvmJitCacheKeyIncludesAbiVersionsTest() {
  const std::string standalone = Simple::VM::Jit::BuildLlvmJitCacheKey(123, 4, 8, 16, 99, false);
  const std::string runtime = Simple::VM::Jit::BuildLlvmJitCacheKey(123, 4, 8, 16, 99, true);
  return standalone.find("jitabi=" + std::to_string(Simple::VM::Jit::kLlvmJitCacheAbiVersion)) !=
             std::string::npos &&
         standalone.find("helperabi=" +
                         std::to_string(Simple::VM::Jit::kLlvmJitRuntimeHelperAbiVersion)) !=
             std::string::npos &&
         standalone.find(":standalone") != std::string::npos &&
         runtime.find(":rt") != std::string::npos && standalone != runtime;
}

bool RunJitCallContextHelpersTest() {
  Simple::VM::Jit::JitCallContext context;
  context.args = {11, 22};
  context.locals = {0, 0};
  std::vector<Simple::VM::Jit::Slot> globals = {7};
  context.globals = &globals;

  Simple::VM::Jit::Slot value = 0;
  if (!Simple::VM::Jit::JitArg(context, 1, &value) || value != 22) return false;
  if (!Simple::VM::Jit::PushJitStack(&context, 66)) return false;
  if (!Simple::VM::Jit::JitStackSlot(context, 0, &value) || value != 66) return false;
  if (!Simple::VM::Jit::PopJitStack(&context, &value) || value != 66 ||
      !context.operand_stack.empty()) return false;
  if (!Simple::VM::Jit::SetJitLocal(&context, 0, 33)) return false;
  if (!Simple::VM::Jit::JitLocal(context, 0, &value) || value != 33) return false;
  if (!Simple::VM::Jit::SetJitGlobal(&context, 0, 44)) return false;
  if (!Simple::VM::Jit::JitGlobal(context, 0, &value) || value != 44 || globals[0] != 44) return false;

  Simple::VM::Jit::SetJitReturn(&context, 55);
  if (!context.has_return || context.return_value != 55) return false;
  Simple::VM::Jit::ClearJitReturn(&context);
  if (context.has_return || context.return_value != 0) return false;

  Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Fallback, "fallback");
  if (context.trap.kind != Simple::VM::Jit::JitCallTrapKind::Fallback ||
      context.trap.message != "fallback") {
    return false;
  }
  Simple::VM::Jit::RegisterJitRoot(&context, 123);
  if (context.root_refs.size() != 1 || context.root_refs[0] != 123) return false;
  Simple::VM::Jit::ClearJitRoots(&context);
  if (!context.root_refs.empty() || Simple::VM::Jit::JitArg(context, 9, &value)) return false;

  Simple::Byte::SbcModule module;
  module.types.resize(3);
  module.types[0].kind = static_cast<uint8_t>(Simple::Byte::TypeKind::I32);
  module.types[1].kind = static_cast<uint8_t>(Simple::Byte::TypeKind::Ref);
  module.types[2].kind = static_cast<uint8_t>(Simple::Byte::TypeKind::String);
  context.args = {Simple::VM::Runtime::PackRef(77), 0};
  context.locals = {Simple::VM::Runtime::PackRef(Simple::VM::HeapLayout::kNullRef),
                    Simple::VM::Runtime::PackRef(88)};
  context.operand_stack = {Simple::VM::Runtime::PackI32(5), Simple::VM::Runtime::PackRef(99)};
  if (!Simple::VM::Jit::PublishJitRootsFromContext(&context, module, {1, 0}, {1, 2}, {0, 1})) {
    return false;
  }
  if (context.root_refs.size() != 3 || context.root_refs[0] != 77 ||
      context.root_refs[1] != 88 || context.root_refs[2] != 99) {
    return false;
  }
  Simple::VM::Jit::ClearJitRoots(&context);
  Simple::VM::Jit::PublishJitRootSlotsByMask(&context, context.locals, 0b10);
  if (context.root_refs.size() != 1 || context.root_refs[0] != 88) return false;
  Simple::VM::Jit::ClearJitRoots(&context);
  Simple::VM::Jit::PublishJitRootSlotsByMask(&context, context.operand_stack, 0b10);
  if (context.root_refs.size() != 1 || context.root_refs[0] != 99) return false;
  Simple::VM::Heap root_heap;
  uint32_t live_ref = Simple::VM::CreateString(root_heap, u"live");
  uint32_t dead_ref = Simple::VM::CreateString(root_heap, u"dead");
  std::vector<uint32_t> published_roots = {live_ref};
  Simple::VM::Jit::PushJitRootFrame(&published_roots);
  root_heap.ResetMarks();
  Simple::VM::Jit::MarkPublishedJitRoots(root_heap);
  root_heap.Sweep();
  Simple::VM::Jit::PopJitRootFrame(&published_roots);
  if (!root_heap.Get(live_ref) || root_heap.Get(dead_ref)) return false;
  Simple::VM::Jit::MarkJitSafepoint(&context, 7, 11, true, false);
  if (!context.safepoint.active || context.safepoint.function_index != 7 || context.safepoint.pc != 11 ||
      !context.safepoint.may_block || context.safepoint.may_allocate) {
    return false;
  }
  Simple::VM::Jit::ClearJitSafepoint(&context);
  return !context.safepoint.active;
}

bool RunJitStatusCodeTest() {
  using Simple::VM::Jit::ClassifyJitReason;
  using Simple::VM::Jit::JitStatusCode;
  using Simple::VM::Jit::JitStatusCodeName;

  return JitStatusCodeName(JitStatusCode::Halt) == std::string("halt") &&
         JitStatusCodeName(JitStatusCode::Return) == std::string("return") &&
         JitStatusCodeName(JitStatusCode::Trap) == std::string("trap") &&
         JitStatusCodeName(JitStatusCode::Fallback) == std::string("fallback") &&
         JitStatusCodeName(JitStatusCode::Unsupported) == std::string("unsupported") &&
         ClassifyJitReason(nullptr) == JitStatusCode::Return &&
         ClassifyJitReason("") == JitStatusCode::Return &&
         ClassifyJitReason("halt") == JitStatusCode::Halt &&
         ClassifyJitReason("trap: division by zero") == JitStatusCode::Trap &&
         ClassifyJitReason("unsupported: intrinsic 4") == JitStatusCode::Unsupported &&
         ClassifyJitReason("unsupported") == JitStatusCode::Unsupported &&
         ClassifyJitReason("LLVM JIT branch stack height mismatch") == JitStatusCode::Fallback;
}

bool RunJitStatusCountsTest() {
  using Simple::VM::Jit::JitStatusCode;

  Simple::VM::ExecResult result;
  result = Simple::VM::Runtime::AttachExecutionStats(
      result,
      {}, {}, {}, {}, {}, {}, {}, {},
      {2, 3},
      {},
      {4, 5, 6},
      {"unsupported: opcode", "LLVM JIT helper failed", "trap: panic"});
  if (result.jit_status_counts.size() != 5) return false;
  return result.jit_status_counts[static_cast<size_t>(JitStatusCode::Return)] == 5 &&
         result.jit_status_counts[static_cast<size_t>(JitStatusCode::Unsupported)] == 4 &&
         result.jit_status_counts[static_cast<size_t>(JitStatusCode::Fallback)] == 5 &&
         result.jit_status_counts[static_cast<size_t>(JitStatusCode::Trap)] == 6 &&
         result.jit_status_counts[static_cast<size_t>(JitStatusCode::Halt)] == 0;
}

bool RunLlvmJitScalarImportCallInsideLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 6);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModule(main_code, 1, "System.OS", "args_count", SigSpec{0, 0, {}}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  Simple::VM::Heap heap;
  Simple::VM::ExecOptions options;
  options.argv.push_back("jit-test");
  if (!backend.TryRunFunctionWithRuntime(load.module, 0, {}, &heap, nullptr, &options, ret, has_ret, error)) {
    std::cerr << "LLVM JIT scalar import loop run failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 6;
}

bool RunLlvmJitScalarImportLoopMatchesInterpreterTest() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 6);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModule(main_code, 1, "System.OS", "args_count", SigSpec{0, 0, {}}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::ExecOptions options;
  options.argv.push_back("jit-test");
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false, options);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true, options);
  if (exec_nojit.status != exec_jit.status || exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "scalar import loop int/jit mismatch: status " << static_cast<int>(exec_nojit.status)
              << " vs " << static_cast<int>(exec_jit.status) << ", exit " << exec_nojit.exit_code
              << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return exec_jit.exit_code == 6;
}

bool RunLlvmJitPreLoopAllocatingImportWithLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 3);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 1, "System.OS", "cwd_get", SigSpec{2, 0, {}},
                                               BuildTypesI32RefString()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  Simple::VM::Heap heap;
  if (!backend.TryRunFunctionWithRuntime(load.module, 0, {}, &heap, nullptr, nullptr, ret, has_ret, error)) {
    std::cerr << "LLVM JIT pre-loop allocating import run failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 3;
}

bool RunLlvmJitDynamicDlScalarCallInsideLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 4);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(main_code, static_cast<int64_t>(reinterpret_cast<intptr_t>(&SimpleVmLlvmTestAddOneI32)));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 1, "System.FFI", "call$test",
                                               SigSpec{0, 2, {3, 0}}, BuildTypesI32RefStringI64()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  Simple::VM::Heap heap;
  if (!backend.TryRunFunctionWithRuntime(load.module, 0, {}, &heap, nullptr, nullptr, ret, has_ret, error)) {
    std::cerr << "LLVM JIT dynamic dl scalar loop run failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 4;
}

bool RunLlvmJitDynamicDlStringArgInsideLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  const uint32_t text_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, "jit"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_offset, &text_const);

  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(main_code, text_const);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 1);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(main_code, static_cast<int64_t>(reinterpret_cast<intptr_t>(&SimpleVmLlvmTestCStringLength)));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 2, "System.FFI", "call$cstring",
                                               SigSpec{0, 2, {3, 2}}, BuildTypesI32RefStringI64(), const_pool));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  Simple::VM::Heap heap;
  if (!backend.TryRunFunctionWithRuntime(load.module, 0, {}, &heap, nullptr, nullptr, ret, has_ret, error)) {
    std::cerr << "LLVM JIT dynamic dl string loop run failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 3;
}

bool RunLlvmJitDynamicDlScalarLoopMatchesInterpreterTest() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 4);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(main_code, static_cast<int64_t>(reinterpret_cast<intptr_t>(&SimpleVmLlvmTestAddOneI32)));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 1, "System.FFI", "call$test",
                                               SigSpec{0, 2, {3, 0}}, BuildTypesI32RefStringI64()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::ExecResult exec_nojit = Simple::VM::ExecuteModule(load.module, true, false);
  Simple::VM::ExecResult exec_jit = Simple::VM::ExecuteModule(load.module, true, true);
  if (exec_nojit.status != exec_jit.status || exec_nojit.exit_code != exec_jit.exit_code) {
    std::cerr << "dynamic dl scalar loop int/jit mismatch: status " << static_cast<int>(exec_nojit.status)
              << " vs " << static_cast<int>(exec_jit.status) << ", exit " << exec_nojit.exit_code
              << " vs " << exec_jit.exit_code << "\n";
    return false;
  }
  return exec_jit.exit_code == 4;
}

bool RunLlvmJitDynamicDlContextHelperInsideLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 4);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(main_code, static_cast<int64_t>(reinterpret_cast<intptr_t>(&SimpleVmLlvmTestAddOneI64)));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConvI32ToI64));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConvI64ToI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 1, "System.FFI", "call$i64",
                                               SigSpec{3, 2, {3, 3}}, BuildTypesI32RefStringI64()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  Simple::VM::Heap heap;
  if (!backend.TryRunFunctionWithRuntime(load.module, 0, {}, &heap, nullptr, nullptr, ret, has_ret, error)) {
    std::cerr << "LLVM JIT dynamic dl context helper loop run failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 4;
}

bool RunLlvmJitDynamicDlManagedSignatureDiagnosticTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 1, "System.FFI", "call$managed",
                                               SigSpec{0, 2, {3, 1}}, BuildTypesI32RefStringI64()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "expected dynamic dl managed signature loop rejection\n";
    return false;
  }
  return error.find("category=dynamic-dl/external-c") != std::string::npos &&
         error.find("reason=non-scalar-or-managed-signature") != std::string::npos &&
         error.find("target=System.FFI.call$managed") != std::string::npos;
}

bool RunLlvmJitManagedArgImportCallInsideLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  const uint32_t path_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, "."));
  uint32_t path_const = 0;
  AppendConstString(const_pool, path_offset, &path_const);

  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(main_code, path_const);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 1);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 2, "System.Path", "exists",
                                               SigSpec{0, 1, {2}}, BuildTypesI32RefString(), const_pool));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  Simple::VM::Heap heap;
  Simple::VM::ExecOptions options;
  if (!backend.TryRunFunctionWithRuntime(load.module, 0, {}, &heap, nullptr, &options, ret, has_ret, error)) {
    std::cerr << "LLVM JIT managed-arg import loop run failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 1;
}

bool RunLlvmJitResourceInputImportCallInsideLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  const uint32_t sym_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, "missing_symbol"));
  uint32_t sym_const = 0;
  AppendConstString(const_pool, sym_offset, &sym_const);

  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(main_code, sym_const);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 1);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI64));
  AppendI64(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 2);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModuleWithTypes(main_code, 2, "System.FFI", "sym",
                                               SigSpec{3, 2, {3, 2}}, BuildTypesI32RefStringI64(), const_pool));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  Simple::VM::Heap heap;
  Simple::VM::ExecOptions options;
  if (!backend.TryRunFunctionWithRuntime(load.module, 0, {}, &heap, nullptr, &options, ret, has_ret, error)) {
    std::cerr << "LLVM JIT resource-input import loop run failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 1;
}

bool RunLlvmJitUnsafeImportCallInsideLoopRejectsTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallImport));
  AppendU32(main_code, 1);
  AppendU8(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildSingleImportFunctionModule(main_code, 1, "System.OS", "sleepMs", SigSpec{1, 1, {0}}));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "expected unsafe import loop rejection\n";
    return false;
  }
  return error.find("unsupported: import/indirect call inside loop needs LLVM state merge/runtime ABI") !=
             std::string::npos &&
         error.find("op=CallImport") != std::string::npos && error.find("pc=") != std::string::npos &&
         error.find("category=native-registry") != std::string::npos &&
         error.find("reason=blocking-call") != std::string::npos &&
         error.find("target=System.OS.sleepMs") != std::string::npos;
}

bool RunLlvmJitIndirectCallInsideLoopRejectsTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(main_code, 0);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(callee, 7);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSigsWithTables({main_code, callee}, {1, 0}, {0, 0},
                                                {SigSpec{0, 0, {}}}, {}, BuildTypesI32Void()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "expected unsafe indirect loop rejection\n";
    return false;
  }
  return error.find("unsupported: import/indirect call inside loop needs LLVM state merge/runtime ABI") !=
             std::string::npos &&
         error.find("op=CallIndirect") != std::string::npos && error.find("pc=") != std::string::npos &&
         error.find("category=indirect/procedure") != std::string::npos &&
         error.find("reason=unknown-target-effects") != std::string::npos;
}

bool RunLlvmJitDirectUnspecifiedVoidCallInsideLoopTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 3);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstF32));
  AppendF32(main_code, 1.0f);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(main_code, 1);
  AppendU8(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 1);
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSigsWithTables({main_code, callee}, {1, 1}, {0, 1},
                                                {SigSpec{1, 0, {}}, SigSpec{0, 1, {2}}},
                                                {}, BuildTypesUnspecifiedI32F32()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "direct unspecified-void loop call failed: " << error << "\n";
    return false;
  }
  return has_ret && Simple::VM::Runtime::UnpackI32(ret) == 3;
}

bool RunLlvmJitDirectRefCallInsideLoopRejectsTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

  using Simple::Byte::OpCode;
  std::vector<uint8_t> main_code;
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  size_t loop_start = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = main_code.size();
  AppendI32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Call));
  AppendU32(main_code, 1);
  AppendU8(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Pop));
  AppendU8(main_code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(main_code, 1);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = main_code.size();
  AppendI32(main_code, 0);
  size_t loop_end = main_code.size();
  AppendU8(main_code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(main_code, 0);
  AppendU8(main_code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(main_code, exit_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(main_code, back_jmp,
           static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  std::vector<uint8_t> callee;
  AppendU8(callee, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(callee, 0);
  AppendU8(callee, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(callee, static_cast<uint8_t>(OpCode::Ret));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(
      BuildModuleWithFunctionsAndSigsWithTables({main_code, callee}, {1, 0}, {0, 1},
                                                {SigSpec{0, 0, {}}, SigSpec{1, 0, {}}},
                                                {}, BuildTypesI32RefString()));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    std::cerr << "expected unsafe direct ref loop rejection\n";
    return false;
  }
  return error.find("unsupported: import/indirect call inside loop needs LLVM state merge/runtime ABI") !=
             std::string::npos &&
         error.find("op=Call") != std::string::npos && error.find("pc=") != std::string::npos &&
         error.find("category=direct-simple") != std::string::npos &&
         error.find("reason=non-scalar-or-managed-signature") != std::string::npos;
}

bool RunLlvmJitLoopSmokeTest() {
  Simple::VM::Jit::LlvmJitBackend backend;
  if (!backend.Status().available) return true;

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
  AppendI32(code, 6);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t exit_jmp = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  size_t back_jmp = code.size();
  AppendI32(code, 0);
  size_t loop_end = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  WriteU32(code, exit_jmp, static_cast<uint32_t>(static_cast<int32_t>(loop_end) - static_cast<int32_t>(exit_jmp + 4)));
  WriteU32(code, back_jmp, static_cast<uint32_t>(static_cast<int32_t>(loop_start) - static_cast<int32_t>(back_jmp + 4)));

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(BuildModule(code, 1, 2));
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  Simple::VM::Interpreter::Slot ret = 0;
  bool has_ret = false;
  std::string error;
  if (!backend.TryRunFunction(load.module, 0, {}, ret, has_ret, error)) {
    return error == "unsupported";
  }
  if (!has_ret || Simple::VM::Runtime::UnpackI32(ret) != 7) {
    std::cerr << "expected LLVM JIT loop return 7\n";
    return false;
  }
  return true;
}

static const TestCase kJitTests[] = {
  {"llvm_jit_cache_key_includes_abi_versions", RunLlvmJitCacheKeyIncludesAbiVersionsTest},
  {"jit_call_context_helpers", RunJitCallContextHelpersTest},
  {"jit_status_codes", RunJitStatusCodeTest},
  {"jit_status_counts", RunJitStatusCountsTest},
  {"llvm_jit_leaf_i32_smoke", RunLlvmJitLeafI32SmokeTest},
  {"llvm_jit_locals_params_smoke", RunLlvmJitLocalsAndParamsSmokeTest},
  {"llvm_jit_cache_smoke", RunLlvmJitCacheSmokeTest},
  {"llvm_jit_float_smoke", RunLlvmJitFloatSmokeTest},
  {"llvm_jit_global_smoke", RunLlvmJitGlobalSmokeTest},
  {"llvm_jit_initialized_global_smoke", RunLlvmJitInitializedGlobalSmokeTest},
  {"llvm_jit_unsigned_smoke", RunLlvmJitUnsignedSmokeTest},
  {"llvm_jit_checked_i32_smoke", RunLlvmJitCheckedI32SmokeTest},
  {"llvm_jit_checked_u32_smoke", RunLlvmJitCheckedU32SmokeTest},
  {"llvm_jit_checked_64_smoke", RunLlvmJitChecked64SmokeTest},
  {"llvm_jit_checked_conv_smoke", RunLlvmJitCheckedConvSmokeTest},
  {"llvm_jit_checked_float_conv_smoke", RunLlvmJitCheckedFloatConvSmokeTest},
  {"llvm_jit_checked_bounds_smoke", RunLlvmJitCheckedBoundsSmokeTest},
  {"llvm_jit_guard_bounds_smoke", RunLlvmJitGuardBoundsSmokeTest},
  {"llvm_jit_pointer_ops_smoke", RunLlvmJitPointerOpsSmokeTest},
  {"llvm_jit_memory_pseudo_ops_smoke", RunLlvmJitMemoryPseudoOpsSmokeTest},
  {"llvm_jit_const128_smoke", RunLlvmJitConst128SmokeTest},
  {"llvm_jit_trap_fallback_smoke", RunLlvmJitTrapFallbackSmokeTest},
  {"llvm_jit_syscall_fallback_smoke", RunLlvmJitSysCallFallbackSmokeTest},
  {"llvm_jit_address_task_ops_smoke", RunLlvmJitAddressTaskOpsSmokeTest},
  {"llvm_jit_pseudo_extended_ops_smoke", RunLlvmJitPseudoExtendedOpsSmokeTest},
  {"llvm_jit_self_call_smoke", RunLlvmJitSelfCallSmokeTest},
  {"llvm_jit_direct_call_helper_smoke", RunLlvmJitDirectCallHelperSmokeTest},
  {"llvm_jit_call_import_helper_smoke", RunLlvmJitCallImportHelperSmokeTest},
  {"llvm_jit_call_native_helper_smoke", RunLlvmJitCallNativeHelperSmokeTest},
  {"llvm_jit_call_indirect_helper_smoke", RunLlvmJitCallIndirectHelperSmokeTest},
  {"llvm_jit_no_capture_closure_smoke", RunLlvmJitNoCaptureClosureSmokeTest},
  {"llvm_jit_void_call_helper_smoke", RunLlvmJitVoidCallHelperSmokeTest},
  {"llvm_jit_direct_tail_call_helper_smoke", RunLlvmJitDirectTailCallHelperSmokeTest},
  {"llvm_jit_intrinsic_smoke", RunLlvmJitIntrinsicSmokeTest},
  {"llvm_jit_yield_smoke", RunLlvmJitYieldSmokeTest},
  {"llvm_jit_halt_smoke", RunLlvmJitHaltSmokeTest},
  {"llvm_jit_jmptable_smoke", RunLlvmJitJmpTableSmokeTest},
  {"llvm_jit_sqrt_intrinsic_smoke", RunLlvmJitSqrtIntrinsicSmokeTest},
  {"llvm_jit_self_tail_call_smoke", RunLlvmJitSelfTailCallSmokeTest},
  {"llvm_jit_null_ref_smoke", RunLlvmJitNullRefSmokeTest},
  {"llvm_jit_narrow_integer_smoke", RunLlvmJitNarrowIntegerSmokeTest},
  {"llvm_jit_bitops_conversions_smoke", RunLlvmJitBitopsConversionsSmokeTest},
  {"llvm_jit_i64_smoke", RunLlvmJitI64SmokeTest},
  {"llvm_jit_compare_bool_smoke", RunLlvmJitCompareBoolSmokeTest},
  {"llvm_jit_forward_branch_smoke", RunLlvmJitForwardBranchSmokeTest},
  {"llvm_jit_scalar_import_call_inside_loop", RunLlvmJitScalarImportCallInsideLoopTest},
  {"llvm_jit_scalar_import_loop_matches_interpreter", RunLlvmJitScalarImportLoopMatchesInterpreterTest},
  {"llvm_jit_pre_loop_allocating_import_with_loop", RunLlvmJitPreLoopAllocatingImportWithLoopTest},
  {"llvm_jit_dynamic_dl_scalar_call_inside_loop", RunLlvmJitDynamicDlScalarCallInsideLoopTest},
  {"llvm_jit_dynamic_dl_context_helper_inside_loop", RunLlvmJitDynamicDlContextHelperInsideLoopTest},
  {"llvm_jit_dynamic_dl_scalar_loop_matches_interpreter", RunLlvmJitDynamicDlScalarLoopMatchesInterpreterTest},
  {"llvm_jit_dynamic_dl_string_arg_inside_loop", RunLlvmJitDynamicDlStringArgInsideLoopTest},
  {"llvm_jit_dynamic_dl_managed_signature_diagnostic", RunLlvmJitDynamicDlManagedSignatureDiagnosticTest},
  {"llvm_jit_managed_arg_import_call_inside_loop", RunLlvmJitManagedArgImportCallInsideLoopTest},
  {"llvm_jit_resource_input_import_call_inside_loop", RunLlvmJitResourceInputImportCallInsideLoopTest},
  {"llvm_jit_unsafe_import_call_inside_loop_rejects", RunLlvmJitUnsafeImportCallInsideLoopRejectsTest},
  {"llvm_jit_indirect_call_inside_loop_rejects", RunLlvmJitIndirectCallInsideLoopRejectsTest},
  {"llvm_jit_direct_unspecified_void_call_inside_loop", RunLlvmJitDirectUnspecifiedVoidCallInsideLoopTest},
  {"llvm_jit_direct_ref_call_inside_loop_rejects", RunLlvmJitDirectRefCallInsideLoopRejectsTest},
  {"llvm_jit_loop_smoke", RunLlvmJitLoopSmokeTest},
  {"jit_disabled", RunJitDisabledTest},
};

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
