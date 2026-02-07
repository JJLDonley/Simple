#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "intrinsic_ids.h"
#include "ir_builder.h"
#include "ir_compiler.h"
#include "ir_lang.h"
#include "opcode.h"
#include "sbc_emitter.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "test_utils.h"
#include "vm.h"

namespace Simple::VM::Tests {

using Simple::Byte::sbc::AppendConstString;
using Simple::Byte::sbc::AppendStringToPool;
using Simple::Byte::sbc::AppendU8;
using Simple::Byte::sbc::AppendU16;
using Simple::Byte::sbc::AppendU32;
using Simple::Byte::sbc::BuildModule;
using Simple::Byte::sbc::BuildModuleWithFunctionsAndSigs;
using Simple::Byte::sbc::BuildModuleWithTables;

std::vector<uint8_t> BuildJmpTableModule(int32_t index);

std::vector<uint8_t> BuildIrTextModule(const std::string& text, const char* name) {
  Simple::IR::Text::IrTextModule parsed;
  std::string error;
  if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
    std::cerr << "IR text parse failed (" << name << "): " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
    std::cerr << "IR text lower failed (" << name << "): " << error << "\n";
    return {};
  }
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed (" << name << "): " << error << "\n";
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrTextModuleWithTables(const std::string& text,
                                                 const char* name,
                                                 std::vector<uint8_t> types,
                                                 std::vector<uint8_t> fields,
                                                 std::vector<uint8_t> const_pool) {
  Simple::IR::Text::IrTextModule parsed;
  std::string error;
  if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
    std::cerr << "IR text parse failed (" << name << "): " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
    std::cerr << "IR text lower failed (" << name << "): " << error << "\n";
    return {};
  }
  module.types_bytes = std::move(types);
  module.fields_bytes = std::move(fields);
  module.const_pool = std::move(const_pool);
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed (" << name << "): " << error << "\n";
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrTextModuleWithTablesAndGlobals(const std::string& text,
                                                           const char* name,
                                                           std::vector<uint8_t> types,
                                                           std::vector<uint8_t> fields,
                                                           std::vector<uint8_t> const_pool,
                                                           std::vector<uint8_t> globals) {
  Simple::IR::Text::IrTextModule parsed;
  std::string error;
  if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
    std::cerr << "IR text parse failed (" << name << "): " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
    std::cerr << "IR text lower failed (" << name << "): " << error << "\n";
    return {};
  }
  module.types_bytes = std::move(types);
  module.fields_bytes = std::move(fields);
  module.const_pool = std::move(const_pool);
  module.globals_bytes = std::move(globals);
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed (" << name << "): " << error << "\n";
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrTextModuleWithSigs(const std::string& text,
                                               const char* name,
                                               std::vector<Simple::Byte::sbc::SigSpec> sig_specs,
                                               bool log_errors = true) {
  Simple::IR::Text::IrTextModule parsed;
  std::string error;
  if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
    if (log_errors) {
      std::cerr << "IR text parse failed (" << name << "): " << error << "\n";
    }
    return {};
  }
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
    if (log_errors) {
      std::cerr << "IR text lower failed (" << name << "): " << error << "\n";
    }
    return {};
  }
  module.sig_specs = std::move(sig_specs);
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    if (log_errors) {
      std::cerr << "IR compile failed (" << name << "): " << error << "\n";
    }
    return {};
  }
  return out;
}

bool RunIrTextExpectFail(const char* text, const char* name) {
  Simple::IR::Text::IrTextModule parsed;
  std::string error;
  if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
    return true;
  }
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
    return true;
  }
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    return true;
  }
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(out);
  if (!load.ok) {
    return true;
  }
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
  if (!vr.ok) {
    return true;
  }
  std::cerr << "expected IR text failure: " << name << "\n";
  return false;
}

std::vector<uint8_t> BuildIrAddModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(7);
  builder.EmitConstI32(5);
  builder.EmitOp(Simple::Byte::OpCode::AddI32);
  builder.EmitOp(Simple::Byte::OpCode::Ret);
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_add_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrJumpModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel skip = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitJmp(skip);
  builder.EmitConstI32(99);
  builder.EmitOp(Simple::Byte::OpCode::Pop);
  builder.BindLabel(skip, nullptr);
  builder.EmitConstI32(7);
  builder.EmitOp(Simple::Byte::OpCode::Ret);
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_jmp_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrJmpTableModule(int32_t index) {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel case0 = builder.CreateLabel();
  Simple::IR::IrLabel case1 = builder.CreateLabel();
  Simple::IR::IrLabel def = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstI32(index);
  builder.EmitJmpTable({case0, case1}, def);
  builder.BindLabel(case0, nullptr);
  builder.EmitConstI32(1);
  builder.EmitRet();
  builder.BindLabel(case1, nullptr);
  builder.EmitConstI32(2);
  builder.EmitRet();
  builder.BindLabel(def, nullptr);
  builder.EmitConstI32(3);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  module.const_pool = builder.const_pool();
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildJmpTableModule(index);
  if (!ExpectSbcEqual(out, expected, "ir_jmp_table_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrStackOps2Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(1);
  builder.EmitConstI32(2);
  builder.EmitConstI32(3);
  builder.EmitRot();
  builder.EmitSwap();
  builder.EmitDup2();
  builder.EmitOp(Simple::Byte::OpCode::AddI32);
  builder.EmitOp(Simple::Byte::OpCode::AddI32);
  builder.EmitOp(Simple::Byte::OpCode::AddI32);
  builder.EmitOp(Simple::Byte::OpCode::AddI32);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_stack_ops2_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrI64BitwiseModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI64(6);
  builder.EmitConstI64(3);
  builder.EmitAndI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_i64_bitwise_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrConstSmallModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI8(-5);
  builder.EmitConstU16(10);
  builder.EmitAddI32();
  builder.EmitConstChar(65);
  builder.EmitAddI32();
  builder.EmitConstU32(2);
  builder.EmitAddI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_const_small_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrIncDecNegModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(4);
  builder.EmitIncI32();
  builder.EmitDecI32();
  builder.EmitNegI32();
  builder.EmitConstI32(10);
  builder.EmitAddI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_inc_dec_neg_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrIncDecNegWideModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU32(1);
  builder.EmitIncU32();
  builder.EmitDecU32();
  builder.EmitPop();
  builder.EmitConstU64(0);
  builder.EmitNegU64();
  builder.EmitPop();
  builder.EmitConstF32(1.5f);
  builder.EmitIncF32();
  builder.EmitPop();
  builder.EmitConstF64(2.5);
  builder.EmitDecF64();
  builder.EmitPop();
  builder.EmitConstI8(-3);
  builder.EmitNegI8();
  builder.EmitPop();
  builder.EmitConstU16(2);
  builder.EmitDecU16();
  builder.EmitPop();
  builder.EmitConstI32(6);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_inc_dec_neg_wide_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListInsertRemoveI64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitNewList(0, 4);
  builder.EmitDup();
  builder.EmitConstI32(0);
  builder.EmitConstI64(9);
  builder.EmitListInsertI64();
  builder.EmitDup();
  builder.EmitConstI32(1);
  builder.EmitConstI64(4);
  builder.EmitListInsertI64();
  builder.EmitConstI32(0);
  builder.EmitListRemoveI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_list_insert_remove_i64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU32ArithModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU32(7);
  builder.EmitConstU32(5);
  builder.EmitSubU32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u32_arith_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrCmpVariantsModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel is_true = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstI32(3);
  builder.EmitConstI32(3);
  builder.EmitCmpNeI32();
  builder.EmitBoolNot();
  builder.EmitConstI32(3);
  builder.EmitConstI32(2);
  builder.EmitCmpGtI32();
  builder.EmitBoolAnd();
  builder.EmitJmpTrue(is_true);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(is_true, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_cmp_variants_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64ArithModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU64(10);
  builder.EmitConstU64(4);
  builder.EmitSubU64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_arith_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF64CmpModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel is_true = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstF64(3.0);
  builder.EmitConstF64(2.0);
  builder.EmitCmpGtF64();
  builder.EmitJmpTrue(is_true);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(is_true, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f64_cmp_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64CmpModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel is_true = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstU64(5);
  builder.EmitConstU64(7);
  builder.EmitCmpLtU64();
  builder.EmitJmpTrue(is_true);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(is_true, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_cmp_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF32ArithModule2() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF32(6.0f);
  builder.EmitConstF32(2.0f);
  builder.EmitDivF32();
  builder.EmitConvF32ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f32_arith_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF64ArithModule2() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF64(9.0);
  builder.EmitConstF64(3.0);
  builder.EmitMulF64();
  builder.EmitConvF64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f64_arith_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU32ArithModule2() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU32(3);
  builder.EmitConstU32(4);
  builder.EmitMulU32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u32_arith_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64ArithModule2() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU64(20);
  builder.EmitConstU64(5);
  builder.EmitDivU64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_arith_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU32CmpModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel is_true = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstU32(1);
  builder.EmitConstU32(2);
  builder.EmitCmpLtU32();
  builder.EmitJmpTrue(is_true);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(is_true, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u32_cmp_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64CmpModule2() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel is_true = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstU64(5);
  builder.EmitConstU64(5);
  builder.EmitCmpGeU64();
  builder.EmitJmpTrue(is_true);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(is_true, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_cmp_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF32CmpModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel is_true = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstF32(2.0f);
  builder.EmitConstF32(2.0f);
  builder.EmitCmpEqF32();
  builder.EmitJmpTrue(is_true);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(is_true, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f32_cmp_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF64CmpModule2() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel is_true = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstF64(1.0);
  builder.EmitConstF64(2.0);
  builder.EmitCmpLtF64();
  builder.EmitJmpTrue(is_true);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(is_true, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f64_cmp_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrI64ArithModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI64(8);
  builder.EmitConstI64(3);
  builder.EmitModI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_i64_arith_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU32ModModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU32(10);
  builder.EmitConstU32(6);
  builder.EmitModU32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u32_mod_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64ModModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU64(10);
  builder.EmitConstU64(6);
  builder.EmitModU64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_mod_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrI64MulModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI64(3);
  builder.EmitConstI64(4);
  builder.EmitMulI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_i64_mul_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrI64DivModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI64(9);
  builder.EmitConstI64(3);
  builder.EmitDivI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_i64_div_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU32ArithModule3() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU32(2);
  builder.EmitConstU32(3);
  builder.EmitAddU32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u32_arith_module3")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64DivModule2() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU64(12);
  builder.EmitConstU64(3);
  builder.EmitDivU64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_div_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU32DivModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU32(8);
  builder.EmitConstU32(2);
  builder.EmitDivU32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u32_div_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64AddModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU64(3);
  builder.EmitConstU64(2);
  builder.EmitAddU64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_add_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF32SubModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF32(5.0f);
  builder.EmitConstF32(2.0f);
  builder.EmitSubF32();
  builder.EmitConvF32ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f32_sub_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF64SubModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF64(7.0);
  builder.EmitConstF64(4.0);
  builder.EmitSubF64();
  builder.EmitConvF64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f64_sub_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU32MulModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU32(4);
  builder.EmitConstU32(3);
  builder.EmitMulU32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u32_mul_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrU64SubModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstU64(9);
  builder.EmitConstU64(4);
  builder.EmitSubU64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_u64_sub_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF32MulModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF32(3.0f);
  builder.EmitConstF32(4.0f);
  builder.EmitMulF32();
  builder.EmitConvF32ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f32_mul_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF64DivModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF64(8.0);
  builder.EmitConstF64(2.0);
  builder.EmitDivF64();
  builder.EmitConvF64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f64_div_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrI32ArithModule2() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(20);
  builder.EmitConstI32(3);
  builder.EmitModI32();
  builder.EmitConstI32(5);
  builder.EmitMulI32();
  builder.EmitConstI32(4);
  builder.EmitSubI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_i32_arith_module2")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrI64AddSubModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI64(10);
  builder.EmitConstI64(4);
  builder.EmitSubI64();
  builder.EmitConstI64(2);
  builder.EmitAddI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_i64_add_sub_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrLocalsModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitConstI32(9);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_locals_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrCallModule() {
  Simple::IR::IrBuilder entry_builder;
  entry_builder.EmitEnter(0);
  entry_builder.EmitCall(1, 0);
  entry_builder.EmitRet();
  std::vector<uint8_t> entry;
  std::string error;
  if (!entry_builder.Finish(&entry, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrBuilder callee_builder;
  callee_builder.EmitEnter(0);
  callee_builder.EmitConstI32(7);
  callee_builder.EmitRet();
  std::vector<uint8_t> callee;
  if (!callee_builder.Finish(&callee, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrModule module;
  Simple::IR::IrFunction entry_func;
  entry_func.code = entry;
  entry_func.local_count = 0;
  entry_func.stack_max = 12;
  module.functions.push_back(std::move(entry_func));
  Simple::IR::IrFunction callee_func;
  callee_func.code = callee;
  callee_func.local_count = 0;
  callee_func.stack_max = 12;
  module.functions.push_back(std::move(callee_func));
  module.entry_method_id = 0;

  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }

  std::vector<uint32_t> sig_ids = {0, 0};
  Simple::Byte::sbc::SigSpec sig_spec;
  sig_spec.ret_type_id = 0;
  sig_spec.param_count = 0;
  std::vector<uint8_t> expected = BuildModuleWithFunctionsAndSigs({entry, callee}, {0, 0}, sig_ids, {sig_spec});
  if (!ExpectSbcEqual(out, expected, "ir_call_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrCallCheckModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitCallCheck();
  builder.EmitConstI32(0);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_callcheck_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrIntrinsicModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitIntrinsic(Simple::VM::kIntrinsicBreakpoint);
  builder.EmitConstI32(0);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_intrinsic_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrSysCallModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitSysCall(7);
  builder.EmitConstI32(0);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_syscall_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrUpvalueModule() {
  Simple::IR::IrBuilder entry_builder;
  entry_builder.EmitEnter(0);
  entry_builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  entry_builder.EmitNewClosure(1, 1);
  entry_builder.EmitCallIndirect(0, 0);
  entry_builder.EmitRet();
  std::vector<uint8_t> entry;
  std::string error;
  if (!entry_builder.Finish(&entry, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrBuilder callee_builder;
  callee_builder.EmitEnter(0);
  callee_builder.EmitLoadUpvalue(0);
  callee_builder.EmitPop();
  callee_builder.EmitConstI32(1);
  callee_builder.EmitRet();
  std::vector<uint8_t> callee;
  if (!callee_builder.Finish(&callee, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrModule module;
  Simple::IR::IrFunction entry_func;
  entry_func.code = entry;
  entry_func.local_count = 0;
  entry_func.stack_max = 12;
  module.functions.push_back(std::move(entry_func));
  Simple::IR::IrFunction callee_func;
  callee_func.code = callee;
  callee_func.local_count = 0;
  callee_func.stack_max = 12;
  module.functions.push_back(std::move(callee_func));
  module.entry_method_id = 0;
  Simple::Byte::sbc::SigSpec sig_spec;
  sig_spec.ret_type_id = 0;
  sig_spec.param_count = 0;
  module.sig_specs = {sig_spec};
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint32_t> sig_ids = {0, 0};
  std::vector<uint8_t> expected = BuildModuleWithFunctionsAndSigs({entry, callee}, {0, 0}, sig_ids,
                                                                   {sig_spec});
  if (!ExpectSbcEqual(out, expected, "ir_upvalue_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrNewClosureModule() {
  Simple::IR::IrBuilder entry_builder;
  entry_builder.EmitEnter(0);
  entry_builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  entry_builder.EmitNewClosure(1, 1);
  entry_builder.EmitPop();
  entry_builder.EmitConstI32(0);
  entry_builder.EmitRet();
  std::vector<uint8_t> entry;
  std::string error;
  if (!entry_builder.Finish(&entry, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrBuilder callee_builder;
  callee_builder.EmitEnter(0);
  callee_builder.EmitConstI32(7);
  callee_builder.EmitRet();
  std::vector<uint8_t> callee;
  if (!callee_builder.Finish(&callee, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrModule module;
  Simple::IR::IrFunction entry_func;
  entry_func.code = entry;
  entry_func.local_count = 0;
  entry_func.stack_max = 12;
  module.functions.push_back(std::move(entry_func));
  Simple::IR::IrFunction callee_func;
  callee_func.code = callee;
  callee_func.local_count = 0;
  callee_func.stack_max = 12;
  module.functions.push_back(std::move(callee_func));
  module.entry_method_id = 0;

  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }

  std::vector<uint32_t> sig_ids = {0, 0};
  Simple::Byte::sbc::SigSpec sig_spec;
  sig_spec.ret_type_id = 0;
  sig_spec.param_count = 0;
  std::vector<uint8_t> expected = BuildModuleWithFunctionsAndSigs({entry, callee}, {0, 0}, sig_ids, {sig_spec});
  if (!ExpectSbcEqual(out, expected, "ir_new_closure_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrGlobalsModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(11);
  builder.EmitStoreGlobal(0);
  builder.EmitLoadGlobal(0);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;

  std::vector<uint8_t> globals;
  AppendU32(globals, 0);            // name_str
  AppendU32(globals, 0);            // type_id
  AppendU32(globals, 1);            // flags (mutable)
  AppendU32(globals, 0xFFFFFFFFu);  // init_const_id
  module.globals_bytes = globals;

  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 1, 0);
  if (!ExpectSbcEqual(out, expected, "ir_globals_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrStackOpsModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(3);
  builder.EmitDup();
  builder.EmitOp(Simple::Byte::OpCode::AddI32);
  builder.EmitPop();
  builder.EmitConstI32(5);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_stack_ops_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrBranchModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel taken = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstBool(true);
  builder.EmitJmpTrue(taken);
  builder.EmitConstI32(1);
  builder.EmitJmp(done);
  builder.BindLabel(taken, nullptr);
  builder.EmitConstI32(9);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_branch_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrCompareModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel ok = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstI32(7);
  builder.EmitConstI32(7);
  builder.EmitCmpEqI32();
  builder.EmitConstI32(3);
  builder.EmitConstI32(9);
  builder.EmitCmpLtI32();
  builder.EmitBoolAnd();
  builder.EmitJmpTrue(ok);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(ok, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_compare_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrBoolModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel ok = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitConstBool(false);
  builder.EmitBoolNot();
  builder.EmitConstBool(true);
  builder.EmitBoolOr();
  builder.EmitJmpTrue(ok);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(ok, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_bool_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrConvI32ToI64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(4);
  builder.EmitConvI32ToI64();
  builder.EmitConstI64(5);
  builder.EmitAddI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_conv_i32_i64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrConvI32ToF64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(3);
  builder.EmitConvI32ToF64();
  builder.EmitConstF64(4.0);
  builder.EmitOp(Simple::Byte::OpCode::AddF64);
  builder.EmitConvF64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_conv_i32_f64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrConvF32F64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF32(6.0f);
  builder.EmitConvF32ToF64();
  builder.EmitConstF64(1.0);
  builder.EmitAddF64();
  builder.EmitConvF64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_conv_f32_f64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrF32ArithModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstF32(2.0f);
  builder.EmitConstF32(5.0f);
  builder.EmitAddF32();
  builder.EmitConvF32ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_f32_arith_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrBitwiseI32Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstI32(0xF0);
  builder.EmitConstI32(0x0F);
  builder.EmitAndI32();
  builder.EmitConstI32(0x0F);
  builder.EmitOrI32();
  builder.EmitConstI32(0x0A);
  builder.EmitXorI32();
  builder.EmitConstI32(1);
  builder.EmitShlI32();
  builder.EmitConstI32(2);
  builder.EmitShrI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_bitwise_i32_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrCallIndirectModule() {
  Simple::IR::IrBuilder entry_builder;
  entry_builder.EmitEnter(0);
  entry_builder.EmitConstI32(1);
  entry_builder.EmitCallIndirect(0, 0);
  entry_builder.EmitRet();
  std::vector<uint8_t> entry;
  std::string error;
  if (!entry_builder.Finish(&entry, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrBuilder callee_builder;
  callee_builder.EmitEnter(0);
  callee_builder.EmitConstI32(9);
  callee_builder.EmitRet();
  std::vector<uint8_t> callee;
  if (!callee_builder.Finish(&callee, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrModule module;
  Simple::IR::IrFunction entry_func;
  entry_func.code = entry;
  entry_func.local_count = 0;
  entry_func.stack_max = 12;
  module.functions.push_back(std::move(entry_func));
  Simple::IR::IrFunction callee_func;
  callee_func.code = callee;
  callee_func.local_count = 0;
  callee_func.stack_max = 12;
  module.functions.push_back(std::move(callee_func));
  module.entry_method_id = 0;

  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }

  std::vector<uint32_t> sig_ids = {0, 0};
  Simple::Byte::sbc::SigSpec sig_spec;
  sig_spec.ret_type_id = 0;
  sig_spec.param_count = 0;
  std::vector<uint8_t> expected = BuildModuleWithFunctionsAndSigs({entry, callee}, {0, 0}, sig_ids, {sig_spec});
  if (!ExpectSbcEqual(out, expected, "ir_call_indirect_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrTailCallModule() {
  Simple::IR::IrBuilder entry_builder;
  entry_builder.EmitEnter(0);
  entry_builder.EmitTailCall(1, 0);
  std::vector<uint8_t> entry;
  std::string error;
  if (!entry_builder.Finish(&entry, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrBuilder callee_builder;
  callee_builder.EmitEnter(0);
  callee_builder.EmitConstI32(42);
  callee_builder.EmitRet();
  std::vector<uint8_t> callee;
  if (!callee_builder.Finish(&callee, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrModule module;
  Simple::IR::IrFunction entry_func;
  entry_func.code = entry;
  entry_func.local_count = 0;
  entry_func.stack_max = 12;
  module.functions.push_back(std::move(entry_func));
  Simple::IR::IrFunction callee_func;
  callee_func.code = callee;
  callee_func.local_count = 0;
  callee_func.stack_max = 12;
  module.functions.push_back(std::move(callee_func));
  module.entry_method_id = 0;

  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }

  std::vector<uint32_t> sig_ids = {0, 0};
  Simple::Byte::sbc::SigSpec sig_spec;
  sig_spec.ret_type_id = 0;
  sig_spec.param_count = 0;
  std::vector<uint8_t> expected = BuildModuleWithFunctionsAndSigs({entry, callee}, {0, 0}, sig_ids, {sig_spec});
  if (!ExpectSbcEqual(out, expected, "ir_tailcall_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 3);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitConstI32(7);
  builder.EmitArraySetI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitArrayGetI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(5);
  builder.EmitListPushI32();
  builder.EmitLoadLocal(0);
  builder.EmitListPopI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrStringModule() {
  std::vector<uint8_t> const_pool;
  uint32_t str0 = static_cast<uint32_t>(AppendStringToPool(const_pool, "a"));
  uint32_t str1 = static_cast<uint32_t>(AppendStringToPool(const_pool, "bc"));
  uint32_t id0 = 0;
  uint32_t id1 = 0;
  AppendConstString(const_pool, str0, &id0);
  AppendConstString(const_pool, str1, &id1);

  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstString(id0);
  builder.EmitConstString(id1);
  builder.EmitStringConcat();
  builder.EmitStringLen();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  module.const_pool = const_pool;

  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModuleWithTables(code, const_pool, {}, {}, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_string_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrStringGetCharModule() {
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "ABC"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstString(text_const);
  builder.EmitConstI32(1);
  builder.EmitStringGetChar();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  module.const_pool = const_pool;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModuleWithTables(code, const_pool, {}, {}, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_string_get_char_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrStringSliceModule() {
  std::vector<uint8_t> const_pool;
  uint32_t text_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hello"));
  uint32_t text_const = 0;
  AppendConstString(const_pool, text_off, &text_const);

  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitConstString(text_const);
  builder.EmitConstI32(1);
  builder.EmitConstI32(4);
  builder.EmitStringSlice();
  builder.EmitStringLen();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  module.const_pool = const_pool;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModuleWithTables(code, const_pool, {}, {}, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_string_slice_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrRefOpsModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel ok = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(0);
  builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder.EmitIsNull();
  builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder.EmitRefEq();
  builder.EmitBoolAnd();
  builder.EmitJmpTrue(ok);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(ok, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }

  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_ref_ops_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrFieldModule() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 1);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0);
  AppendU32(fields, 0);
  AppendU32(fields, 0);
  AppendU32(fields, 4);

  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitNewObject(1);
  builder.EmitDup();
  builder.EmitConstI32(12);
  builder.EmitStoreField(0);
  builder.EmitLoadField(0);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  module.types_bytes = types;
  module.fields_bytes = fields;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected_pool = module.const_pool;
  if (expected_pool.empty()) {
    uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(expected_pool, ""));
    uint32_t dummy_const_id = 0;
    AppendConstString(expected_pool, dummy_str_offset, &dummy_const_id);
  }
  std::vector<uint8_t> expected = BuildModuleWithTables(code, expected_pool, types, fields, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_field_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrTypeOfModule() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  Simple::IR::IrBuilder builder;
  builder.EmitEnter(0);
  builder.EmitNewObject(1);
  builder.EmitTypeOf();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 0;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  module.types_bytes = types;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected_pool = module.const_pool;
  if (expected_pool.empty()) {
    uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(expected_pool, ""));
    uint32_t dummy_const_id = 0;
    AppendConstString(expected_pool, dummy_str_offset, &dummy_const_id);
  }
  std::vector<uint8_t> expected = BuildModuleWithTables(code, expected_pool, types, {}, 0, 0);
  if (!ExpectSbcEqual(out, expected, "ir_typeof_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayI64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitConstI64(42);
  builder.EmitArraySetI64();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitArrayGetI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_i64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListF32Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstF32(3.5f);
  builder.EmitListPushF32();
  builder.EmitLoadLocal(0);
  builder.EmitListPopF32();
  builder.EmitConvF32ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_f32_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListRefModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder.EmitListPushRef();
  builder.EmitLoadLocal(0);
  builder.EmitListPopRef();
  builder.EmitIsNull();
  builder.EmitJmpTrue(builder.CreateLabel());
  std::vector<uint8_t> code;
  std::string error;
  Simple::IR::IrBuilder builder2;
  Simple::IR::IrLabel ok = builder2.CreateLabel();
  Simple::IR::IrLabel done = builder2.CreateLabel();
  builder2.EmitEnter(1);
  builder2.EmitNewList(0, 2);
  builder2.EmitStoreLocal(0);
  builder2.EmitLoadLocal(0);
  builder2.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder2.EmitListPushRef();
  builder2.EmitLoadLocal(0);
  builder2.EmitListPopRef();
  builder2.EmitIsNull();
  builder2.EmitJmpTrue(ok);
  builder2.EmitConstI32(0);
  builder2.EmitJmp(done);
  builder2.BindLabel(ok, nullptr);
  builder2.EmitConstI32(1);
  builder2.BindLabel(done, nullptr);
  builder2.EmitRet();
  if (!builder2.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_ref_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayF64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitConstF64(6.0);
  builder.EmitArraySetF64();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitArrayGetF64();
  builder.EmitConvF64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_f64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayRefModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel ok = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder.EmitArraySetRef();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitArrayGetRef();
  builder.EmitIsNull();
  builder.EmitJmpTrue(ok);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(ok, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_ref_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListF64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstF64(4.0);
  builder.EmitListPushF64();
  builder.EmitLoadLocal(0);
  builder.EmitListPopF64();
  builder.EmitConvF64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_f64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayF32Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitConstF32(3.5f);
  builder.EmitArraySetF32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(1);
  builder.EmitArrayGetF32();
  builder.EmitConvF32ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_f32_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListI64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI64(21);
  builder.EmitListPushI64();
  builder.EmitLoadLocal(0);
  builder.EmitListPopI64();
  builder.EmitConvI64ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_i64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayLenModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 4);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitArrayLen();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_len_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListLenModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitListLen();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_len_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListInsertRemoveModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 4);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitConstI32(9);
  builder.EmitListInsertI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListRemoveI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_insert_remove_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListClearModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(5);
  builder.EmitListPushI32();
  builder.EmitLoadLocal(0);
  builder.EmitListClear();
  builder.EmitConstI32(1);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_clear_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListGetSetModule() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(2);
  builder.EmitListPushI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitConstI32(7);
  builder.EmitListSetI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetI32();
  builder.EmitAddI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_get_set_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayGetSetF32Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 1);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitConstF32(1.5f);
  builder.EmitArraySetF32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitArrayGetF32();
  builder.EmitConvF32ToI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_get_set_f32_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrArrayGetSetRefModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel ok = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(1);
  builder.EmitNewArray(0, 1);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder.EmitArraySetRef();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitArrayGetRef();
  builder.EmitIsNull();
  builder.EmitJmpTrue(ok);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(ok, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_array_get_set_ref_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListGetSetF32Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstF32(2.5f);
  builder.EmitListPushF32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetF32();
  builder.EmitConvF32ToI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitConstF32(3.5f);
  builder.EmitListSetF32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetF32();
  builder.EmitConvF32ToI32();
  builder.EmitAddI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_get_set_f32_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListGetSetRefModule() {
  Simple::IR::IrBuilder builder;
  Simple::IR::IrLabel ok = builder.CreateLabel();
  Simple::IR::IrLabel done = builder.CreateLabel();
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitOp(Simple::Byte::OpCode::ConstNull);
  builder.EmitListPushRef();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetRef();
  builder.EmitIsNull();
  builder.EmitJmpTrue(ok);
  builder.EmitConstI32(0);
  builder.EmitJmp(done);
  builder.BindLabel(ok, nullptr);
  builder.EmitConstI32(1);
  builder.BindLabel(done, nullptr);
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_get_set_ref_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListGetSetI64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstI64(10);
  builder.EmitListPushI64();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetI64();
  builder.EmitConvI64ToI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitConstI64(11);
  builder.EmitListSetI64();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetI64();
  builder.EmitConvI64ToI32();
  builder.EmitAddI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_get_set_i64_module")) {
    return {};
  }
  return out;
}

std::vector<uint8_t> BuildIrListGetSetF64Module() {
  Simple::IR::IrBuilder builder;
  builder.EmitEnter(1);
  builder.EmitNewList(0, 2);
  builder.EmitStoreLocal(0);
  builder.EmitLoadLocal(0);
  builder.EmitConstF64(2.0);
  builder.EmitListPushF64();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetF64();
  builder.EmitConvF64ToI32();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitConstF64(3.0);
  builder.EmitListSetF64();
  builder.EmitLoadLocal(0);
  builder.EmitConstI32(0);
  builder.EmitListGetF64();
  builder.EmitConvF64ToI32();
  builder.EmitAddI32();
  builder.EmitRet();
  std::vector<uint8_t> code;
  std::string error;
  if (!builder.Finish(&code, &error)) {
    std::cerr << "IR finish failed: " << error << "\n";
    return {};
  }
  Simple::IR::IrModule module;
  Simple::IR::IrFunction func;
  func.code = code;
  func.local_count = 1;
  func.stack_max = 8;
  module.functions.push_back(std::move(func));
  module.entry_method_id = 0;
  std::vector<uint8_t> out;
  if (!Simple::IR::CompileToSbc(module, &out, &error)) {
    std::cerr << "IR compile failed: " << error << "\n";
    return {};
  }
  std::vector<uint8_t> expected = BuildModule(code, 0, 1);
  if (!ExpectSbcEqual(out, expected, "ir_list_get_set_f64_module")) {
    return {};
  }
  return out;
}


bool RunIrEmitAddTest() {
  return RunExpectExit(BuildIrAddModule(), 12);
}

bool RunIrEmitJumpTest() {
  return RunExpectExit(BuildIrJumpModule(), 7);
}

bool RunIrEmitJmpTableTest() {
  return RunExpectExit(BuildIrJmpTableModule(1), 2);
}

bool RunIrEmitStackOps2Test() {
  return RunExpectExit(BuildIrStackOps2Module(), 10);
}

bool RunIrEmitI64BitwiseTest() {
  return RunExpectExit(BuildIrI64BitwiseModule(), 2);
}

bool RunIrEmitConstSmallTest() {
  return RunExpectExit(BuildIrConstSmallModule(), 72);
}

bool RunIrEmitIncDecNegTest() {
  return RunExpectExit(BuildIrIncDecNegModule(), 6);
}

bool RunIrEmitIncDecNegWideTest() {
  return RunExpectExit(BuildIrIncDecNegWideModule(), 6);
}

bool RunIrEmitListInsertRemoveI64Test() {
  return RunExpectExit(BuildIrListInsertRemoveI64Module(), 9);
}

bool RunIrEmitU32ArithTest() {
  return RunExpectExit(BuildIrU32ArithModule(), 2);
}

bool RunIrEmitCmpVariantsTest() {
  return RunExpectExit(BuildIrCmpVariantsModule(), 1);
}

bool RunIrEmitU64ArithTest() {
  return RunExpectExit(BuildIrU64ArithModule(), 6);
}

bool RunIrEmitF64CmpTest() {
  return RunExpectExit(BuildIrF64CmpModule(), 1);
}

bool RunIrEmitU64CmpTest() {
  return RunExpectExit(BuildIrU64CmpModule(), 1);
}

bool RunIrEmitF32Arith2Test() {
  return RunExpectExit(BuildIrF32ArithModule2(), 3);
}

bool RunIrEmitF64Arith2Test() {
  return RunExpectExit(BuildIrF64ArithModule2(), 27);
}

bool RunIrEmitU32Arith2Test() {
  return RunExpectExit(BuildIrU32ArithModule2(), 12);
}

bool RunIrEmitU64Arith2Test() {
  return RunExpectExit(BuildIrU64ArithModule2(), 4);
}

bool RunIrEmitU32CmpTest() {
  return RunExpectExit(BuildIrU32CmpModule(), 1);
}

bool RunIrEmitU64Cmp2Test() {
  return RunExpectExit(BuildIrU64CmpModule2(), 1);
}

bool RunIrEmitF32CmpTest() {
  return RunExpectExit(BuildIrF32CmpModule(), 1);
}

bool RunIrEmitF64Cmp2Test() {
  return RunExpectExit(BuildIrF64CmpModule2(), 1);
}

bool RunIrEmitI64ArithTest() {
  return RunExpectExit(BuildIrI64ArithModule(), 2);
}

bool RunIrEmitU32ModTest() {
  return RunExpectExit(BuildIrU32ModModule(), 4);
}

bool RunIrEmitU64ModTest() {
  return RunExpectExit(BuildIrU64ModModule(), 4);
}

bool RunIrEmitI64MulTest() {
  return RunExpectExit(BuildIrI64MulModule(), 12);
}

bool RunIrEmitI64DivTest() {
  return RunExpectExit(BuildIrI64DivModule(), 3);
}

bool RunIrEmitU32Arith3Test() {
  return RunExpectExit(BuildIrU32ArithModule3(), 5);
}

bool RunIrEmitU64Div2Test() {
  return RunExpectExit(BuildIrU64DivModule2(), 4);
}

bool RunIrEmitU32DivTest() {
  return RunExpectExit(BuildIrU32DivModule(), 4);
}

bool RunIrEmitU64AddTest() {
  return RunExpectExit(BuildIrU64AddModule(), 5);
}

bool RunIrEmitF32SubTest() {
  return RunExpectExit(BuildIrF32SubModule(), 3);
}

bool RunIrEmitF64SubTest() {
  return RunExpectExit(BuildIrF64SubModule(), 3);
}

bool RunIrEmitU32MulTest() {
  return RunExpectExit(BuildIrU32MulModule(), 12);
}

bool RunIrEmitU64SubTest() {
  return RunExpectExit(BuildIrU64SubModule(), 5);
}

bool RunIrEmitF32MulTest() {
  return RunExpectExit(BuildIrF32MulModule(), 12);
}

bool RunIrEmitF64DivTest() {
  return RunExpectExit(BuildIrF64DivModule(), 4);
}

bool RunIrEmitI32Arith2Test() {
  return RunExpectExit(BuildIrI32ArithModule2(), 6);
}

bool RunIrEmitI64AddSubTest() {
  return RunExpectExit(BuildIrI64AddSubModule(), 8);
}

bool RunIrEmitLocalsTest() {
  return RunExpectExit(BuildIrLocalsModule(), 9);
}

bool RunIrEmitCallTest() {
  return RunExpectExit(BuildIrCallModule(), 7);
}

bool RunIrEmitCallCheckTest() {
  return RunExpectExit(BuildIrCallCheckModule(), 0);
}

bool RunIrEmitIntrinsicTest() {
  return RunExpectExit(BuildIrIntrinsicModule(), 0);
}

bool RunIrEmitSysCallTest() {
  return RunExpectVerifyFail(BuildIrSysCallModule(), "ir_emit_syscall");
}

bool RunIrEmitNewClosureTest() {
  return RunExpectExit(BuildIrNewClosureModule(), 0);
}

bool RunIrEmitUpvalueTest() {
  return RunExpectExit(BuildIrUpvalueModule(), 1);
}

bool RunIrEmitGlobalsTest() {
  return RunExpectExit(BuildIrGlobalsModule(), 11);
}

bool RunIrEmitStackOpsTest() {
  return RunExpectExit(BuildIrStackOpsModule(), 5);
}

bool RunIrEmitBranchTest() {
  return RunExpectExit(BuildIrBranchModule(), 9);
}

bool RunIrEmitCompareTest() {
  return RunExpectExit(BuildIrCompareModule(), 1);
}

bool RunIrEmitBoolTest() {
  return RunExpectExit(BuildIrBoolModule(), 1);
}

bool RunIrEmitConvI32I64Test() {
  return RunExpectExit(BuildIrConvI32ToI64Module(), 9);
}

bool RunIrEmitConvI32F64Test() {
  return RunExpectExit(BuildIrConvI32ToF64Module(), 7);
}

bool RunIrEmitConvF32F64Test() {
  return RunExpectExit(BuildIrConvF32F64Module(), 7);
}

bool RunIrEmitF32ArithTest() {
  return RunExpectExit(BuildIrF32ArithModule(), 7);
}

bool RunIrEmitBitwiseI32Test() {
  return RunExpectExit(BuildIrBitwiseI32Module(), 2);
}

bool RunIrEmitCallIndirectTest() {
  return RunExpectExit(BuildIrCallIndirectModule(), 9);
}

bool RunIrEmitTailCallTest() {
  return RunExpectExit(BuildIrTailCallModule(), 42);
}

bool RunIrEmitArrayTest() {
  return RunExpectExit(BuildIrArrayModule(), 7);
}

bool RunIrEmitListTest() {
  return RunExpectExit(BuildIrListModule(), 5);
}

bool RunIrEmitStringTest() {
  return RunExpectExit(BuildIrStringModule(), 3);
}

bool RunIrEmitStringGetCharTest() {
  return RunExpectExit(BuildIrStringGetCharModule(), 66);
}

bool RunIrEmitStringSliceTest() {
  return RunExpectExit(BuildIrStringSliceModule(), 3);
}

bool RunIrEmitRefOpsTest() {
  return RunExpectExit(BuildIrRefOpsModule(), 1);
}

bool RunIrEmitFieldTest() {
  return RunExpectExit(BuildIrFieldModule(), 12);
}

bool RunIrEmitTypeOfTest() {
  return RunExpectExit(BuildIrTypeOfModule(), 1);
}

bool RunIrEmitArrayI64Test() {
  return RunExpectExit(BuildIrArrayI64Module(), 42);
}

bool RunIrEmitListF32Test() {
  return RunExpectExit(BuildIrListF32Module(), 3);
}

bool RunIrEmitListRefTest() {
  return RunExpectExit(BuildIrListRefModule(), 1);
}

bool RunIrEmitArrayF64Test() {
  return RunExpectExit(BuildIrArrayF64Module(), 6);
}

bool RunIrEmitArrayRefTest() {
  return RunExpectExit(BuildIrArrayRefModule(), 1);
}

bool RunIrEmitListF64Test() {
  return RunExpectExit(BuildIrListF64Module(), 4);
}

bool RunIrEmitArrayF32Test() {
  return RunExpectExit(BuildIrArrayF32Module(), 3);
}

bool RunIrEmitListI64Test() {
  return RunExpectExit(BuildIrListI64Module(), 21);
}

bool RunIrEmitArrayLenTest() {
  return RunExpectExit(BuildIrArrayLenModule(), 4);
}

bool RunIrEmitListLenTest() {
  return RunExpectExit(BuildIrListLenModule(), 0);
}

bool RunIrEmitListInsertRemoveTest() {
  return RunExpectExit(BuildIrListInsertRemoveModule(), 9);
}

bool RunIrEmitListClearTest() {
  return RunExpectExit(BuildIrListClearModule(), 1);
}

bool RunIrEmitListGetSetTest() {
  return RunExpectExit(BuildIrListGetSetModule(), 9);
}

bool RunIrEmitArrayGetSetF32Test() {
  return RunExpectExit(BuildIrArrayGetSetF32Module(), 1);
}

bool RunIrEmitArrayGetSetRefTest() {
  return RunExpectExit(BuildIrArrayGetSetRefModule(), 1);
}

bool RunIrEmitListGetSetF32Test() {
  return RunExpectExit(BuildIrListGetSetF32Module(), 5);
}

bool RunIrEmitListGetSetRefTest() {
  return RunExpectExit(BuildIrListGetSetRefModule(), 1);
}

bool RunIrEmitListGetSetI64Test() {
  return RunExpectExit(BuildIrListGetSetI64Module(), 21);
}

bool RunIrEmitListGetSetF64Test() {
  return RunExpectExit(BuildIrListGetSetF64Module(), 5);
}

bool RunIrTextAddTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 7\n"
      "  const.i32 5\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_add");
  if (module.empty()) return false;
  return RunExpectExit(module, 12);
}

bool RunIrTextSmallTypeOpsTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i8 7\n"
      "  neg.i8\n"
      "  pop\n"
      "  const.i16 9\n"
      "  inc.i16\n"
      "  pop\n"
      "  const.u8 5\n"
      "  dec.u8\n"
      "  pop\n"
      "  const.u16 2\n"
      "  neg.u16\n"
      "  pop\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_small_type_ops");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextUnsignedWideOpsTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.u32 1\n"
      "  inc.u32\n"
      "  pop\n"
      "  const.u32 2\n"
      "  dec.u32\n"
      "  pop\n"
      "  const.u64 0\n"
      "  neg.u64\n"
      "  pop\n"
      "  const.u64 3\n"
      "  inc.u64\n"
      "  pop\n"
      "  const.i32 2\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_unsigned_wide_ops");
  if (module.empty()) return false;
  return RunExpectExit(module, 2);
}

bool RunIrTextFloatIncDecTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.f32 2.25\n"
      "  inc.f32\n"
      "  conv.f32.i32\n"
      "  const.f64 5.9\n"
      "  dec.f64\n"
      "  conv.f64.i32\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_float_inc_dec");
  if (module.empty()) return false;
  return RunExpectExit(module, 7);
}

bool RunIrTextBranchTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i32 3\n"
      "  const.i32 2\n"
      "  cmp.gt.i32\n"
      "  jmp.true is_true\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "is_true:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_branch");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextLocalsTest() {
  const char* text =
      "func main locals=1 stack=6\n"
      "  enter 1\n"
      "  const.i32 10\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_locals");
  if (module.empty()) return false;
  return RunExpectExit(module, 12);
}

bool RunIrTextBitwiseBoolTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i32 6\n"
      "  const.i32 3\n"
      "  and.i32\n"
      "  const.i32 2\n"
      "  shl.i32\n"
      "  const.i32 10\n"
      "  cmp.eq.i32\n"
      "  bool.not\n"
      "  jmp.true is_true\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "is_true:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_bitwise_bool");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextIntrinsicTrapTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  intrinsic 999\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_intrinsic_trap");
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_intrinsic_trap");
}

bool RunIrTextSysCallVerifyFailTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  syscall 7\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_syscall_verify_fail");
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_syscall_verify_fail");
}

bool RunIrTextSysCallMissingIdTest() {
  const char* text =
      "func main locals=0 stack=2\n"
      "  enter 0\n"
      "  syscall\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_syscall_missing_id");
}

bool RunIrTextConstBoolTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.bool 1\n"
      "  bool.not\n"
      "  jmp.true is_true\n"
      "  const.i32 1\n"
      "  jmp done\n"
      "is_true:\n"
      "  const.i32 0\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_const_bool");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextConstCharTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.char 65\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_const_char");
  if (module.empty()) return false;
  return RunExpectExit(module, 65);
}

bool RunIrTextArrayLenTest() {
  const char* text =
      "func main locals=1 stack=10\n"
      "  enter 1\n"
      "  newarray 0 3\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  array.len\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_len");
  if (module.empty()) return false;
  return RunExpectExit(module, 3);
}

bool RunIrTextBoolAndOrTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.bool 1\n"
      "  const.bool 0\n"
      "  bool.and\n"
      "  bool.not\n"
      "  const.bool 1\n"
      "  bool.or\n"
      "  jmp.true is_true\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "is_true:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_bool_and_or");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextCmpUnsignedTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.u32 0\n"
      "  const.u32 1\n"
      "  cmp.lt.u32\n"
      "  const.u64 2\n"
      "  const.u64 1\n"
      "  cmp.gt.u64\n"
      "  bool.and\n"
      "  jmp.true is_true\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "is_true:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_cmp_unsigned");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextCallCheckTest() {
  const char* text =
      "func main locals=0 stack=4 sig=0\n"
      "  enter 0\n"
      "  callcheck 0\n"
      "  const.i32 2\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_callcheck");
  if (module.empty()) return false;
  return RunExpectExit(module, 2);
}

bool RunIrTextArrayI32Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newarray 0 2\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.i32 7\n"
      "  array.set.i32\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  array.get.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_i32");
  if (module.empty()) return false;
  return RunExpectExit(module, 7);
}

bool RunIrTextListI32Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 5\n"
      "  list.push.i32\n"
      "  ldloc 0\n"
      "  const.i32 6\n"
      "  list.push.i32\n"
      "  ldloc 0\n"
      "  list.len\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_i32");
  if (module.empty()) return false;
  return RunExpectExit(module, 2);
}

bool RunIrTextObjectFieldTest() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newobj 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 42\n"
      "  stfld 0\n"
      "  ldloc 0\n"
      "  ldfld 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 1);

  std::vector<uint8_t> fields;
  AppendU32(fields, 0);
  AppendU32(fields, 0);
  AppendU32(fields, 0);
  AppendU32(fields, 1);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_object_field",
                                            std::move(types), std::move(fields),
                                            std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 42);
}

bool RunIrTextNamedTablesTest() {
  const char* text =
      "types:\n"
      "  type Color size=16 kind=artifact\n"
      "  field r i32 offset=0\n"
      "  field g i32 offset=4\n"
      "  field b i32 offset=8\n"
      "  field a i32 offset=12\n"
      "sigs:\n"
      "  sig main: () -> i32\n"
      "consts:\n"
      "  const max i32 255\n"
      "  const greet string \"hi\"\n"
      "imports:\n"
      "  intrinsic log 3\n"
      "func main locals=1 stack=10 sig=main\n"
      "  locals: c\n"
      "  enter 1\n"
      "  newobj Color\n"
      "  stloc c\n"
      "  ldloc c\n"
      "  const.i32 max\n"
      "  stfld Color.r\n"
      "  ldloc c\n"
      "  ldfld Color.r\n"
      "  const.string greet\n"
      "  pop\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_named_tables");
  if (module.empty()) return false;
  return RunExpectExit(module, 255);
}

bool RunIrTextBadTypeNameTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  newobj MissingType\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_type_name");
}

bool RunIrTextBadFieldNameTest() {
  const char* text =
      "types:\n"
      "  type Color size=16 kind=artifact\n"
      "  field r i32 offset=0\n"
      "sigs:\n"
      "  sig main: () -> i32\n"
      "func main locals=1 stack=6 sig=main\n"
      "  locals: c\n"
      "  enter 1\n"
      "  newobj Color\n"
      "  stloc c\n"
      "  ldloc c\n"
      "  ldfld Color.g\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_field_name");
}

bool RunIrTextFieldMisalignedTest() {
  const char* text =
      "types:\n"
      "  type Obj size=8 kind=artifact\n"
      "  field a i32 offset=2\n"
      "sigs:\n"
      "  sig main: () -> i32\n"
      "func main locals=0 stack=4 sig=main\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_field_misaligned");
}

bool RunIrTextFieldOutOfBoundsTest() {
  const char* text =
      "types:\n"
      "  type Obj size=8 kind=artifact\n"
      "  field a i64 offset=4\n"
      "sigs:\n"
      "  sig main: () -> i32\n"
      "func main locals=0 stack=4 sig=main\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_field_oob");
}

bool RunIrTextBadConstNameTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 MissingConst\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_const_name");
}

bool RunIrTextLowerLineNumberTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  bad.op\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::IR::Text::IrTextModule parsed;
  std::string error;
  if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
    std::cerr << "expected lower error, got parse: " << error << "\n";
    return false;
  }
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
    return error.find("line 3") != std::string::npos;
  }
  std::cerr << "expected lower failure for bad op\n";
  return false;
}

bool RunIrTextLocalTypeNameTest() {
  const char* text =
      "func main locals=2 stack=4\n"
      "  locals: a:i32, b:ref\n"
      "  enter 2\n"
      "  const.i32 1\n"
      "  stloc a\n"
      "  ldloc a\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_local_type_name");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextLocalTypeBadNameTest() {
  const char* text =
      "func main locals=1 stack=4\n"
      "  locals: a:MissingType\n"
      "  enter 1\n"
      "  const.i32 1\n"
      "  stloc a\n"
      "  ldloc a\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_local_type_bad_name");
}

bool RunIrTextUpvalueTypeBadNameTest() {
  const char* text =
      "func callee locals=0 stack=6\n"
      "  upvalues: uv:MissingType\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry callee\n";
  return RunIrTextExpectFail(text, "ir_text_upvalue_type_bad_name");
}

bool RunIrTextSyscallNameFailTest() {
  const char* text =
      "imports:\n"
      "  syscall demo 7\n"
      "func main locals=0 stack=2\n"
      "  enter 0\n"
      "  syscall demo\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_syscall_name_fail");
}

bool RunIrTextStringLenTest() {
  std::vector<uint8_t> const_pool;
  uint32_t str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, "hey"));
  uint32_t const_id = 0;
  AppendConstString(const_pool, str_offset, &const_id);
  std::string text = "func main locals=0 stack=4\n";
  text += "  enter 0\n";
  text += "  const.string " + std::to_string(const_id) + "\n";
  text += "  string.len\n";
  text += "  ret\n";
  text += "end\n";
  text += "entry main\n";

  auto module = BuildIrTextModuleWithTables(text, "ir_text_string_len",
                                            {}, {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 3);
}

bool RunIrTextBadOperandTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_operand");
}

bool RunIrTextUnknownOpTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  wat\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_unknown_op");
}

bool RunIrTextGlobalTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 5\n"
      "  stglob 0\n"
      "  ldglob 0\n"
      "  const.i32 3\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> globals;
  AppendU32(globals, 0);              // name_str
  AppendU32(globals, 0);              // type_id
  AppendU32(globals, 0);              // flags
  AppendU32(globals, 0xFFFFFFFFu);    // init_const_id

  auto module = BuildIrTextModuleWithTablesAndGlobals(text, "ir_text_global",
                                                      std::move(types), {}, {}, std::move(globals));
  if (module.empty()) return false;
  return RunExpectExit(module, 8);
}

bool RunIrTextNamedGlobalsTest() {
  const char* text =
      "globals:\n"
      "  global g i32\n"
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 9\n"
      "  stglob g\n"
      "  ldglob g\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_named_globals");
  if (module.empty()) return false;
  return RunExpectExit(module, 9);
}

bool RunIrTextNamedGlobalsInitTest() {
  const char* text =
      "consts:\n"
      "  const greet string \"hi\"\n"
      "  const kf f32 2.5\n"
      "globals:\n"
      "  global gs string init=greet\n"
      "  global gf f32 init=kf\n"
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  ldglob gf\n"
      "  pop\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_named_globals_init");
  if (module.empty()) return false;
  return RunExpectExit(module, 0);
}

bool RunIrTextNamedGlobalsBadNameTest() {
  const char* text =
      "globals:\n"
      "  global g i32\n"
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  ldglob missing\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_named_globals_bad_name");
}

bool RunIrTextUnknownLabelTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  jmp missing\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_unknown_label");
}

bool RunIrTextRefOpsTest() {
  const char* text =
      "func main locals=0 stack=10\n"
      "  enter 0\n"
      "  newobj 1\n"
      "  dup\n"
      "  ref.eq\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_ref_ops",
                                            std::move(types), {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextTypeOfTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  newobj 1\n"
      "  typeof\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_typeof",
                                            std::move(types), {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextClosureUpvalueTest() {
  const char* text =
      "func callee locals=0 stack=10 sig=0\n"
      "  enter 0\n"
      "  ldupv 0\n"
      "  isnull\n"
      "  bool.not\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  newobj 1\n"
      "  newclosure 0 1\n"
      "  call.indirect 0 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_closure_upvalue",
                                            std::move(types), {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextBadNewClosureTest() {
  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  newclosure 99 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_bad_newclosure");
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_bad_newclosure");
}

bool RunIrTextStringConcatTest() {
  std::vector<uint8_t> const_pool;
  uint32_t left_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hi"));
  uint32_t left_id = 0;
  AppendConstString(const_pool, left_off, &left_id);
  uint32_t right_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "!"));
  uint32_t right_id = 0;
  AppendConstString(const_pool, right_off, &right_id);

  std::string text = "func main locals=0 stack=8\n";
  text += "  enter 0\n";
  text += "  const.string " + std::to_string(left_id) + "\n";
  text += "  const.string " + std::to_string(right_id) + "\n";
  text += "  string.concat\n";
  text += "  string.len\n";
  text += "  ret\n";
  text += "end\n";
  text += "entry main\n";

  auto module = BuildIrTextModuleWithTables(text, "ir_text_string_concat",
                                            {}, {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 3);
}

bool RunIrTextStringGetCharTest() {
  std::vector<uint8_t> const_pool;
  uint32_t str_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "abc"));
  uint32_t str_id = 0;
  AppendConstString(const_pool, str_off, &str_id);

  std::string text = "func main locals=0 stack=10\n";
  text += "  enter 0\n";
  text += "  const.string " + std::to_string(str_id) + "\n";
  text += "  const.i32 1\n";
  text += "  string.get.char\n";
  text += "  const.i32 98\n";
  text += "  cmp.eq.i32\n";
  text += "  jmp.true ok\n";
  text += "  const.i32 0\n";
  text += "  jmp done\n";
  text += "ok:\n";
  text += "  const.i32 1\n";
  text += "done:\n";
  text += "  ret\n";
  text += "end\n";
  text += "entry main\n";

  auto module = BuildIrTextModuleWithTables(text, "ir_text_string_get_char",
                                            {}, {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextStringSliceTest() {
  std::vector<uint8_t> const_pool;
  uint32_t str_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hello"));
  uint32_t str_id = 0;
  AppendConstString(const_pool, str_off, &str_id);

  std::string text = "func main locals=0 stack=10\n";
  text += "  enter 0\n";
  text += "  const.string " + std::to_string(str_id) + "\n";
  text += "  const.i32 1\n";
  text += "  const.i32 4\n";
  text += "  string.slice\n";
  text += "  string.len\n";
  text += "  ret\n";
  text += "end\n";
  text += "entry main\n";

  auto module = BuildIrTextModuleWithTables(text, "ir_text_string_slice",
                                            {}, {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 3);
}

bool RunIrTextArrayI64Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newarray 0 2\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.i64 9\n"
      "  array.set.i64\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  array.get.i64\n"
      "  conv.i64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_i64");
  if (module.empty()) return false;
  return RunExpectExit(module, 9);
}

bool RunIrTextArrayF32Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.f32 3.5\n"
      "  array.set.f32\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  array.get.f32\n"
      "  conv.f32.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_f32");
  if (module.empty()) return false;
  return RunExpectExit(module, 3);
}

bool RunIrTextArrayF64Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.f64 4.0\n"
      "  array.set.f64\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  array.get.f64\n"
      "  conv.f64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_f64");
  if (module.empty()) return false;
  return RunExpectExit(module, 4);
}

bool RunIrTextArrayRefTest() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  newobj 1\n"
      "  array.set.ref\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  array.get.ref\n"
      "  isnull\n"
      "  bool.not\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_array_ref",
                                            std::move(types), {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextListI64Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i64 3\n"
      "  list.push.i64\n"
      "  ldloc 0\n"
      "  list.pop.i64\n"
      "  conv.i64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_i64");
  if (module.empty()) return false;
  return RunExpectExit(module, 3);
}

bool RunIrTextListF32Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.f32 2.5\n"
      "  list.push.f32\n"
      "  ldloc 0\n"
      "  list.pop.f32\n"
      "  conv.f32.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_f32");
  if (module.empty()) return false;
  return RunExpectExit(module, 2);
}

bool RunIrTextListF64Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.f64 4.0\n"
      "  list.push.f64\n"
      "  ldloc 0\n"
      "  list.pop.f64\n"
      "  conv.f64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_f64");
  if (module.empty()) return false;
  return RunExpectExit(module, 4);
}

bool RunIrTextListRefTest() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  newobj 1\n"
      "  list.push.ref\n"
      "  ldloc 0\n"
      "  list.pop.ref\n"
      "  isnull\n"
      "  bool.not\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_list_ref",
                                            std::move(types), {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextListInsertRemoveTest() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.i32 9\n"
      "  list.insert.i32\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  list.remove.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_remove");
  if (module.empty()) return false;
  return RunExpectExit(module, 9);
}

bool RunIrTextListInsertRemoveF32Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.f32 3.5\n"
      "  list.insert.f32\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  list.remove.f32\n"
      "  conv.f32.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_remove_f32");
  if (module.empty()) return false;
  return RunExpectExit(module, 3);
}

bool RunIrTextListInsertRemoveI64Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.i64 12\n"
      "  list.insert.i64\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  list.remove.i64\n"
      "  conv.i64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_remove_i64");
  if (module.empty()) return false;
  return RunExpectExit(module, 12);
}

bool RunIrTextListInsertRemoveRefTest() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.null\n"
      "  list.insert.ref\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  list.remove.ref\n"
      "  pop\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_remove_ref");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextListInsertRemoveF64Test() {
  const char* text =
      "func main locals=1 stack=12\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.f64 7.5\n"
      "  list.insert.f64\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  list.remove.f64\n"
      "  conv.f64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_remove_f64");
  if (module.empty()) return false;
  return RunExpectExit(module, 7);
}

bool RunIrTextConvChainTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i32 7\n"
      "  conv.i32.f64\n"
      "  conv.f64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_conv_chain");
  if (module.empty()) return false;
  return RunExpectExit(module, 7);
}

bool RunIrTextBitwiseI32Test() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i32 6\n"
      "  const.i32 3\n"
      "  and.i32\n"
      "  const.i32 4\n"
      "  or.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_bitwise_i32");
  if (module.empty()) return false;
  return RunExpectExit(module, 6);
}

bool RunIrTextBitwiseI64Test() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i64 6\n"
      "  const.i64 3\n"
      "  and.i64\n"
      "  const.i64 4\n"
      "  or.i64\n"
      "  conv.i64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_bitwise_i64");
  if (module.empty()) return false;
  return RunExpectExit(module, 6);
}

bool RunIrTextShiftI32Test() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  const.i32 3\n"
      "  shl.i32\n"
      "  const.i32 2\n"
      "  shr.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_shift_i32");
  if (module.empty()) return false;
  return RunExpectExit(module, 2);
}

bool RunIrTextShiftI64Test() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i64 1\n"
      "  const.i64 4\n"
      "  shl.i64\n"
      "  const.i64 2\n"
      "  shr.i64\n"
      "  conv.i64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_shift_i64");
  if (module.empty()) return false;
  return RunExpectExit(module, 4);
}

bool RunIrTextCompareI32Test() {
  const char* text =
      "func main locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  const.i32 4\n"
      "  const.i32 4\n"
      "  cmp.eq.i32\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_compare_i32");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextCompareU64Test() {
  const char* text =
      "func main locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  const.u64 5\n"
      "  const.u64 7\n"
      "  cmp.lt.u64\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_compare_u64");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextBoolOpsTest() {
  const char* text =
      "func main locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  const.bool 1\n"
      "  const.bool 0\n"
      "  bool.or\n"
      "  bool.not\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_bool_ops");
  if (module.empty()) return false;
  return RunExpectExit(module, 0);
}

bool RunIrTextRefNullTest() {
  const char* text =
      "func main locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  const.null\n"
      "  isnull\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_ref_null");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextBoolTypeMismatchTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  const.i32 0\n"
      "  bool.and\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bool_type_mismatch");
}

bool RunIrTextCompareTypeMismatchTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  const.i64 2\n"
      "  cmp.eq.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_cmp_type_mismatch");
}

bool RunIrTextShiftTypeMismatchTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i64 1\n"
      "  const.i32 1\n"
      "  shl.i64\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_shift_type_mismatch");
}

bool RunIrTextListInsertTypeMismatchTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.i32 9\n"
      "  list.insert.f32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_list_insert_type_mismatch");
}

bool RunIrTextArraySetTypeMismatchTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.i32 1\n"
      "  array.set.f64\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_array_set_type_mismatch");
}

bool RunIrTextCallArgCountMismatchTest() {
  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  call 1 2\n"
      "  ret\n"
      "end\n"
      "func target locals=0 stack=4 sig=0\n"
      "  enter 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_arg_count_mismatch");
}

bool RunIrTextCallIndirectArgCountMismatchTest() {
  const char* text =
      "func main locals=1 stack=6 sig=0\n"
      "  enter 1\n"
      "  const.null\n"
      "  call.indirect 0 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_indirect_arg_count_mismatch");
}

bool RunIrTextJmpNonBoolCondTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  ret\n"
      "ok:\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmp_non_bool_cond");
}

bool RunIrTextArrayGetNonRefTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  const.i32 0\n"
      "  array.get.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_array_get_non_ref");
}

bool RunIrTextListGetNonRefTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  const.i32 0\n"
      "  list.get.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_list_get_non_ref");
}

bool RunIrTextCallIndirectBadSigIdTextTest() {
  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  const.null\n"
      "  call.indirect 5 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_indirect_bad_sig_id");
}

bool RunIrTextJmpTableMissingLabelTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  jmptable def case0 case1\n"
      "def:\n"
      "  const.i32 0\n"
      "  ret\n"
      "case0:\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmptable_missing_label");
}

bool RunIrTextBadLocalsCountTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 1\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_locals_count");
}

bool RunIrTextStackUnderflowTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  pop\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_stack_underflow");
}

bool RunIrTextJumpToEndTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  jmp done\n"
      "done:\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_jump_to_end");
  if (module.empty()) return false;
  return RunExpectExit(module, 0);
}

bool RunIrTextJumpMidInstructionTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  jmp target\n"
      "  const.i32 1\n"
      "target:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jump_mid_instruction");
}

bool RunIrTextJmpTableArityMismatchTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  jmptable def\n"
      "def:\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmptable_arity_mismatch");
}

bool RunIrTextJmpTableNonI32IndexTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.f32 1.0\n"
      "  jmptable def case0\n"
      "def:\n"
      "  const.i32 0\n"
      "  ret\n"
      "case0:\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmptable_non_i32_index");
}

bool RunIrTextConstI128UnsupportedTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i128 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i128_unsupported");
}

bool RunIrTextConstStringMissingPoolTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.string missing_str\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_string_missing_pool");
}

bool RunIrTextCallMissingSigTest() {
  const char* text =
      "func main locals=0 stack=6 sig=1\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_missing_sig");
}

bool RunIrTextConstU128UnsupportedTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u128 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u128_unsupported");
}

bool RunIrTextConstI128BadTokenTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i128 not_a_number\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i128_bad_token");
}

bool RunIrTextConstU64BadTokenTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u64 nope\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u64_bad_token");
}

bool RunIrTextConstI32BadTokenTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 nope\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i32_bad_token");
}

bool RunIrTextConstF64BadTokenTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.f64 nope\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_f64_bad_token");
}

bool RunIrTextConstF32NanTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.f32 nan\n"
      "  const.f32 nan\n"
      "  cmp.eq.f32\n"
      "  bool.not\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_const_f32_nan");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextConstF32InfTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.f32 inf\n"
      "  const.f32 inf\n"
      "  cmp.eq.f32\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_const_f32_inf");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextConstF64InfTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.f64 inf\n"
      "  const.f64 inf\n"
      "  cmp.eq.f64\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_const_f64_inf");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextConstF64NegInfTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.f64 -inf\n"
      "  const.f64 -inf\n"
      "  cmp.eq.f64\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_const_f64_neg_inf");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextConstU32NegativeTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u32 -1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u32_negative");
}

bool RunIrTextConstI32OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 2147483648\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i32_overflow");
}

bool RunIrTextConstI32UnderflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 -2147483649\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i32_underflow");
}

bool RunIrTextConstU32OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u32 4294967296\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u32_overflow");
}

bool RunIrTextConstI8OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i8 128\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i8_overflow");
}

bool RunIrTextConstU8OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u8 256\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u8_overflow");
}

bool RunIrTextConstI16OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i16 32768\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i16_overflow");
}

bool RunIrTextConstU16OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u16 65536\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u16_overflow");
}

bool RunIrTextConstI64OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i64 9223372036854775808\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i64_overflow");
}

bool RunIrTextConstI64UnderflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i64 -9223372036854775809\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i64_underflow");
}

bool RunIrTextConstU64OverflowTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u64 18446744073709551616\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u64_overflow");
}

bool RunIrTextConstU32BadHexTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u32 0xZZ\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u32_bad_hex");
}

bool RunIrTextConstI32BadHexTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 0xZZ\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_i32_bad_hex");
}

bool RunIrTextConstU32NegativeHexTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.u32 -0x1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_const_u32_negative_hex");
}

bool RunIrTextCallIndirectMissingValueTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  call.indirect 0 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_indirect_missing_value");
}

bool RunIrTextCallIndirectNonRefValueTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  call.indirect 0 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_call_indirect_non_ref_value");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_call_indirect_non_ref_value");
}

bool RunIrTextNewArrayMissingLenTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  newarray 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_newarray_missing_len");
}

bool RunIrTextNewListMissingCapTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  newlist 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_newlist_missing_cap");
}

bool RunIrTextEnterMissingCountTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_enter_missing_count");
}

bool RunIrTextCallMissingArgsTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  call 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_missing_args");
}

bool RunIrTextJmpMissingLabelTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  jmp\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmp_missing_label");
}

bool RunIrTextJmpExtraOperandTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  jmp done extra\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmp_extra_operand");
}

bool RunIrTextCallExtraOperandTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  call 0 0 extra\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_extra_operand");
}

bool RunIrTextCallIndirectExtraOperandTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.null\n"
      "  call.indirect 0 0 extra\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_call_indirect_extra_operand");
}

bool RunIrTextJmpTableMissingDefaultTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  jmptable\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmptable_missing_default");
}

bool RunIrTextUnknownOpCapsTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  ADD.I32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_unknown_op_caps");
}

bool RunIrTextMissingEntryTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n";
  return RunIrTextExpectFail(text, "ir_text_missing_entry");
}

bool RunIrTextDuplicateEntryTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_duplicate_entry");
}

bool RunIrTextBadFuncHeaderTest() {
  const char* text =
      "func main locals=0\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_func_header");
}

bool RunIrTextBadSigTokenTest() {
  const char* text =
      "func main locals=0 stack=4 sig=abc\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_sig_token");
}

bool RunIrTextInvalidLabelNameTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "bad-label:\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_invalid_label_name");
}

bool RunIrTextLabelStartsWithDigitTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "1bad:\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_label_starts_with_digit");
}

bool RunIrTextJmpInvalidLabelTokenTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  jmp bad-label\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmp_invalid_label");
}

bool RunIrTextJmpTableInvalidLabelTokenTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  jmptable def bad-label\n"
      "def:\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmptable_invalid_label");
}

bool RunIrTextEntryUnknownFuncTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry missing\n";
  return RunIrTextExpectFail(text, "ir_text_entry_unknown_func");
}

bool RunIrTextDuplicateFuncTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_duplicate_func");
}

bool RunIrTextBadLocalsTokenTest() {
  const char* text =
      "func main locals=abc stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_locals_token");
}

bool RunIrTextBadStackTokenTest() {
  const char* text =
      "func main locals=0 stack=abc\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_bad_stack_token");
}

bool RunIrTextLocalsOverflowTest() {
  const char* text =
      "func main locals=70000 stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_locals_overflow");
}

bool RunIrTextStackOverflowTest() {
  const char* text =
      "func main locals=0 stack=4294967296\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_stack_overflow");
}

bool RunIrTextSigOverflowTest() {
  const char* text =
      "func main locals=0 stack=4 sig=4294967296\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_sig_overflow");
}

bool RunIrTextLocalsBadHexTest() {
  const char* text =
      "func main locals=0xZZ stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_locals_bad_hex");
}

bool RunIrTextStackBadHexTest() {
  const char* text =
      "func main locals=0 stack=0xZZ\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_stack_bad_hex");
}

bool RunIrTextSigNegativeHexTest() {
  const char* text =
      "func main locals=0 stack=4 sig=-0x1\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_sig_negative_hex");
}

bool RunIrTextNegativeLocalsTest() {
  const char* text =
      "func main locals=-1 stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_negative_locals");
}

bool RunIrTextNegativeStackTest() {
  const char* text =
      "func main locals=0 stack=-4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_negative_stack");
}

bool RunIrTextLabelBeforeFuncTest() {
  const char* text =
      "label:\n"
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_label_before_func");
}

bool RunIrTextDuplicateLabelTest() {
  const char* text =
      "func main locals=0 stack=4\n"
      "  enter 0\n"
      "dup:\n"
      "  const.i32 1\n"
      "dup:\n"
      "  const.i32 2\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_duplicate_label");
}

bool RunIrTextJmpTableUnknownLabelTest() {
  const char* text =
      "func main locals=0 stack=6\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  jmptable 0 missing\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  return RunIrTextExpectFail(text, "ir_text_jmptable_unknown_label");
}

bool RunIrTextArrayGetOutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  array.get.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_get_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_get_oob");
}

bool RunIrTextArraySetI64OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.i64 9\n"
      "  array.set.i64\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_i64_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_i64_oob");
}

bool RunIrTextArraySetF32OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.f32 1.0\n"
      "  array.set.f32\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_f32_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_f32_oob");
}

bool RunIrTextArraySetF64OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.f64 2.0\n"
      "  array.set.f64\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_f64_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_f64_oob");
}

bool RunIrTextArraySetRefOutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.null\n"
      "  array.set.ref\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_ref_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_ref_oob");
}

bool RunIrTextArrayGetNegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  array.get.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_get_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_get_neg_idx");
}

bool RunIrTextArraySetI32NegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  const.i32 3\n"
      "  array.set.i32\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_i32_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_i32_neg_idx");
}

bool RunIrTextArraySetI64NegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  const.i64 3\n"
      "  array.set.i64\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_i64_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_i64_neg_idx");
}

bool RunIrTextArraySetF32NegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  const.f32 1.0\n"
      "  array.set.f32\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_f32_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_f32_neg_idx");
}

bool RunIrTextArraySetF64NegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  const.f64 1.0\n"
      "  array.set.f64\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_f64_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_f64_neg_idx");
}

bool RunIrTextArraySetRefNegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  const.null\n"
      "  array.set.ref\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_ref_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_array_set_ref_neg_idx");
}

bool RunIrTextListPopEmptyTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 2\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  list.pop.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_pop_empty");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_pop_empty");
}

bool RunIrTextListGetNegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 2\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  list.get.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_get_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_get_neg_idx");
}

bool RunIrTextListSetNegativeIndexTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 2\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 -1\n"
      "  const.i32 2\n"
      "  list.set.i32\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_set_neg_idx");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_set_neg_idx");
}

bool RunIrTextListInsertI32OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.i32 4\n"
      "  list.insert.i32\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_i32_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_insert_i32_oob");
}

bool RunIrTextListInsertI64OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.i64 4\n"
      "  list.insert.i64\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_i64_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_insert_i64_oob");
}

bool RunIrTextListInsertF32OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.f32 1.0\n"
      "  list.insert.f32\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_f32_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_insert_f32_oob");
}

bool RunIrTextListInsertF64OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.f64 1.0\n"
      "  list.insert.f64\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_f64_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_insert_f64_oob");
}

bool RunIrTextListInsertRefOutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  const.null\n"
      "  list.insert.ref\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_insert_ref_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_insert_ref_oob");
}

bool RunIrTextListRemoveI32OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  list.remove.i32\n"
      "  pop\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_remove_i32_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_remove_i32_oob");
}

bool RunIrTextListRemoveI64OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  list.remove.i64\n"
      "  pop\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_remove_i64_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_remove_i64_oob");
}

bool RunIrTextListRemoveF32OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  list.remove.f32\n"
      "  pop\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_remove_f32_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_remove_f32_oob");
}

bool RunIrTextListRemoveF64OutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  list.remove.f64\n"
      "  pop\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_remove_f64_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_remove_f64_oob");
}

bool RunIrTextListRemoveRefOutOfBoundsTrapTest() {
  const char* text =
      "func main locals=1 stack=8\n"
      "  enter 1\n"
      "  newlist 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 2\n"
      "  list.remove.ref\n"
      "  pop\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_remove_ref_oob");
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_list_remove_ref_oob");
}

bool RunIrTextStringGetCharOobTrapTest() {
  std::vector<uint8_t> const_pool;
  uint32_t str_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hi"));
  uint32_t str_id = 0;
  AppendConstString(const_pool, str_off, &str_id);

  std::string text = "func main locals=0 stack=8\n";
  text += "  enter 0\n";
  text += "  const.string " + std::to_string(str_id) + "\n";
  text += "  const.i32 5\n";
  text += "  string.get.char\n";
  text += "  ret\n";
  text += "end\n";
  text += "entry main\n";

  auto module = BuildIrTextModuleWithTables(text, "ir_text_string_get_char_oob",
                                            {}, {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_string_get_char_oob");
}

bool RunIrTextStringSliceOobTrapTest() {
  std::vector<uint8_t> const_pool;
  uint32_t str_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "hello"));
  uint32_t str_id = 0;
  AppendConstString(const_pool, str_off, &str_id);

  std::string text = "func main locals=0 stack=10\n";
  text += "  enter 0\n";
  text += "  const.string " + std::to_string(str_id) + "\n";
  text += "  const.i32 2\n";
  text += "  const.i32 99\n";
  text += "  string.slice\n";
  text += "  pop\n";
  text += "  const.i32 0\n";
  text += "  ret\n";
  text += "end\n";
  text += "entry main\n";

  auto module = BuildIrTextModuleWithTables(text, "ir_text_string_slice_oob",
                                            {}, {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_string_slice_oob");
}

bool RunIrTextListClearTest() {
  const char* text =
      "func main locals=1 stack=10\n"
      "  enter 1\n"
      "  newlist 0 4\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 1\n"
      "  list.push.i32\n"
      "  ldloc 0\n"
      "  list.clear\n"
      "  ldloc 0\n"
      "  list.len\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_list_clear");
  if (module.empty()) return false;
  return RunExpectExit(module, 0);
}

bool RunIrTextCallArgsTest() {
  const char* text =
      "func add locals=2 stack=8 sig=0\n"
      "  enter 2\n"
      "  ldloc 0\n"
      "  ldloc 1\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=8 sig=1\n"
      "  enter 0\n"
      "  const.i32 4\n"
      "  const.i32 5\n"
      "  call 0 2\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 2;
  sig0.param_types = {0, 0};
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_call_args", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectExit(module, 9);
}

bool RunIrTextCallIndirectArgsTest() {
  const char* text =
      "func callee locals=2 stack=8 sig=0\n"
      "  enter 2\n"
      "  ldloc 0\n"
      "  ldloc 1\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "func main locals=1 stack=10 sig=1\n"
      "  enter 1\n"
      "  newclosure 0 0\n"
      "  stloc 0\n"
      "  const.i32 6\n"
      "  const.i32 7\n"
      "  ldloc 0\n"
      "  call.indirect 0 2\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 2;
  sig0.param_types = {0, 0};
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_call_indirect_args", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectExit(module, 13);
}

bool RunIrTextStoreUpvalueTest() {
  const char* text =
      "func callee locals=0 stack=10 sig=0\n"
      "  enter 0\n"
      "  newobj 1\n"
      "  stupv 0\n"
      "  ldupv 0\n"
      "  isnull\n"
      "  bool.not\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  const.null\n"
      "  newclosure 0 1\n"
      "  call.indirect 0 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_store_upvalue",
                                            std::move(types), {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextNamedUpvalueTest() {
  const char* text =
      "func callee locals=0 stack=10 sig=0\n"
      "  upvalues: uv\n"
      "  enter 0\n"
      "  newobj 1\n"
      "  stupv uv\n"
      "  ldupv uv\n"
      "  isnull\n"
      "  bool.not\n"
      "  jmp.true ok\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "ok:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  const.null\n"
      "  newclosure 0 1\n"
      "  call.indirect 0 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::Unspecified));
  AppendU8(types, 1);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  auto module = BuildIrTextModuleWithTables(text, "ir_text_named_upvalue",
                                            std::move(types), {}, std::move(const_pool));
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextTailCallArgsTest() {
  const char* text =
      "func add locals=2 stack=8 sig=0\n"
      "  enter 2\n"
      "  ldloc 0\n"
      "  ldloc 1\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=8 sig=1\n"
      "  enter 0\n"
      "  const.i32 2\n"
      "  const.i32 7\n"
      "  tailcall 0 2\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 2;
  sig0.param_types = {0, 0};
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_tailcall_args", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectExit(module, 9);
}

bool RunIrTextStoreUpvalueTypeMismatchTest() {
  const char* text =
      "func callee locals=0 stack=8 sig=0\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  stupv 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=8 sig=1\n"
      "  enter 0\n"
      "  const.null\n"
      "  newclosure 0 1\n"
      "  call.indirect 0 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 0;
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_stupv_type_mismatch", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_stupv_type_mismatch");
}

bool RunIrTextCallBadArgCountTest() {
  const char* text =
      "func add locals=2 stack=8 sig=0\n"
      "  enter 2\n"
      "  ldloc 0\n"
      "  ldloc 1\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=8 sig=1\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  call 0 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 2;
  sig0.param_types = {0, 0};
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_call_bad_arg_count", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_call_bad_arg_count");
}

bool RunIrTextCallIndirectBadArgCountTest() {
  const char* text =
      "func callee locals=2 stack=8 sig=0\n"
      "  enter 2\n"
      "  ldloc 0\n"
      "  ldloc 1\n"
      "  add.i32\n"
      "  ret\n"
      "end\n"
      "func main locals=1 stack=10 sig=1\n"
      "  enter 1\n"
      "  newclosure 0 0\n"
      "  stloc 0\n"
      "  const.i32 2\n"
      "  const.i32 3\n"
      "  ldloc 0\n"
      "  call.indirect 0 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 2;
  sig0.param_types = {0, 0};
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_call_indirect_bad_arg_count", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_call_indirect_bad_arg_count");
}

bool RunIrTextGlobalInitStringTest() {
  std::vector<uint8_t> const_pool;
  uint32_t str_off = static_cast<uint32_t>(AppendStringToPool(const_pool, "ok"));
  uint32_t str_const = 0;
  AppendConstString(const_pool, str_off, &str_const);

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
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> globals;
  AppendU32(globals, 0);
  AppendU32(globals, 1);
  AppendU32(globals, 0);
  AppendU32(globals, str_const);

  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  ldglob 0\n"
      "  string.len\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModuleWithTablesAndGlobals(text, "ir_text_global_init_string",
                                                      std::move(types), {}, std::move(const_pool),
                                                      std::move(globals));
  if (module.empty()) return false;
  return RunExpectExit(module, 2);
}

bool RunIrTextGlobalInitF32Test() {
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 3);
  AppendF32(const_pool, 4.5f);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::F32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> globals;
  AppendU32(globals, 0);
  AppendU32(globals, 1);
  AppendU32(globals, 0);
  AppendU32(globals, const_id);

  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  ldglob 0\n"
      "  conv.f32.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModuleWithTablesAndGlobals(text, "ir_text_global_init_f32",
                                                      std::move(types), {}, std::move(const_pool),
                                                      std::move(globals));
  if (module.empty()) return false;
  return RunExpectExit(module, 4);
}

bool RunIrTextGlobalInitF64Test() {
  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 4);
  AppendF64(const_pool, 6.0);

  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::F64));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 8);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> globals;
  AppendU32(globals, 0);
  AppendU32(globals, 1);
  AppendU32(globals, 0);
  AppendU32(globals, const_id);

  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  ldglob 0\n"
      "  conv.f64.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModuleWithTablesAndGlobals(text, "ir_text_global_init_f64",
                                                      std::move(types), {}, std::move(const_pool),
                                                      std::move(globals));
  if (module.empty()) return false;
  return RunExpectExit(module, 6);
}

bool RunIrTextCallParamTypeMismatchTest() {
  const char* text =
      "func callee locals=1 stack=6 sig=0\n"
      "  enter 1\n"
      "  ldloc 0\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=6 sig=1\n"
      "  enter 0\n"
      "  const.bool 1\n"
      "  call 0 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 1;
  sig0.param_types = {0};
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_call_param_type_mismatch", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_call_param_type_mismatch");
}

bool RunIrTextCallParamI8TypeMismatchTest() {
  const char* text =
      "func callee locals=1 stack=6 sig=0\n"
      "  enter 1\n"
      "  ldloc 0\n"
      "  ret\n"
      "end\n"
      "func main locals=0 stack=6 sig=1\n"
      "  enter 0\n"
      "  const.i8 7\n"
      "  call 0 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 1;
  sig0.param_types = {0};
  Simple::Byte::sbc::SigSpec sig1;
  sig1.ret_type_id = 0;
  sig1.param_count = 0;
  auto module =
      BuildIrTextModuleWithSigs(text, "ir_text_call_param_i8_type_mismatch", {sig0, sig1});
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_call_param_i8_type_mismatch");
}

bool RunIrTextCmpMixedSmallTypesTest() {
  const char* text =
      "func main locals=0 stack=8\n"
      "  enter 0\n"
      "  const.i8 -1\n"
      "  const.i16 -1\n"
      "  cmp.eq.i32\n"
      "  const.u8 255\n"
      "  const.u16 255\n"
      "  cmp.eq.u32\n"
      "  bool.and\n"
      "  jmp.true is_true\n"
      "  const.i32 0\n"
      "  jmp done\n"
      "is_true:\n"
      "  const.i32 1\n"
      "done:\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_cmp_mixed_small_types");
  if (module.empty()) return false;
  return RunExpectExit(module, 1);
}

bool RunIrTextArraySetI32CharTest() {
  const char* text =
      "func main locals=1 stack=10\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.char 65\n"
      "  array.set.i32\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  array.get.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_i32_char");
  if (module.empty()) return false;
  return RunExpectExit(module, 65);
}

bool RunIrTextArraySetI32BoolTypeMismatchTest() {
  const char* text =
      "func main locals=1 stack=10\n"
      "  enter 1\n"
      "  newarray 0 1\n"
      "  stloc 0\n"
      "  ldloc 0\n"
      "  const.i32 0\n"
      "  const.bool 1\n"
      "  array.set.i32\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_array_set_i32_bool_type_mismatch");
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_array_set_i32_bool_type_mismatch");
}

bool RunIrTextConvTypeMismatchTest() {
  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  conv.f32.i32\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_conv_type_mismatch");
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_conv_type_mismatch");
}

bool RunIrTextCallIndirectBadSigIdTest() {
  const char* text =
      "func main locals=0 stack=6 sig=0\n"
      "  enter 0\n"
      "  const.null\n"
      "  call.indirect 5 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModule(text, "ir_text_call_indirect_bad_sig");
  if (module.empty()) return false;
  return RunExpectVerifyFail(module, "ir_text_call_indirect_bad_sig");
}

bool RunIrTextBadFuncSigIdTest() {
  const char* text =
      "func main locals=0 stack=4 sig=3\n"
      "  enter 0\n"
      "  const.i32 1\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  Simple::Byte::sbc::SigSpec sig0;
  sig0.ret_type_id = 0;
  sig0.param_count = 0;
  auto module = BuildIrTextModuleWithSigs(text, "ir_text_bad_func_sig", {sig0}, false);
  if (module.empty()) return true;
  return RunExpectVerifyFail(module, "ir_text_bad_func_sig");
}

bool RunIrTextGlobalInitUnsupportedConstTest() {
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);

  std::vector<uint8_t> const_pool;
  uint32_t const_id = static_cast<uint32_t>(const_pool.size());
  AppendU32(const_pool, 5);
  AppendU32(const_pool, 0);

  std::vector<uint8_t> globals;
  AppendU32(globals, 0);
  AppendU32(globals, 0);
  AppendU32(globals, 0);
  AppendU32(globals, const_id);

  const char* text =
      "func main locals=0 stack=4 sig=0\n"
      "  enter 0\n"
      "  const.i32 0\n"
      "  ret\n"
      "end\n"
      "entry main\n";
  auto module = BuildIrTextModuleWithTablesAndGlobals(text, "ir_text_global_init_unsupported_const",
                                                      std::move(types), {}, std::move(const_pool),
                                                      std::move(globals));
  if (module.empty()) return false;
  return RunExpectTrap(module, "ir_text_global_init_unsupported_const");
}

static const TestCase kIrTests[] = {
  {"ir_emit_add", RunIrEmitAddTest},
  {"ir_emit_jump", RunIrEmitJumpTest},
  {"ir_emit_jmp_table", RunIrEmitJmpTableTest},
  {"ir_emit_stack_ops2", RunIrEmitStackOps2Test},
  {"ir_emit_i64_bitwise", RunIrEmitI64BitwiseTest},
  {"ir_emit_const_small", RunIrEmitConstSmallTest},
  {"ir_emit_inc_dec_neg", RunIrEmitIncDecNegTest},
  {"ir_emit_inc_dec_neg_wide", RunIrEmitIncDecNegWideTest},
  {"ir_emit_list_insert_remove_i64", RunIrEmitListInsertRemoveI64Test},
  {"ir_emit_u32_arith", RunIrEmitU32ArithTest},
  {"ir_emit_cmp_variants", RunIrEmitCmpVariantsTest},
  {"ir_emit_u64_arith", RunIrEmitU64ArithTest},
  {"ir_emit_f64_cmp", RunIrEmitF64CmpTest},
  {"ir_emit_u64_cmp", RunIrEmitU64CmpTest},
  {"ir_emit_f32_arith2", RunIrEmitF32Arith2Test},
  {"ir_emit_f64_arith2", RunIrEmitF64Arith2Test},
  {"ir_emit_u32_arith2", RunIrEmitU32Arith2Test},
  {"ir_emit_u64_arith2", RunIrEmitU64Arith2Test},
  {"ir_emit_u32_cmp", RunIrEmitU32CmpTest},
  {"ir_emit_u64_cmp2", RunIrEmitU64Cmp2Test},
  {"ir_emit_f32_cmp", RunIrEmitF32CmpTest},
  {"ir_emit_f64_cmp2", RunIrEmitF64Cmp2Test},
  {"ir_emit_i64_arith", RunIrEmitI64ArithTest},
  {"ir_emit_u32_mod", RunIrEmitU32ModTest},
  {"ir_emit_u64_mod", RunIrEmitU64ModTest},
  {"ir_emit_i64_mul", RunIrEmitI64MulTest},
  {"ir_emit_i64_div", RunIrEmitI64DivTest},
  {"ir_emit_u32_arith3", RunIrEmitU32Arith3Test},
  {"ir_emit_u64_div2", RunIrEmitU64Div2Test},
  {"ir_emit_u32_div", RunIrEmitU32DivTest},
  {"ir_emit_u64_add", RunIrEmitU64AddTest},
  {"ir_emit_f32_sub", RunIrEmitF32SubTest},
  {"ir_emit_f64_sub", RunIrEmitF64SubTest},
  {"ir_emit_u32_mul", RunIrEmitU32MulTest},
  {"ir_emit_u64_sub", RunIrEmitU64SubTest},
  {"ir_emit_f32_mul", RunIrEmitF32MulTest},
  {"ir_emit_f64_div", RunIrEmitF64DivTest},
  {"ir_emit_i32_arith2", RunIrEmitI32Arith2Test},
  {"ir_emit_i64_add_sub", RunIrEmitI64AddSubTest},
  {"ir_emit_locals", RunIrEmitLocalsTest},
  {"ir_emit_call", RunIrEmitCallTest},
  {"ir_emit_callcheck", RunIrEmitCallCheckTest},
  {"ir_emit_intrinsic", RunIrEmitIntrinsicTest},
  {"ir_emit_syscall", RunIrEmitSysCallTest},
  {"ir_emit_new_closure", RunIrEmitNewClosureTest},
  {"ir_emit_upvalue", RunIrEmitUpvalueTest},
  {"ir_emit_globals", RunIrEmitGlobalsTest},
  {"ir_emit_stack_ops", RunIrEmitStackOpsTest},
  {"ir_emit_branch", RunIrEmitBranchTest},
  {"ir_emit_compare", RunIrEmitCompareTest},
  {"ir_emit_bool", RunIrEmitBoolTest},
  {"ir_emit_conv_i32_i64", RunIrEmitConvI32I64Test},
  {"ir_emit_conv_i32_f64", RunIrEmitConvI32F64Test},
  {"ir_emit_conv_f32_f64", RunIrEmitConvF32F64Test},
  {"ir_emit_f32_arith", RunIrEmitF32ArithTest},
  {"ir_emit_bitwise_i32", RunIrEmitBitwiseI32Test},
  {"ir_emit_call_indirect", RunIrEmitCallIndirectTest},
  {"ir_emit_tailcall", RunIrEmitTailCallTest},
  {"ir_emit_array", RunIrEmitArrayTest},
  {"ir_emit_list", RunIrEmitListTest},
  {"ir_emit_string", RunIrEmitStringTest},
  {"ir_emit_string_get_char", RunIrEmitStringGetCharTest},
  {"ir_emit_string_slice", RunIrEmitStringSliceTest},
  {"ir_emit_ref_ops", RunIrEmitRefOpsTest},
  {"ir_emit_field", RunIrEmitFieldTest},
  {"ir_emit_typeof", RunIrEmitTypeOfTest},
  {"ir_emit_array_i64", RunIrEmitArrayI64Test},
  {"ir_emit_list_f32", RunIrEmitListF32Test},
  {"ir_emit_list_ref", RunIrEmitListRefTest},
  {"ir_emit_array_f64", RunIrEmitArrayF64Test},
  {"ir_emit_array_ref", RunIrEmitArrayRefTest},
  {"ir_emit_list_f64", RunIrEmitListF64Test},
  {"ir_emit_array_f32", RunIrEmitArrayF32Test},
  {"ir_emit_list_i64", RunIrEmitListI64Test},
  {"ir_emit_array_len", RunIrEmitArrayLenTest},
  {"ir_emit_list_len", RunIrEmitListLenTest},
  {"ir_emit_list_insert_remove", RunIrEmitListInsertRemoveTest},
  {"ir_emit_list_clear", RunIrEmitListClearTest},
  {"ir_emit_list_get_set", RunIrEmitListGetSetTest},
  {"ir_emit_array_get_set_f32", RunIrEmitArrayGetSetF32Test},
  {"ir_emit_array_get_set_ref", RunIrEmitArrayGetSetRefTest},
  {"ir_emit_list_get_set_f32", RunIrEmitListGetSetF32Test},
  {"ir_emit_list_get_set_ref", RunIrEmitListGetSetRefTest},
  {"ir_emit_list_get_set_i64", RunIrEmitListGetSetI64Test},
  {"ir_emit_list_get_set_f64", RunIrEmitListGetSetF64Test},
  {"ir_text_add", RunIrTextAddTest},
  {"ir_text_small_type_ops", RunIrTextSmallTypeOpsTest},
  {"ir_text_unsigned_wide_ops", RunIrTextUnsignedWideOpsTest},
  {"ir_text_float_inc_dec", RunIrTextFloatIncDecTest},
  {"ir_text_branch", RunIrTextBranchTest},
  {"ir_text_locals", RunIrTextLocalsTest},
  {"ir_text_bitwise_bool", RunIrTextBitwiseBoolTest},
  {"ir_text_intrinsic_trap", RunIrTextIntrinsicTrapTest},
  {"ir_text_syscall_verify_fail", RunIrTextSysCallVerifyFailTest},
  {"ir_text_syscall_missing_id", RunIrTextSysCallMissingIdTest},
  {"ir_text_const_bool", RunIrTextConstBoolTest},
  {"ir_text_const_char", RunIrTextConstCharTest},
  {"ir_text_array_len", RunIrTextArrayLenTest},
  {"ir_text_bool_and_or", RunIrTextBoolAndOrTest},
  {"ir_text_cmp_unsigned", RunIrTextCmpUnsignedTest},
  {"ir_text_callcheck", RunIrTextCallCheckTest},
  {"ir_text_array_i32", RunIrTextArrayI32Test},
  {"ir_text_list_i32", RunIrTextListI32Test},
  {"ir_text_object_field", RunIrTextObjectFieldTest},
  {"ir_text_named_tables", RunIrTextNamedTablesTest},
  {"ir_text_bad_type_name", RunIrTextBadTypeNameTest},
  {"ir_text_bad_field_name", RunIrTextBadFieldNameTest},
  {"ir_text_field_misaligned", RunIrTextFieldMisalignedTest},
  {"ir_text_field_oob", RunIrTextFieldOutOfBoundsTest},
  {"ir_text_bad_const_name", RunIrTextBadConstNameTest},
  {"ir_text_lower_line_number", RunIrTextLowerLineNumberTest},
  {"ir_text_local_type_name", RunIrTextLocalTypeNameTest},
  {"ir_text_local_type_bad_name", RunIrTextLocalTypeBadNameTest},
  {"ir_text_upvalue_type_bad_name", RunIrTextUpvalueTypeBadNameTest},
  {"ir_text_syscall_name_fail", RunIrTextSyscallNameFailTest},
  {"ir_text_string_len", RunIrTextStringLenTest},
  {"ir_text_bad_operand", RunIrTextBadOperandTest},
  {"ir_text_unknown_op", RunIrTextUnknownOpTest},
  {"ir_text_global", RunIrTextGlobalTest},
  {"ir_text_named_globals", RunIrTextNamedGlobalsTest},
  {"ir_text_named_globals_init", RunIrTextNamedGlobalsInitTest},
  {"ir_text_named_globals_bad_name", RunIrTextNamedGlobalsBadNameTest},
  {"ir_text_unknown_label", RunIrTextUnknownLabelTest},
  {"ir_text_jmptable_unknown_label", RunIrTextJmpTableUnknownLabelTest},
  {"ir_text_ref_null", RunIrTextRefNullTest},
  {"ir_text_typeof", RunIrTextTypeOfTest},
  {"ir_text_closure_upvalue", RunIrTextClosureUpvalueTest},
  {"ir_text_bad_newclosure", RunIrTextBadNewClosureTest},
  {"ir_text_string_concat", RunIrTextStringConcatTest},
  {"ir_text_string_get_char", RunIrTextStringGetCharTest},
  {"ir_text_string_slice", RunIrTextStringSliceTest},
  {"ir_text_array_i64", RunIrTextArrayI64Test},
  {"ir_text_array_f32", RunIrTextArrayF32Test},
  {"ir_text_array_f64", RunIrTextArrayF64Test},
  {"ir_text_array_ref", RunIrTextArrayRefTest},
  {"ir_text_list_i64", RunIrTextListI64Test},
  {"ir_text_list_f32", RunIrTextListF32Test},
  {"ir_text_list_f64", RunIrTextListF64Test},
  {"ir_text_list_ref", RunIrTextListRefTest},
  {"ir_text_list_insert_remove", RunIrTextListInsertRemoveTest},
  {"ir_text_list_insert_remove_f32", RunIrTextListInsertRemoveF32Test},
  {"ir_text_list_insert_remove_i64", RunIrTextListInsertRemoveI64Test},
  {"ir_text_list_insert_remove_ref", RunIrTextListInsertRemoveRefTest},
  {"ir_text_list_insert_remove_f64", RunIrTextListInsertRemoveF64Test},
  {"ir_text_conv_chain", RunIrTextConvChainTest},
  {"ir_text_bitwise_i32", RunIrTextBitwiseI32Test},
  {"ir_text_bitwise_i64", RunIrTextBitwiseI64Test},
  {"ir_text_shift_i32", RunIrTextShiftI32Test},
  {"ir_text_shift_i64", RunIrTextShiftI64Test},
  {"ir_text_compare_i32", RunIrTextCompareI32Test},
  {"ir_text_compare_u64", RunIrTextCompareU64Test},
  {"ir_text_cmp_mixed_small_types", RunIrTextCmpMixedSmallTypesTest},
  {"ir_text_bool_ops", RunIrTextBoolOpsTest},
  {"ir_text_ref_ops", RunIrTextRefOpsTest},
  {"ir_text_bool_type_mismatch", RunIrTextBoolTypeMismatchTest},
  {"ir_text_cmp_type_mismatch", RunIrTextCompareTypeMismatchTest},
  {"ir_text_shift_type_mismatch", RunIrTextShiftTypeMismatchTest},
  {"ir_text_list_insert_type_mismatch", RunIrTextListInsertTypeMismatchTest},
  {"ir_text_array_set_type_mismatch", RunIrTextArraySetTypeMismatchTest},
  {"ir_text_array_set_i32_char", RunIrTextArraySetI32CharTest},
  {"ir_text_array_set_i32_bool_type_mismatch", RunIrTextArraySetI32BoolTypeMismatchTest},
  {"ir_text_call_arg_count_mismatch", RunIrTextCallArgCountMismatchTest},
  {"ir_text_call_indirect_arg_count_mismatch", RunIrTextCallIndirectArgCountMismatchTest},
  {"ir_text_jmp_non_bool_cond", RunIrTextJmpNonBoolCondTest},
  {"ir_text_array_get_non_ref", RunIrTextArrayGetNonRefTest},
  {"ir_text_list_get_non_ref", RunIrTextListGetNonRefTest},
  {"ir_text_call_indirect_bad_sig_id", RunIrTextCallIndirectBadSigIdTextTest},
  {"ir_text_jmptable_missing_label", RunIrTextJmpTableMissingLabelTest},
  {"ir_text_bad_locals_count", RunIrTextBadLocalsCountTest},
  {"ir_text_const_u128_unsupported", RunIrTextConstU128UnsupportedTest},
  {"ir_text_const_i128_bad_token", RunIrTextConstI128BadTokenTest},
  {"ir_text_const_u64_bad_token", RunIrTextConstU64BadTokenTest},
  {"ir_text_const_i32_bad_token", RunIrTextConstI32BadTokenTest},
  {"ir_text_const_f64_bad_token", RunIrTextConstF64BadTokenTest},
  {"ir_text_const_f32_nan", RunIrTextConstF32NanTest},
  {"ir_text_const_f32_inf", RunIrTextConstF32InfTest},
  {"ir_text_const_f64_inf", RunIrTextConstF64InfTest},
  {"ir_text_const_f64_neg_inf", RunIrTextConstF64NegInfTest},
  {"ir_text_const_u32_negative", RunIrTextConstU32NegativeTest},
  {"ir_text_const_i32_overflow", RunIrTextConstI32OverflowTest},
  {"ir_text_const_i32_underflow", RunIrTextConstI32UnderflowTest},
  {"ir_text_const_u32_overflow", RunIrTextConstU32OverflowTest},
  {"ir_text_const_i8_overflow", RunIrTextConstI8OverflowTest},
  {"ir_text_const_u8_overflow", RunIrTextConstU8OverflowTest},
  {"ir_text_const_i16_overflow", RunIrTextConstI16OverflowTest},
  {"ir_text_const_u16_overflow", RunIrTextConstU16OverflowTest},
  {"ir_text_const_i64_overflow", RunIrTextConstI64OverflowTest},
  {"ir_text_const_i64_underflow", RunIrTextConstI64UnderflowTest},
  {"ir_text_const_u64_overflow", RunIrTextConstU64OverflowTest},
  {"ir_text_const_u32_bad_hex", RunIrTextConstU32BadHexTest},
  {"ir_text_const_i32_bad_hex", RunIrTextConstI32BadHexTest},
  {"ir_text_const_u32_negative_hex", RunIrTextConstU32NegativeHexTest},
  {"ir_text_call_indirect_missing_value", RunIrTextCallIndirectMissingValueTest},
  {"ir_text_call_indirect_non_ref_value", RunIrTextCallIndirectNonRefValueTest},
  {"ir_text_newarray_missing_len", RunIrTextNewArrayMissingLenTest},
  {"ir_text_newlist_missing_cap", RunIrTextNewListMissingCapTest},
  {"ir_text_enter_missing_count", RunIrTextEnterMissingCountTest},
  {"ir_text_call_missing_args", RunIrTextCallMissingArgsTest},
  {"ir_text_jmp_missing_label", RunIrTextJmpMissingLabelTest},
  {"ir_text_jmp_extra_operand", RunIrTextJmpExtraOperandTest},
  {"ir_text_call_extra_operand", RunIrTextCallExtraOperandTest},
  {"ir_text_call_indirect_extra_operand", RunIrTextCallIndirectExtraOperandTest},
  {"ir_text_jmptable_missing_default", RunIrTextJmpTableMissingDefaultTest},
  {"ir_text_unknown_op_caps", RunIrTextUnknownOpCapsTest},
  {"ir_text_missing_entry", RunIrTextMissingEntryTest},
  {"ir_text_duplicate_entry", RunIrTextDuplicateEntryTest},
  {"ir_text_bad_func_header", RunIrTextBadFuncHeaderTest},
  {"ir_text_bad_sig_token", RunIrTextBadSigTokenTest},
  {"ir_text_invalid_label_name", RunIrTextInvalidLabelNameTest},
  {"ir_text_label_starts_with_digit", RunIrTextLabelStartsWithDigitTest},
  {"ir_text_jmp_invalid_label", RunIrTextJmpInvalidLabelTokenTest},
  {"ir_text_jmptable_invalid_label", RunIrTextJmpTableInvalidLabelTokenTest},
  {"ir_text_entry_unknown_func", RunIrTextEntryUnknownFuncTest},
  {"ir_text_duplicate_func", RunIrTextDuplicateFuncTest},
  {"ir_text_bad_locals_token", RunIrTextBadLocalsTokenTest},
  {"ir_text_bad_stack_token", RunIrTextBadStackTokenTest},
  {"ir_text_locals_overflow", RunIrTextLocalsOverflowTest},
  {"ir_text_stack_overflow", RunIrTextStackOverflowTest},
  {"ir_text_sig_overflow", RunIrTextSigOverflowTest},
  {"ir_text_locals_bad_hex", RunIrTextLocalsBadHexTest},
  {"ir_text_stack_bad_hex", RunIrTextStackBadHexTest},
  {"ir_text_sig_negative_hex", RunIrTextSigNegativeHexTest},
  {"ir_text_negative_locals", RunIrTextNegativeLocalsTest},
  {"ir_text_negative_stack", RunIrTextNegativeStackTest},
  {"ir_text_label_before_func", RunIrTextLabelBeforeFuncTest},
  {"ir_text_duplicate_label", RunIrTextDuplicateLabelTest},
  {"ir_text_jmptable_unknown_label", RunIrTextJmpTableUnknownLabelTest},
  {"ir_text_array_get_oob", RunIrTextArrayGetOutOfBoundsTrapTest},
  {"ir_text_array_set_i64_oob", RunIrTextArraySetI64OutOfBoundsTrapTest},
  {"ir_text_array_set_f32_oob", RunIrTextArraySetF32OutOfBoundsTrapTest},
  {"ir_text_array_set_f64_oob", RunIrTextArraySetF64OutOfBoundsTrapTest},
  {"ir_text_array_set_ref_oob", RunIrTextArraySetRefOutOfBoundsTrapTest},
  {"ir_text_array_get_neg_idx", RunIrTextArrayGetNegativeIndexTrapTest},
  {"ir_text_array_set_i32_neg_idx", RunIrTextArraySetI32NegativeIndexTrapTest},
  {"ir_text_array_set_i64_neg_idx", RunIrTextArraySetI64NegativeIndexTrapTest},
  {"ir_text_array_set_f32_neg_idx", RunIrTextArraySetF32NegativeIndexTrapTest},
  {"ir_text_array_set_f64_neg_idx", RunIrTextArraySetF64NegativeIndexTrapTest},
  {"ir_text_array_set_ref_neg_idx", RunIrTextArraySetRefNegativeIndexTrapTest},
  {"ir_text_list_pop_empty", RunIrTextListPopEmptyTrapTest},
  {"ir_text_list_get_neg_idx", RunIrTextListGetNegativeIndexTrapTest},
  {"ir_text_list_set_neg_idx", RunIrTextListSetNegativeIndexTrapTest},
  {"ir_text_list_insert_i32_oob", RunIrTextListInsertI32OutOfBoundsTrapTest},
  {"ir_text_list_insert_i64_oob", RunIrTextListInsertI64OutOfBoundsTrapTest},
  {"ir_text_list_insert_f32_oob", RunIrTextListInsertF32OutOfBoundsTrapTest},
  {"ir_text_list_insert_f64_oob", RunIrTextListInsertF64OutOfBoundsTrapTest},
  {"ir_text_list_insert_ref_oob", RunIrTextListInsertRefOutOfBoundsTrapTest},
  {"ir_text_list_remove_i32_oob", RunIrTextListRemoveI32OutOfBoundsTrapTest},
  {"ir_text_list_remove_i64_oob", RunIrTextListRemoveI64OutOfBoundsTrapTest},
  {"ir_text_list_remove_f32_oob", RunIrTextListRemoveF32OutOfBoundsTrapTest},
  {"ir_text_list_remove_f64_oob", RunIrTextListRemoveF64OutOfBoundsTrapTest},
  {"ir_text_list_remove_ref_oob", RunIrTextListRemoveRefOutOfBoundsTrapTest},
  {"ir_text_string_get_char_oob", RunIrTextStringGetCharOobTrapTest},
  {"ir_text_string_slice_oob", RunIrTextStringSliceOobTrapTest},
  {"ir_text_stack_underflow", RunIrTextStackUnderflowTest},
  {"ir_text_jump_to_end", RunIrTextJumpToEndTest},
  {"ir_text_jump_mid_instruction", RunIrTextJumpMidInstructionTest},
  {"ir_text_jmptable_arity_mismatch", RunIrTextJmpTableArityMismatchTest},
  {"ir_text_jmptable_non_i32_index", RunIrTextJmpTableNonI32IndexTest},
  {"ir_text_const_i128_unsupported", RunIrTextConstI128UnsupportedTest},
  {"ir_text_const_string_missing_pool", RunIrTextConstStringMissingPoolTest},
  {"ir_text_call_missing_sig", RunIrTextCallMissingSigTest},
  {"ir_text_list_clear", RunIrTextListClearTest},
  {"ir_text_call_args", RunIrTextCallArgsTest},
  {"ir_text_call_indirect_args", RunIrTextCallIndirectArgsTest},
  {"ir_text_store_upvalue", RunIrTextStoreUpvalueTest},
  {"ir_text_named_upvalue", RunIrTextNamedUpvalueTest},
  {"ir_text_tailcall_args", RunIrTextTailCallArgsTest},
  {"ir_text_stupv_type_mismatch", RunIrTextStoreUpvalueTypeMismatchTest},
  {"ir_text_call_bad_arg_count", RunIrTextCallBadArgCountTest},
  {"ir_text_call_indirect_bad_arg_count", RunIrTextCallIndirectBadArgCountTest},
  {"ir_text_global_init_string", RunIrTextGlobalInitStringTest},
  {"ir_text_global_init_f32", RunIrTextGlobalInitF32Test},
  {"ir_text_global_init_f64", RunIrTextGlobalInitF64Test},
  {"ir_text_call_param_type_mismatch", RunIrTextCallParamTypeMismatchTest},
  {"ir_text_call_param_i8_type_mismatch", RunIrTextCallParamI8TypeMismatchTest},
  {"ir_text_conv_type_mismatch", RunIrTextConvTypeMismatchTest},
  {"ir_text_call_indirect_bad_sig", RunIrTextCallIndirectBadSigIdTest},
  {"ir_text_bad_func_sig", RunIrTextBadFuncSigIdTest},
  {"ir_text_global_init_unsupported_const", RunIrTextGlobalInitUnsupportedConstTest},
};

static const TestSection kIrSections[] = {
  {"ir", kIrTests, sizeof(kIrTests) / sizeof(kIrTests[0])},
};

const TestSection* GetIrSections(size_t* count) {
  if (count) {
    *count = sizeof(kIrSections) / sizeof(kIrSections[0]);
  }
  return kIrSections;
}

} // namespace Simple::VM::Tests
