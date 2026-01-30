#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "heap.h"
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

void AppendU64(std::vector<uint8_t>& out, uint64_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
}

void AppendI32(std::vector<uint8_t>& out, int32_t v) {
  AppendU32(out, static_cast<uint32_t>(v));
}

void AppendI64(std::vector<uint8_t>& out, int64_t v) {
  AppendU64(out, static_cast<uint64_t>(v));
}

size_t AppendStringToPool(std::vector<uint8_t>& pool, const std::string& text) {
  size_t offset = pool.size();
  pool.insert(pool.end(), text.begin(), text.end());
  pool.push_back('\0');
  return offset;
}

void AppendConstString(std::vector<uint8_t>& pool, uint32_t str_offset, uint32_t* out_const_id) {
  uint32_t const_id = static_cast<uint32_t>(pool.size());
  AppendU32(pool, 0); // STRING kind
  AppendU32(pool, str_offset);
  *out_const_id = const_id;
}

void AppendConstBlob(std::vector<uint8_t>& pool, uint32_t kind, const std::vector<uint8_t>& blob, uint32_t* out_const_id) {
  uint32_t const_id = static_cast<uint32_t>(pool.size());
  AppendU32(pool, kind);
  uint32_t blob_offset = static_cast<uint32_t>(pool.size() + 4);
  AppendU32(pool, blob_offset);
  AppendU32(pool, static_cast<uint32_t>(blob.size()));
  pool.insert(pool.end(), blob.begin(), blob.end());
  *out_const_id = const_id;
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

uint32_t ReadU32At(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
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

std::vector<uint8_t> BuildModuleWithTablesAndSig(const std::vector<uint8_t>& code,
                                                 const std::vector<uint8_t>& const_pool,
                                                 const std::vector<uint8_t>& types_bytes,
                                                 const std::vector<uint8_t>& fields_bytes,
                                                 uint32_t global_count,
                                                 uint16_t local_count,
                                                 uint32_t ret_type_id,
                                                 uint16_t param_count,
                                                 uint16_t call_conv,
                                                 uint32_t param_type_start,
                                                 const std::vector<uint32_t>& param_types) {
  std::vector<uint8_t> types = types_bytes;
  if (types.empty()) {
    AppendU32(types, 0);       // name_str
    AppendU8(types, 0);        // kind
    AppendU8(types, 0);        // flags
    AppendU16(types, 0);       // reserved
    AppendU32(types, 4);       // size
    AppendU32(types, 0);       // field_start
    AppendU32(types, 0);       // field_count
  }

  std::vector<uint8_t> fields = fields_bytes;

  std::vector<uint8_t> methods;
  AppendU32(methods, 0);     // name_str
  AppendU32(methods, 0);     // sig_id
  AppendU32(methods, 0);     // code_offset
  AppendU16(methods, local_count);
  AppendU16(methods, 0);     // flags

  std::vector<uint8_t> sigs;
  AppendU32(sigs, ret_type_id);
  AppendU16(sigs, param_count);
  AppendU16(sigs, call_conv);
  AppendU32(sigs, param_type_start);
  if (!param_types.empty() || param_type_start > 0) {
    std::vector<uint32_t> packed = param_types;
    if (param_type_start > 0) {
      packed.insert(packed.begin(), param_type_start, 0);
    }
    for (uint32_t type_id : packed) {
      AppendU32(sigs, type_id);
    }
  }

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
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 1, 0});
  sections.push_back({5, const_pool, 0, 0});
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

std::vector<uint8_t> BuildModuleWithTables(const std::vector<uint8_t>& code,
                                           const std::vector<uint8_t>& const_pool,
                                           const std::vector<uint8_t>& types_bytes,
                                           const std::vector<uint8_t>& fields_bytes,
                                           uint32_t global_count,
                                           uint16_t local_count) {
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, types_bytes, fields_bytes, global_count, local_count,
                                     0, 0, 0, 0, empty_params);
}

std::vector<uint8_t> BuildModule(const std::vector<uint8_t>& code, uint32_t global_count, uint16_t local_count) {
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, global_count, local_count);
}

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

std::vector<uint8_t> BuildModuleWithFunctions(const std::vector<std::vector<uint8_t>>& funcs,
                                              const std::vector<uint16_t>& local_counts) {
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> types;
  AppendU32(types, 0);       // name_str
  AppendU8(types, 0);        // kind
  AppendU8(types, 0);        // flags
  AppendU16(types, 0);       // reserved
  AppendU32(types, 4);       // size
  AppendU32(types, 0);       // field_start
  AppendU32(types, 0);       // field_count

  std::vector<uint8_t> fields;

  std::vector<uint8_t> sigs;
  AppendU32(sigs, 0);        // ret_type_id
  AppendU16(sigs, 0);        // param_count
  AppendU16(sigs, 0);        // call_conv
  AppendU32(sigs, 0);        // param_type_start

  std::vector<uint8_t> methods;
  std::vector<uint8_t> functions;
  std::vector<uint8_t> code;
  size_t offset = 0;
  for (size_t i = 0; i < funcs.size(); ++i) {
    uint16_t locals = 0;
    if (i < local_counts.size()) locals = local_counts[i];
    AppendU32(methods, 0);                                   // name_str
    AppendU32(methods, 0);                                   // sig_id
    AppendU32(methods, static_cast<uint32_t>(offset));       // code_offset
    AppendU16(methods, locals);                              // local_count
    AppendU16(methods, 0);                                   // flags

    AppendU32(functions, static_cast<uint32_t>(i));          // method_id
    AppendU32(functions, static_cast<uint32_t>(offset));     // code_offset
    AppendU32(functions, static_cast<uint32_t>(funcs[i].size()));
    AppendU32(functions, 8);                                 // stack_max

    code.insert(code.end(), funcs[i].begin(), funcs[i].end());
    offset += funcs[i].size();
  }

  std::vector<uint8_t> globals;
  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, static_cast<uint32_t>(funcs.size()), 0});
  sections.push_back({4, sigs, 1, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, static_cast<uint32_t>(funcs.size()), 0});
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

std::vector<uint8_t> BuildModuleWithFunctionsAndSig(const std::vector<std::vector<uint8_t>>& funcs,
                                                    const std::vector<uint16_t>& local_counts,
                                                    uint32_t ret_type_id,
                                                    uint16_t param_count,
                                                    const std::vector<uint32_t>& param_types) {
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> types;
  AppendU32(types, 0);       // name_str
  AppendU8(types, 0);        // kind
  AppendU8(types, 0);        // flags
  AppendU16(types, 0);       // reserved
  AppendU32(types, 4);       // size
  AppendU32(types, 0);       // field_start
  AppendU32(types, 0);       // field_count

  std::vector<uint8_t> fields;

  std::vector<uint8_t> sigs;
  AppendU32(sigs, ret_type_id);
  AppendU16(sigs, param_count);
  AppendU16(sigs, 0);        // call_conv
  AppendU32(sigs, 0);        // param_type_start
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
    AppendU32(methods, 0);                                   // name_str
    AppendU32(methods, 0);                                   // sig_id
    AppendU32(methods, static_cast<uint32_t>(offset));       // code_offset
    AppendU16(methods, locals);                              // local_count
    AppendU16(methods, 0);                                   // flags

    AppendU32(functions, static_cast<uint32_t>(i));          // method_id
    AppendU32(functions, static_cast<uint32_t>(offset));     // code_offset
    AppendU32(functions, static_cast<uint32_t>(funcs[i].size()));
    AppendU32(functions, 8);                                 // stack_max

    code.insert(code.end(), funcs[i].begin(), funcs[i].end());
    offset += funcs[i].size();
  }

  std::vector<uint8_t> globals;
  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, static_cast<uint32_t>(funcs.size()), 0});
  sections.push_back({4, sigs, 1, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, 0, 0});
  sections.push_back({7, functions, static_cast<uint32_t>(funcs.size()), 0});
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

std::vector<uint8_t> BuildNewClosureModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadNewClosureVerifyModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildArrayLenModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildListInsertModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildListOverflowModule() {
  using simplevm::OpCode;
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
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 2);
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPushI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildStringModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> types;
  // type 0: dummy
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
  AppendU16(types, 0);
  AppendU32(types, 4);
  AppendU32(types, 0);
  AppendU32(types, 0);
  // type 1: object with 1 i32 field at offset 0
  AppendU32(types, 0);
  AppendU8(types, 0);
  AppendU8(types, 0);
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildConstU32Module() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstU32));
  AppendU32(code, 1234);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstCharModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstChar));
  AppendU16(code, 65);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildConstI64Module() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildI64ModModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> types;
  AppendU32(types, 0);
  AppendU8(types, 0);
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildIntrinsicTrapModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Intrinsic));
  AppendU32(code, 42);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildSysCallTrapModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::SysCall));
  AppendU32(code, 7);
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadMergeModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringConcatVerifyModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  AppendU8(code, static_cast<uint8_t>(OpCode::StringSlice));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringSliceStartVerifyModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadListLenVerifyModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadListPushValueVerifyModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildBadJumpBoundaryModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadJmpCondRuntimeModule(bool invert) {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 1, 0);
}

std::vector<uint8_t> BuildGlobalInitStringModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithGlobalInitConst(code, 1, 0, 0xFFFFFFF0u);
}

std::vector<uint8_t> BuildBadStringConstNoNullModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithSigParamCount(code, 0, 0, 1);
}

std::vector<uint8_t> BuildBadSigCallConvLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithSigCallConv(code, 0, 0, 2);
}

std::vector<uint8_t> BuildBadSigParamTypesMissingLoadModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadSigTableTruncatedLoadModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x00, 0xDEADBEEFu);
  return module;
}

std::vector<uint8_t> BuildBadHeaderVersionLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU16(module, 0x04, 0x0002u);
  return module;
}

std::vector<uint8_t> BuildBadHeaderReservedLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x14, 1);
  return module;
}

std::vector<uint8_t> BuildBadSectionCountZeroLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x08, 0);
  return module;
}

std::vector<uint8_t> BuildBadSectionTableMisalignedLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  std::vector<uint8_t> module = BuildModule(code, 0, 0);
  WriteU32(module, 0x0C, 2);
  return module;
}

std::vector<uint8_t> BuildBadSectionTableOffsetOobLoadModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadFieldsTableSizeLoadModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithMethodFlags(code, 0, 0, 0x10);
}

std::vector<uint8_t> BuildBadHeaderFlagsLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithHeaderFlags(code, 0, 0, 1);
}

std::vector<uint8_t> BuildJumpToEndModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithStackMax(code, 0, 0, 0);
}

std::vector<uint8_t> BuildBadEntryMethodLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithEntryMethodId(code, 0, 0, 1);
}

std::vector<uint8_t> BuildBadFunctionOffsetLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithFunctionOffset(code, 0, 0, 4);
}

std::vector<uint8_t> BuildBadMethodOffsetLoadModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithMethodCodeOffset(code, 0, 0, 4);
}

std::vector<uint8_t> BuildBadFunctionOverlapLoadModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadGlobal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::CallIndirect));
  AppendU32(code, 0);
  AppendU8(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModuleWithGlobalInitConst(code, 1, 0, 0);
}

std::vector<uint8_t> BuildBadCallVerifyModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadCallIndirectParamTypeVerifyModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadTailCallParamTypeVerifyModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadTailCallVerifyModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::TailCall));
  AppendU32(code, 0);
  AppendU8(code, 1);
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadReturnVerifyModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadConvVerifyModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadU64VerifyModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ArrayLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadArrayGetNullModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadListGetModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListGetNullModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadListSetModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadListPopModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListPopI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadListInsertModule() {
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadListInsertNullModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

std::vector<uint8_t> BuildBadListClearNullModule() {
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::ListClear));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringGetCharModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstNull));
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Halt));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildBadStringConcatNullModule() {
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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
  using simplevm::OpCode;
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

bool RunNewClosureTest() {
  std::vector<uint8_t> module_bytes = BuildNewClosureModule();
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

bool RunArrayLenTest() {
  std::vector<uint8_t> module_bytes = BuildArrayLenModule();
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

bool RunListLenTest() {
  std::vector<uint8_t> module_bytes = BuildListLenModule();
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
  if (exec.exit_code != 5) {
    std::cerr << "expected 5, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunListRemoveTest() {
  std::vector<uint8_t> module_bytes = BuildListRemoveModule();
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

bool RunListClearTest() {
  std::vector<uint8_t> module_bytes = BuildListClearModule();
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
  if (exec.exit_code != 0) {
    std::cerr << "expected 0, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunStringTest() {
  std::vector<uint8_t> module_bytes = BuildStringModule();
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

bool RunStringGetCharTest() {
  std::vector<uint8_t> module_bytes = BuildStringGetCharModule();
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
  if (exec.exit_code != 66) {
    std::cerr << "expected 66, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunStringSliceTest() {
  std::vector<uint8_t> module_bytes = BuildStringSliceModule();
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

bool RunConstU32Test() {
  std::vector<uint8_t> module_bytes = BuildConstU32Module();
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
  if (exec.exit_code != 1234) {
    std::cerr << "expected 1234, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstCharTest() {
  std::vector<uint8_t> module_bytes = BuildConstCharModule();
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
  if (exec.exit_code != 65) {
    std::cerr << "expected 65, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunConstI64Test() {
  std::vector<uint8_t> module_bytes = BuildConstI64Module();
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

bool RunConstU64Test() {
  std::vector<uint8_t> module_bytes = BuildConstU64Module();
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

bool RunConstF32Test() {
  std::vector<uint8_t> module_bytes = BuildConstF32Module();
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

bool RunConstF64Test() {
  std::vector<uint8_t> module_bytes = BuildConstF64Module();
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

bool RunConstI128Test() {
  std::vector<uint8_t> module_bytes = BuildConstI128Module();
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

bool RunConstU128Test() {
  std::vector<uint8_t> module_bytes = BuildConstU128Module();
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

bool RunI64ArithTest() {
  std::vector<uint8_t> module_bytes = BuildI64ArithModule();
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

bool RunI64ModTest() {
  std::vector<uint8_t> module_bytes = BuildI64ModModule();
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

bool RunF32ArithTest() {
  std::vector<uint8_t> module_bytes = BuildF32ArithModule();
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

bool RunF64ArithTest() {
  std::vector<uint8_t> module_bytes = BuildF64ArithModule();
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

bool RunConvIntTest() {
  std::vector<uint8_t> module_bytes = BuildConvIntModule();
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

bool RunConvFloatTest() {
  std::vector<uint8_t> module_bytes = BuildConvFloatModule();
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

bool RunU32ArithTest() {
  std::vector<uint8_t> module_bytes = BuildU32ArithModule();
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

bool RunU64CmpTest() {
  std::vector<uint8_t> module_bytes = BuildU64CmpModule();
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

bool RunU32CmpBoundsTest() {
  std::vector<uint8_t> module_bytes = BuildU32CmpBoundsModule();
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

bool RunU64CmpBoundsTest() {
  std::vector<uint8_t> module_bytes = BuildU64CmpBoundsModule();
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

bool RunU32CmpMinMaxTest() {
  std::vector<uint8_t> module_bytes = BuildU32CmpMinMaxModule();
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

bool RunU64CmpMinMaxTest() {
  std::vector<uint8_t> module_bytes = BuildU64CmpMinMaxModule();
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

bool RunU32DivZeroTest() {
  std::vector<uint8_t> module_bytes = BuildU32DivZeroModule();
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

bool RunU32OverflowTest() {
  std::vector<uint8_t> module_bytes = BuildU32OverflowModule();
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

bool RunU64DivZeroTest() {
  std::vector<uint8_t> module_bytes = BuildU64DivZeroModule();
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

bool RunU64OverflowTest() {
  std::vector<uint8_t> module_bytes = BuildU64OverflowModule();
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

bool RunBitwiseI32Test() {
  std::vector<uint8_t> module_bytes = BuildBitwiseI32Module();
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

bool RunShiftMaskI32Test() {
  std::vector<uint8_t> module_bytes = BuildShiftMaskI32Module();
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

bool RunBitwiseI64Test() {
  std::vector<uint8_t> module_bytes = BuildBitwiseI64Module();
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

bool RunShiftMaskI64Test() {
  std::vector<uint8_t> module_bytes = BuildShiftMaskI64Module();
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

bool RunReturnRefTest() {
  std::vector<uint8_t> module_bytes = BuildReturnRefModule();
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
  return true;
}

bool RunDebugNoopTest() {
  std::vector<uint8_t> module_bytes = BuildDebugNoopModule();
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

bool RunFieldTest() {
  std::vector<uint8_t> module_bytes = BuildFieldModule();
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
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadConstStringVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstStringModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadMergeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadMergeModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadMergeHeightVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadMergeHeightModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadMergeRefI32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadMergeRefI32Module();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStackUnderflowVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStackUnderflowVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringConcatVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringConcatVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringGetCharVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringGetCharVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringGetCharIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringGetCharIdxVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringSliceVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringSliceVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadNewClosureVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadNewClosureVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringSliceStartVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringSliceStartVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringSliceEndVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringSliceEndVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadIsNullVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadIsNullVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefEqVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefEqVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefEqMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefEqMixedVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefNeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefNeVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadRefNeMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadRefNeMixedVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeOfVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeOfVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadLoadFieldTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadLoadFieldTypeVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStoreFieldObjectVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStoreFieldObjectVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStoreFieldValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStoreFieldValueVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArrayLenVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArrayLenVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArrayGetIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArrayGetIdxVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetIdxVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetValueVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListLenVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListLenVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListGetIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListGetIdxVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetValueVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPushValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPushValueVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPopVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPopVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListInsertValueVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListInsertValueVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListRemoveIdxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListRemoveIdxVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListClearVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListClearVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStringLenVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringLenVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolNotVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolNotVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolAndVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolAndVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolAndMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolAndMixedVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolOrVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolOrVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBoolOrMixedVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBoolOrMixedVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpCondVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpCondVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpFalseCondVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpFalseCondVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArrayGetArrVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArrayGetArrVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadArraySetArrVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadArraySetArrVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListGetListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListGetListVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListSetListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListSetListVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPushListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPushListVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListPopListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListPopListVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListInsertListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListInsertListVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListRemoveListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListRemoveListVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadListClearListVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadListClearListVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadLocalUninitVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadLocalUninitModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJumpBoundaryVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJumpBoundaryModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJumpOobVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadJumpOobModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadJmpRuntimeTrapTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpRuntimeModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_jmp_runtime load failed: " << load.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module, false);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << "bad_jmp_runtime expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunBadJmpTrueRuntimeTrapTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpTrueRuntimeModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_jmp_true_runtime load failed: " << load.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module, false);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << "bad_jmp_true_runtime expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunBadJmpFalseRuntimeTrapTest() {
  std::vector<uint8_t> module_bytes = BuildBadJmpFalseRuntimeModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_jmp_false_runtime load failed: " << load.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module, false);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << "bad_jmp_false_runtime expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunBadGlobalUninitVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalUninitModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunGlobalInitStringTest() {
  std::vector<uint8_t> module_bytes = BuildGlobalInitStringModule();
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
  if (exec.exit_code != 2) {
    std::cerr << "expected 2, got " << exec.exit_code << "\n";
    return false;
  }
  return true;
}

bool RunGlobalInitF32Test() {
  std::vector<uint8_t> module_bytes = BuildGlobalInitF32Module();
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

bool RunGlobalInitF64Test() {
  std::vector<uint8_t> module_bytes = BuildGlobalInitF64Module();
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

bool RunBadGlobalInitConstLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalInitConstModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadStringConstNoNullLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadStringConstNoNullModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadI128BlobLenLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadI128BlobLenModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldOffsetLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldSizeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldAlignmentLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldAlignmentLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeConstLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeConstLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadGlobalInitTypeRuntimeTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalInitTypeRuntimeModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "bad_global_init_type load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << "bad_global_init_type verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << "bad_global_init_type expected trap, got status="
              << static_cast<int>(exec.status) << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunGoodStringConstLoadTest() {
  std::vector<uint8_t> module_bytes = BuildGoodStringConstLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunGoodI128BlobLenLoadTest() {
  std::vector<uint8_t> module_bytes = BuildGoodI128BlobLenLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  return true;
}

bool RunBadParamLocalsVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadParamLocalsModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadSigCallConvLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigCallConvLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypesMissingLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypesMissingLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypeStartLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypeStartLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypeMisalignedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypeMisalignedLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigParamTypeIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigParamTypeIdLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigTableTruncatedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigTableTruncatedLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionAlignmentLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionAlignmentLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionOverlapLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionOverlapLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadUnknownSectionIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadUnknownSectionIdLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadDuplicateSectionIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadDuplicateSectionIdLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionTableOobLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionTableOobLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadEndianHeaderLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadEndianHeaderLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadHeaderFlagsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderFlagsLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadHeaderMagicLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderMagicLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadHeaderVersionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderVersionLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadHeaderReservedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadHeaderReservedLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionCountZeroLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionCountZeroLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionTableMisalignedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionTableMisalignedLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSectionTableOffsetOobLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSectionTableOffsetOobLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypesTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypesTableSizeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldsTableSizeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodsTableSizeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadSigsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadSigsTableSizeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadGlobalsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalsTableSizeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionsTableSizeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionsTableSizeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadTypeFieldRangeLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadTypeFieldRangeLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFieldTypeIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFieldTypeIdLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadGlobalTypeIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadGlobalTypeIdLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionMethodIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionMethodIdLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodSigIdLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodSigIdLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunMissingCodeSectionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildMissingCodeSectionLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunMissingFunctionsSectionLoadTest() {
  std::vector<uint8_t> module_bytes = BuildMissingFunctionsSectionLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadConstStringOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstStringOffsetLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadConstI128OffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstI128OffsetLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadConstF64TruncatedLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadConstF64TruncatedLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodFlagsLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodFlagsLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunJumpToEndTest() {
  std::vector<uint8_t> module_bytes = BuildJumpToEndModule();
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

bool RunBadStackMaxVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadStackMaxModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadStackMaxZeroLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadStackMaxZeroLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadEntryMethodLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadEntryMethodLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionOffsetLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadMethodOffsetLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadMethodOffsetLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunBadFunctionOverlapLoadTest() {
  std::vector<uint8_t> module_bytes = BuildBadFunctionOverlapLoadModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (load.ok) {
    std::cerr << "expected load failure\n";
    return false;
  }
  return true;
}

bool RunCallCheckTest() {
  std::vector<uint8_t> module_bytes = BuildCallCheckModule();
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

bool RunCallIndirectTest() {
  std::vector<uint8_t> module_bytes = BuildCallIndirectModule();
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

bool RunBadCallIndirectVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallIndirectVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadCallVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadCallParamTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallParamTypeVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadCallIndirectParamTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadCallIndirectParamTypeVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadTailCallParamTypeVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTailCallParamTypeVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadTailCallVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadTailCallVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadReturnVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadReturnVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadConvVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadConvVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunCallIndirectParamTypeTest() {
  std::vector<uint8_t> module_bytes = BuildCallIndirectParamTypeModule();
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

bool RunTailCallTest() {
  std::vector<uint8_t> module_bytes = BuildTailCallModule();
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

bool RunExpectTrap(const std::vector<uint8_t>& module_bytes, const char* name) {
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << name << " load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (!vr.ok) {
    std::cerr << name << " verify failed: " << vr.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << name << " expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunExpectTrapNoVerify(const std::vector<uint8_t>& module_bytes, const char* name) {
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << name << " load failed: " << load.error << "\n";
    return false;
  }
  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module, false);
  if (exec.status != simplevm::ExecStatus::Trapped) {
    std::cerr << name << " expected trap, got status=" << static_cast<int>(exec.status)
              << " error=" << exec.error << "\n";
    return false;
  }
  return true;
}

bool RunIntrinsicTrapTest() {
  return RunExpectTrap(BuildIntrinsicTrapModule(), "intrinsic");
}

bool RunSysCallTrapTest() {
  return RunExpectTrap(BuildSysCallTrapModule(), "syscall");
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

bool RunBadListSetTrapTest() {
  return RunExpectTrap(BuildBadListSetModule(), "bad_list_set");
}

bool RunBadListSetNullTrapTest() {
  return RunExpectTrap(BuildBadListSetNullModule(), "bad_list_set_null");
}

bool RunBadListSetNegIndexTrapTest() {
  return RunExpectTrap(BuildBadListSetNegIndexModule(), "bad_list_set_neg_index");
}

bool RunBadListPopTrapTest() {
  return RunExpectTrap(BuildBadListPopModule(), "bad_list_pop");
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

bool RunBadListInsertNullTrapTest() {
  return RunExpectTrap(BuildBadListInsertNullModule(), "bad_list_insert_null");
}

bool RunBadListRemoveTrapTest() {
  return RunExpectTrap(BuildBadListRemoveModule(), "bad_list_remove");
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
  return RunExpectTrapNoVerify(BuildBadConvRuntimeModule(), "bad_conv_runtime");
}

bool RunBadConstI128KindTrapTest() {
  return RunExpectTrap(BuildBadConstI128KindModule(), "bad_const_i128_kind");
}

bool RunBadConstU128BlobTrapTest() {
  return RunExpectTrap(BuildBadConstU128BlobModule(), "bad_const_u128_blob");
}

bool RunBadBitwiseVerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadBitwiseVerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadU32VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadU32VerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadU64VerifyTest() {
  std::vector<uint8_t> module_bytes = BuildBadU64VerifyModule();
  simplevm::LoadResult load = simplevm::LoadModuleFromBytes(module_bytes);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return false;
  }
  simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
  if (vr.ok) {
    std::cerr << "expected verify failure\n";
    return false;
  }
  return true;
}

bool RunBadBitwiseRuntimeTrapTest() {
  return RunExpectTrapNoVerify(BuildBadBitwiseRuntimeModule(), "bad_bitwise_runtime");
}

bool RunBadU32RuntimeTrapTest() {
  return RunExpectTrapNoVerify(BuildBadU32RuntimeModule(), "bad_u32_runtime");
}

bool RunBadU64RuntimeTrapTest() {
  return RunExpectTrapNoVerify(BuildBadU64RuntimeModule(), "bad_u64_runtime");
}

bool RunBadCallIndirectTrapTest() {
  return RunExpectTrap(BuildBadCallIndirectFuncModule(), "bad_call_indirect");
}

bool RunBadCallIndirectTypeTrapTest() {
  return RunExpectTrap(BuildBadCallIndirectTypeModule(), "bad_call_indirect_type");
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

bool RunListOverflowTrapTest() {
  return RunExpectTrap(BuildListOverflowModule(), "list_overflow");
}

bool RunHeapReuseTest() {
  simplevm::Heap heap;
  uint32_t first = heap.Allocate(simplevm::ObjectKind::String, 0, 8);
  uint32_t second = heap.Allocate(simplevm::ObjectKind::Array, 0, 16);
  heap.ResetMarks();
  heap.Mark(second);
  heap.Sweep();
  if (heap.Get(first) != nullptr) {
    std::cerr << "expected freed handle to be invalid\n";
    return false;
  }
  uint32_t reused = heap.Allocate(simplevm::ObjectKind::List, 0, 12);
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

bool RunGcTest() {
  std::vector<uint8_t> module_bytes = BuildGcModule();
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
      {"new_closure", RunNewClosureTest},
      {"array_i32", RunArrayTest},
      {"array_len", RunArrayLenTest},
      {"list_i32", RunListTest},
      {"list_len", RunListLenTest},
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
      {"i64_mod", RunI64ModTest},
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
      {"heap_reuse", RunHeapReuseTest},
      {"gc_smoke", RunGcTest},
      {"field_ops", RunFieldTest},
      {"bad_field_verify", RunBadFieldVerifyTest},
      {"bad_const_string", RunBadConstStringVerifyTest},
      {"bad_type_verify", RunBadTypeVerifyTest},
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
      {"bad_list_len_verify", RunBadListLenVerifyTest},
      {"bad_list_get_idx_verify", RunBadListGetIdxVerifyTest},
      {"bad_list_set_value_verify", RunBadListSetValueVerifyTest},
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
      {"bad_sig_param_type_misaligned_load", RunBadSigParamTypeMisalignedLoadTest},
      {"bad_sig_param_type_id_load", RunBadSigParamTypeIdLoadTest},
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
      {"bad_header_reserved_load", RunBadHeaderReservedLoadTest},
      {"bad_section_count_zero_load", RunBadSectionCountZeroLoadTest},
      {"bad_section_table_misaligned_load", RunBadSectionTableMisalignedLoadTest},
      {"bad_section_table_offset_oob_load", RunBadSectionTableOffsetOobLoadTest},
      {"bad_types_table_size_load", RunBadTypesTableSizeLoadTest},
      {"bad_fields_table_size_load", RunBadFieldsTableSizeLoadTest},
      {"bad_methods_table_size_load", RunBadMethodsTableSizeLoadTest},
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
      {"bad_call_indirect_param_type_verify", RunBadCallIndirectParamTypeVerifyTest},
      {"bad_tailcall_param_type_verify", RunBadTailCallParamTypeVerifyTest},
      {"bad_tailcall_verify", RunBadTailCallVerifyTest},
      {"bad_return_verify", RunBadReturnVerifyTest},
      {"bad_conv_verify", RunBadConvVerifyTest},
      {"bad_bitwise_verify", RunBadBitwiseVerifyTest},
      {"bad_u32_verify", RunBadU32VerifyTest},
      {"bad_u64_verify", RunBadU64VerifyTest},
      {"callcheck", RunCallCheckTest},
      {"call_param_types", RunCallParamTypeTest},
      {"call_indirect", RunCallIndirectTest},
      {"call_indirect_param_types", RunCallIndirectParamTypeTest},
      {"tailcall", RunTailCallTest},
      {"jump_to_end", RunJumpToEndTest},
      {"intrinsic_trap", RunIntrinsicTrapTest},
      {"syscall_trap", RunSysCallTrapTest},
      {"bad_call_indirect", RunBadCallIndirectTrapTest},
      {"bad_call_indirect_type", RunBadCallIndirectTypeTrapTest},
      {"bad_conv_runtime", RunBadConvRuntimeTrapTest},
      {"bad_bitwise_runtime", RunBadBitwiseRuntimeTrapTest},
      {"bad_u32_runtime", RunBadU32RuntimeTrapTest},
      {"bad_u64_runtime", RunBadU64RuntimeTrapTest},
      {"bad_const_i128_kind", RunBadConstI128KindTrapTest},
      {"bad_const_u128_blob", RunBadConstU128BlobTrapTest},
      {"bad_array_get", RunBadArrayGetTrapTest},
      {"bad_array_len_null", RunBadArrayLenNullTrapTest},
      {"bad_array_get_null", RunBadArrayGetNullTrapTest},
      {"bad_array_set_null", RunBadArraySetNullTrapTest},
      {"bad_array_set", RunBadArraySetTrapTest},
      {"bad_array_get_neg_index", RunBadArrayGetNegIndexTrapTest},
      {"bad_array_set_neg_index", RunBadArraySetNegIndexTrapTest},
      {"bad_list_get", RunBadListGetTrapTest},
      {"bad_list_len_null", RunBadListLenNullTrapTest},
      {"bad_list_get_null", RunBadListGetNullTrapTest},
      {"bad_list_set", RunBadListSetTrapTest},
      {"bad_list_set_null", RunBadListSetNullTrapTest},
      {"bad_list_get_neg_index", RunBadListGetNegIndexTrapTest},
      {"bad_list_set_neg_index", RunBadListSetNegIndexTrapTest},
      {"bad_list_pop", RunBadListPopTrapTest},
      {"bad_list_push_null", RunBadListPushNullTrapTest},
      {"bad_list_pop_null", RunBadListPopNullTrapTest},
      {"bad_list_insert", RunBadListInsertTrapTest},
      {"bad_list_insert_null", RunBadListInsertNullTrapTest},
      {"bad_list_remove", RunBadListRemoveTrapTest},
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
      {"list_overflow", RunListOverflowTrapTest},
  };

  int failures = 0;
  int tCount = 0;
  for (const auto& test : tests) {
    tCount++;
    bool ok = test.fn();
    if (!ok) {
      failures++;
    }
  }

  if (failures == 0) {
    std::cout << "Total Tests:  " << tCount << "\n";
    std::cout << "all tests passed\n";
    return 0;
  }
  std::cout << "Total Tests:  " << tCount << "\n";
  std::cout << failures << " tests failed\n";
  return 1;
}
