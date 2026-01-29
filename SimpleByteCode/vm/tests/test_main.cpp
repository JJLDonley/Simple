#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "opcode.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "vm.h"

namespace {

void AppendU8(std::vector<uint8_t>& out, uint8_t v) {
  out.push_back(v);
}

void AppendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendI32(std::vector<uint8_t>& out, int32_t v) {
  AppendU32(out, static_cast<uint32_t>(v));
}

void WriteU8(std::vector<uint8_t>& out, size_t offset, uint8_t v) {
  out[offset] = v;
}

void WriteU16(std::vector<uint8_t>& out, size_t offset, uint16_t v) {
  out[offset + 0] = static_cast<uint8_t>(v & 0xFF);
  out[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void WriteU32(std::vector<uint8_t>& out, size_t offset, uint32_t v) {
  out[offset + 0] = static_cast<uint8_t>(v & 0xFF);
  out[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  out[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  out[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void PatchRel32(std::vector<uint8_t>& out, size_t operand_offset, size_t target_offset) {
  size_t next_pc = operand_offset + 4;
  int32_t rel = static_cast<int32_t>(static_cast<int64_t>(target_offset) - static_cast<int64_t>(next_pc));
  WriteU32(out, operand_offset, static_cast<uint32_t>(rel));
}

size_t Align4(size_t v) {
  return (v + 3u) & ~static_cast<size_t>(3u);
}

struct SectionData {
  uint32_t id = 0;
  std::vector<uint8_t> bytes;
  uint32_t count = 0;
  uint32_t offset = 0;
};

std::vector<uint8_t> BuildModule(const std::vector<uint8_t>& code, uint32_t global_count, uint16_t local_count) {
  std::vector<uint8_t> types;
  AppendU32(types, 0);       // name_str
  AppendU8(types, 0);        // kind
  AppendU8(types, 0);        // flags
  AppendU16(types, 0);       // reserved
  AppendU32(types, 4);       // size
  AppendU32(types, 0);       // field_start
  AppendU32(types, 0);       // field_count

  std::vector<uint8_t> methods;
  AppendU32(methods, 0);     // name_str
  AppendU32(methods, 0);     // sig_id
  AppendU32(methods, 0);     // code_offset
  AppendU16(methods, local_count);
  AppendU16(methods, 0);     // flags

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);        // ret_type_id
  AppendU16(sigs, 0);        // param_count
  AppendU16(sigs, 0);        // call_conv
  AppendU32(sigs, 0);        // param_type_start

  std::vector<uint8_t> globals;
  for (uint32_t i = 0; i < global_count; ++i) {
    AppendU32(globals, 0);            // name_str
    AppendU32(globals, 0);            // type_id
    AppendU32(globals, 1);            // flags (mutable)
    AppendU32(globals, 0xFFFFFFFFu);  // init_const_id (zero-init)
  }

  std::vector<uint8_t> functions;
  AppendU32(functions, 0);   // method_id
  AppendU32(functions, 0);   // code_offset
  AppendU32(functions, static_cast<uint32_t>(code.size()));
  AppendU32(functions, 8);   // stack_max

  std::vector<SectionData> sections;
  sections.push_back({1, types, 1, 0});
  sections.push_back({2, {}, 0, 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 1, 0});
  sections.push_back({5, {}, 0, 0});
  sections.push_back({6, globals, global_count, 0});
  sections.push_back({7, functions, 1, 0});
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

  WriteU32(module, 0x00, 0x30434253u); // magic
  WriteU16(module, 0x04, 0x0001u);     // version
  WriteU8(module, 0x06, 1);            // endian
  WriteU8(module, 0x07, 0);            // flags
  WriteU32(module, 0x08, section_count);
  WriteU32(module, 0x0C, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);           // entry_method_id
  WriteU32(module, 0x14, 0);           // reserved0
  WriteU32(module, 0x18, 0);           // reserved1
  WriteU32(module, 0x1C, 0);           // reserved2

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

std::vector<uint8_t> BuildSimpleAddModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBoolModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildRefModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildArrayModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildListModule() {
  using simplevm::OpCode;
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
bool RunAddTest() {
  std::vector<uint8_t> module_bytes = BuildSimpleAddModule();
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
  if (exec.exit_code != 42) {
    std::cerr << "expected 42, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunGlobalTest() {
  std::vector<uint8_t> module_bytes = BuildGlobalModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunDupTest() {
  std::vector<uint8_t> module_bytes = BuildDupModule();
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
  if (exec.exit_code != 10) {
    std::cerr << "expected 10, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunSwapTest() {
  std::vector<uint8_t> module_bytes = BuildSwapModule();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunRotTest() {
  std::vector<uint8_t> module_bytes = BuildRotModule();
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
  if (exec.exit_code != 4) {
    std::cerr << "expected 4, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunPopTest() {
  std::vector<uint8_t> module_bytes = BuildPopModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunDup2Test() {
  std::vector<uint8_t> module_bytes = BuildDup2Module();
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
  if (exec.exit_code != 6) {
    std::cerr << "expected 6, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunModTest() {
  std::vector<uint8_t> module_bytes = BuildModModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBoolTest() {
  std::vector<uint8_t> module_bytes = BuildBoolModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunCmpTest() {
  std::vector<uint8_t> module_bytes = BuildCmpModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunBranchTest() {
  std::vector<uint8_t> module_bytes = BuildBranchModule();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunLocalTest() {
  std::vector<uint8_t> module_bytes = BuildLocalModule();
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
  if (exec.exit_code != 9) {
    std::cerr << "expected 9, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunLoopTest() {
  std::vector<uint8_t> module_bytes = BuildLoopModule();
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
  if (exec.exit_code != 3) {
    std::cerr << "expected 3, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunRefTest() {
  std::vector<uint8_t> module_bytes = BuildRefModule();
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
  if (exec.exit_code != 1) {
    std::cerr << "expected 1, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunArrayTest() {
  std::vector<uint8_t> module_bytes = BuildArrayModule();
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
  if (exec.exit_code != 7) {
    std::cerr << "expected 7, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListTest() {
  std::vector<uint8_t> module_bytes = BuildListModule();
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
  if (exec.exit_code != 11) {
    std::cerr << "expected 11, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  struct TestCase {
    const char* name;
    bool (*fn)();
  };

  const TestCase tests[] = {
      {"add_i32", RunAddTest},
      {"globals", RunGlobalTest},
      {"dup", RunDupTest},
      {"dup2", RunDup2Test},
      {"pop", RunPopTest},
      {"swap", RunSwapTest},
      {"rot", RunRotTest},
      {"mod_i32", RunModTest},
      {"bool_ops", RunBoolTest},
      {"cmp_i32", RunCmpTest},
      {"branch", RunBranchTest},
      {"locals", RunLocalTest},
      {"loop", RunLoopTest},
      {"ref_ops", RunRefTest},
      {"array_i32", RunArrayTest},
      {"list_i32", RunListTest},
  };

  int failures = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    bool ok = test.fn();
    if (!ok) {
      std::cout << "[  FAILED  ] " << test.name << "\n";
      failures++;
    } else {
      std::cout << "[       OK ] " << test.name << "\n";
    }
  }

  if (failures == 0) {
    std::cout << "all tests passed\n";
    return 0;
  }
  std::cout << failures << " tests failed\n";
  return 1;
}
