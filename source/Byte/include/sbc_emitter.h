#ifndef SIMPLE_VM_SBC_EMITTER_H
#define SIMPLE_VM_SBC_EMITTER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "sbc_types.h"

namespace Simple::Byte {
namespace sbc {

inline void AppendU8(std::vector<uint8_t>& out, uint8_t v) {
  out.push_back(v);
}

inline void WriteU8(std::vector<uint8_t>& out, size_t offset, uint8_t v) {
  out[offset] = v;
}

inline void AppendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void WriteU16(std::vector<uint8_t>& out, size_t offset, uint16_t v) {
  out[offset + 0] = static_cast<uint8_t>(v & 0xFF);
  out[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

inline void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void AppendU64(std::vector<uint8_t>& out, uint64_t v) {
  AppendU32(out, static_cast<uint32_t>(v & 0xFFFFFFFFu));
  AppendU32(out, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

inline void AppendI32(std::vector<uint8_t>& out, int32_t v) {
  AppendU32(out, static_cast<uint32_t>(v));
}

inline void AppendI64(std::vector<uint8_t>& out, int64_t v) {
  AppendU64(out, static_cast<uint64_t>(v));
}

inline void WriteU32(std::vector<uint8_t>& out, size_t offset, uint32_t v) {
  out[offset + 0] = static_cast<uint8_t>(v & 0xFF);
  out[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  out[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  out[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

inline uint32_t ReadU32At(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

inline size_t Align4(size_t v) {
  return (v + 3u) & ~static_cast<size_t>(3u);
}

inline size_t AppendStringToPool(std::vector<uint8_t>& pool, const std::string& text) {
  size_t offset = pool.size();
  pool.insert(pool.end(), text.begin(), text.end());
  pool.push_back('\0');
  return offset;
}

inline void AppendConstString(std::vector<uint8_t>& pool, uint32_t str_offset, uint32_t* out_const_id) {
  uint32_t const_id = static_cast<uint32_t>(pool.size());
  AppendU32(pool, 0); // STRING kind
  AppendU32(pool, str_offset);
  *out_const_id = const_id;
}

struct SectionData {
  uint32_t id = 0;
  std::vector<uint8_t> bytes;
  uint32_t count = 0;
  uint32_t offset = 0;
};

struct SigSpec {
  uint32_t ret_type_id = 0;
  uint16_t param_count = 0;
  std::vector<uint32_t> param_types;
};

struct TypeSpec {
  uint32_t name_str = 0;
  uint8_t kind = static_cast<uint8_t>(Simple::Byte::TypeKind::I32);
  uint8_t flags = 0;
  uint16_t reserved = 0;
  uint32_t size = 4;
  uint32_t field_start = 0;
  uint32_t field_count = 0;
};

struct FieldSpec {
  uint32_t name_str = 0;
  uint32_t type_id = 0;
  uint32_t offset = 0;
  uint32_t flags = 0;
};

struct MethodSpec {
  uint32_t name_str = 0;
  uint32_t sig_id = 0;
  uint32_t code_offset = 0;
  uint16_t local_count = 0;
  uint16_t flags = 0;
};

struct GlobalSpec {
  uint32_t name_str = 0;
  uint32_t type_id = 0;
  uint32_t flags = 1;
  uint32_t init_const_id = 0xFFFFFFFFu;
};

struct FunctionSpec {
  uint32_t method_id = 0;
  uint32_t code_offset = 0;
  uint32_t code_size = 0;
  uint32_t stack_max = 8;
};

inline void AppendTypeRow(std::vector<uint8_t>& out, const TypeSpec& row) {
  AppendU32(out, row.name_str);
  AppendU8(out, row.kind);
  AppendU8(out, row.flags);
  AppendU16(out, row.reserved);
  AppendU32(out, row.size);
  AppendU32(out, row.field_start);
  AppendU32(out, row.field_count);
}

inline void AppendFieldRow(std::vector<uint8_t>& out, const FieldSpec& row) {
  AppendU32(out, row.name_str);
  AppendU32(out, row.type_id);
  AppendU32(out, row.offset);
  AppendU32(out, row.flags);
}

inline void AppendMethodRow(std::vector<uint8_t>& out, const MethodSpec& row) {
  AppendU32(out, row.name_str);
  AppendU32(out, row.sig_id);
  AppendU32(out, row.code_offset);
  AppendU16(out, row.local_count);
  AppendU16(out, row.flags);
}

inline void AppendSigRow(std::vector<uint8_t>& out, const SigSpec& row,
                         uint16_t call_conv = 0,
                         uint32_t param_type_start = 0) {
  AppendU32(out, row.ret_type_id);
  AppendU16(out, row.param_count);
  AppendU16(out, call_conv);
  AppendU32(out, param_type_start);
}

inline void AppendGlobalRow(std::vector<uint8_t>& out, const GlobalSpec& row) {
  AppendU32(out, row.name_str);
  AppendU32(out, row.type_id);
  AppendU32(out, row.flags);
  AppendU32(out, row.init_const_id);
}

inline void AppendFunctionRow(std::vector<uint8_t>& out, const FunctionSpec& row) {
  AppendU32(out, row.method_id);
  AppendU32(out, row.code_offset);
  AppendU32(out, row.code_size);
  AppendU32(out, row.stack_max);
}

inline std::vector<uint8_t> BuildTypeSection(const std::vector<TypeSpec>& rows) {
  std::vector<uint8_t> out;
  for (const auto& row : rows) AppendTypeRow(out, row);
  return out;
}

inline std::vector<uint8_t> BuildFieldSection(const std::vector<FieldSpec>& rows) {
  std::vector<uint8_t> out;
  for (const auto& row : rows) AppendFieldRow(out, row);
  return out;
}

inline std::vector<uint8_t> BuildGlobalSection(const std::vector<GlobalSpec>& rows) {
  std::vector<uint8_t> out;
  for (const auto& row : rows) AppendGlobalRow(out, row);
  return out;
}

inline std::vector<uint8_t> BuildModuleWithTablesAndSig(const std::vector<uint8_t>& code,
                                                        const std::vector<uint8_t>& const_pool,
                                                        const std::vector<uint8_t>& types_bytes,
                                                        const std::vector<uint8_t>& fields_bytes,
                                                        uint32_t global_count,
                                                        uint16_t local_count,
                                                        uint32_t ret_type_id,
                                                        uint16_t param_count,
                                                        uint16_t call_conv,
                                                        uint32_t param_type_start,
                                                        const std::vector<uint32_t>& param_types,
                                                        const std::vector<uint8_t>& imports_bytes = std::vector<uint8_t>(),
                                                        const std::vector<uint8_t>& exports_bytes = std::vector<uint8_t>()) {
  std::vector<uint8_t> types = types_bytes;
  if (types.empty()) {
    types = BuildTypeSection({TypeSpec{}});
  }

  std::vector<uint8_t> fields = fields_bytes;

  std::vector<uint8_t> methods;
  AppendMethodRow(methods, MethodSpec{0, 0, 0, local_count, 0});

  std::vector<uint8_t> sigs;
  AppendSigRow(sigs, SigSpec{ret_type_id, param_count, {}}, call_conv, param_type_start);
  if (!param_types.empty() || param_type_start > 0) {
    std::vector<uint32_t> packed = param_types;
    if (param_type_start > 0) {
      packed.insert(packed.begin(), param_type_start, 0);
    }
    for (uint32_t type_id : packed) {
      AppendU32(sigs, type_id);
    }
  }

  std::vector<GlobalSpec> global_rows(global_count);
  std::vector<uint8_t> globals = BuildGlobalSection(global_rows);

  std::vector<uint8_t> functions;
  AppendFunctionRow(functions, FunctionSpec{0, 0, static_cast<uint32_t>(code.size()), 8});

  std::vector<SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, fields, static_cast<uint32_t>(fields.size() / 16), 0});
  sections.push_back({3, methods, 1, 0});
  sections.push_back({4, sigs, 1, 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, globals, global_count, 0});
  sections.push_back({7, functions, 1, 0});
  if (!imports_bytes.empty()) {
    sections.push_back({10, imports_bytes, static_cast<uint32_t>(imports_bytes.size() / 16), 0});
  }
  if (!exports_bytes.empty()) {
    sections.push_back({11, exports_bytes, static_cast<uint32_t>(exports_bytes.size() / 16), 0});
  }
  sections.push_back({8, code, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = kSbcHeaderSize;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);

  WriteU32(module, 0x00, 0x30434253u); // magic
  module[0x04] = 0x01;                 // version (low byte)
  module[0x05] = 0x00;                 // version (high byte)
  module[0x06] = 1;                    // endian
  module[0x07] = 0;                    // flags
  WriteU32(module, kSbcHeaderSectionCountOffset, section_count);
  WriteU32(module, kSbcHeaderSectionTableOffset, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, 0);           // entry_method_id
  WriteU32(module, 0x14, 0);           // reserved0
  WriteU32(module, 0x18, 0);           // reserved1
  WriteU32(module, 0x1C, 0);           // reserved2

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

inline std::vector<uint8_t> BuildModuleFromSections(const std::vector<SectionData>& sections,
                                                    uint32_t entry_method_id = 0) {
  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = kSbcHeaderSize;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  std::vector<SectionData> local_sections = sections;
  for (auto& sec : local_sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);
  WriteU32(module, 0x00, 0x30434253u); // magic
  WriteU16(module, 0x04, 0x0001u);     // version
  WriteU8(module, 0x06, 1);            // endian
  WriteU8(module, 0x07, 0);            // flags
  WriteU32(module, kSbcHeaderSectionCountOffset, section_count);
  WriteU32(module, kSbcHeaderSectionTableOffset, static_cast<uint32_t>(header_size));
  WriteU32(module, 0x10, entry_method_id);
  WriteU32(module, 0x14, 0);
  WriteU32(module, 0x18, 0);
  WriteU32(module, 0x1C, 0);

  size_t table_off = header_size;
  for (const auto& sec : local_sections) {
    WriteU32(module, table_off + 0, sec.id);
    WriteU32(module, table_off + 4, sec.offset);
    WriteU32(module, table_off + 8, static_cast<uint32_t>(sec.bytes.size()));
    WriteU32(module, table_off + 12, sec.count);
    table_off += 16;
  }

  for (const auto& sec : local_sections) {
    if (sec.bytes.empty()) continue;
    std::memcpy(module.data() + sec.offset, sec.bytes.data(), sec.bytes.size());
  }

  return module;
}

inline std::vector<uint8_t> BuildModuleWithTables(const std::vector<uint8_t>& code,
                                                  const std::vector<uint8_t>& const_pool,
                                                  const std::vector<uint8_t>& types_bytes,
                                                  const std::vector<uint8_t>& fields_bytes,
                                                  uint32_t global_count,
                                                  uint16_t local_count) {
  std::vector<uint32_t> empty_params;
  return BuildModuleWithTablesAndSig(code, const_pool, types_bytes, fields_bytes, global_count, local_count,
                                     0, 0, 0, 0, empty_params);
}

inline std::vector<uint8_t> BuildModule(const std::vector<uint8_t>& code,
                                        uint32_t global_count,
                                        uint16_t local_count) {
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  std::vector<uint8_t> empty;
  return BuildModuleWithTables(code, const_pool, empty, empty, global_count, local_count);
}

inline std::vector<uint8_t> BuildModuleWithTablesAndSigAndDebug(const std::vector<uint8_t>& code,
                                                                const std::vector<uint8_t>& const_pool,
                                                                const std::vector<uint8_t>& types_bytes,
                                                                const std::vector<uint8_t>& fields_bytes,
                                                                const std::vector<uint8_t>& debug_bytes,
                                                                uint32_t global_count,
                                                                uint16_t local_count,
                                                                uint32_t ret_type_id,
                                                                uint16_t param_count,
                                                                uint16_t call_conv,
                                                                uint32_t param_type_start,
                                                                const std::vector<uint32_t>& param_types) {
  std::vector<uint8_t> types = types_bytes;
  if (types.empty()) {
    AppendU32(types, 0);
    AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32));
    AppendU8(types, 0);
    AppendU16(types, 0);
    AppendU32(types, 4);
    AppendU32(types, 0);
    AppendU32(types, 0);
  }

  std::vector<uint8_t> fields = fields_bytes;

  std::vector<uint8_t> methods;
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU32(methods, 0);
  AppendU16(methods, local_count);
  AppendU16(methods, 0);

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
    AppendU32(globals, 0);
    AppendU32(globals, 0);
    AppendU32(globals, 1);
    AppendU32(globals, 0xFFFFFFFFu);
  }

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
  sections.push_back({6, globals, global_count, 0});
  sections.push_back({7, functions, 1, 0});
  sections.push_back({8, code, 0, 0});
  sections.push_back({9, debug_bytes, 0, 0});

  const uint32_t section_count = static_cast<uint32_t>(sections.size());
  const size_t header_size = kSbcHeaderSize;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);
  WriteU32(module, 0x00, 0x30434253u);
  module[0x04] = 0x01;
  module[0x05] = 0x00;
  module[0x06] = 1;
  module[0x07] = 0;
  WriteU32(module, kSbcHeaderSectionCountOffset, section_count);
  WriteU32(module, kSbcHeaderSectionTableOffset, static_cast<uint32_t>(header_size));
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

inline std::vector<uint8_t> BuildModuleWithFunctionsAndSigs(const std::vector<std::vector<uint8_t>>& funcs,
                                                            const std::vector<uint16_t>& local_counts,
                                                            const std::vector<uint32_t>& method_sig_ids,
                                                            const std::vector<SigSpec>& sig_specs) {
  std::vector<uint8_t> const_pool;
  uint32_t dummy_str_offset = static_cast<uint32_t>(AppendStringToPool(const_pool, ""));
  uint32_t dummy_const_id = 0;
  AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);

  std::vector<uint8_t> types;
  AppendU32(types, 0);       // name_str
  AppendU8(types, static_cast<uint8_t>(Simple::Byte::TypeKind::I32)); // kind
  AppendU8(types, 0);        // flags
  AppendU16(types, 0);       // reserved
  AppendU32(types, 4);       // size (i32)
  AppendU32(types, 0);       // field_start
  AppendU32(types, 0);       // field_count

  std::vector<uint8_t> fields;

  std::vector<uint8_t> sigs;
  std::vector<uint32_t> param_types;
  for (const auto& spec : sig_specs) {
    uint32_t param_type_start = static_cast<uint32_t>(param_types.size());
    AppendU32(sigs, spec.ret_type_id);
    AppendU16(sigs, spec.param_count);
    AppendU16(sigs, 0); // call_conv
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
    AppendU32(methods, 0);                                   // name_str
    AppendU32(methods, sig_id);                              // sig_id
    AppendU32(methods, static_cast<uint32_t>(offset));       // code_offset
    AppendU16(methods, locals);                              // local_count
    AppendU16(methods, 0);                                   // flags

    AppendU32(functions, static_cast<uint32_t>(i));          // method_id
    AppendU32(functions, static_cast<uint32_t>(offset));     // code_offset
    AppendU32(functions, static_cast<uint32_t>(funcs[i].size()));
    AppendU32(functions, 12);                                // stack_max

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
  const size_t header_size = kSbcHeaderSize;
  const size_t table_size = section_count * 16u;
  size_t cursor = Align4(header_size + table_size);
  for (auto& sec : sections) {
    sec.offset = static_cast<uint32_t>(cursor);
    cursor = Align4(cursor + sec.bytes.size());
  }

  std::vector<uint8_t> module(cursor, 0);
  WriteU32(module, 0x00, 0x30434253u); // magic
  module[0x04] = 0x01;
  module[0x05] = 0x00;
  module[0x06] = 1;
  module[0x07] = 0;
  WriteU32(module, kSbcHeaderSectionCountOffset, section_count);
  WriteU32(module, kSbcHeaderSectionTableOffset, static_cast<uint32_t>(header_size));
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

} // namespace sbc
} // namespace Simple::Byte

#endif // SIMPLE_VM_SBC_EMITTER_H
