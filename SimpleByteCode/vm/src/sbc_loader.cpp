#include "sbc_loader.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace simplevm {
namespace {

constexpr size_t kHeaderSize = 32;

bool ReadU8At(const std::vector<uint8_t>& bytes, size_t offset, uint8_t* out) {
  if (offset + 1 > bytes.size()) return false;
  *out = bytes[offset];
  return true;
}

bool ReadU16At(const std::vector<uint8_t>& bytes, size_t offset, uint16_t* out) {
  if (offset + 2 > bytes.size()) return false;
  *out = static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1]) << 8);
  return true;
}

bool ReadU32At(const std::vector<uint8_t>& bytes, size_t offset, uint32_t* out) {
  if (offset + 4 > bytes.size()) return false;
  *out = static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
  return true;
}

bool ReadBytes(const std::vector<uint8_t>& bytes, size_t offset, size_t size, std::vector<uint8_t>* out) {
  if (offset + size > bytes.size()) return false;
  out->assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
  return true;
}

const SectionEntry* FindSection(const std::vector<SectionEntry>& sections, SectionId id) {
  for (const auto& sec : sections) {
    if (sec.id == static_cast<uint32_t>(id)) return &sec;
  }
  return nullptr;
}

LoadResult Fail(const std::string& message) {
  LoadResult result;
  result.ok = false;
  result.error = message;
  return result;
}

} // namespace

LoadResult LoadModuleFromFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Fail("failed to open file");
  in.seekg(0, std::ios::end);
  std::streamoff size = in.tellg();
  if (size <= 0) return Fail("empty file");
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) {
    return Fail("failed to read file");
  }
  return LoadModuleFromBytes(bytes);
}

LoadResult LoadModuleFromBytes(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < kHeaderSize) return Fail("file too small for header");

  SbcModule module;
  SbcHeader& header = module.header;

  if (!ReadU32At(bytes, 0x00, &header.magic)) return Fail("header read failed");
  if (!ReadU16At(bytes, 0x04, &header.version)) return Fail("header read failed");
  if (!ReadU8At(bytes, 0x06, &header.endian)) return Fail("header read failed");
  if (!ReadU8At(bytes, 0x07, &header.flags)) return Fail("header read failed");
  if (!ReadU32At(bytes, 0x08, &header.section_count)) return Fail("header read failed");
  if (!ReadU32At(bytes, 0x0C, &header.section_table_offset)) return Fail("header read failed");
  if (!ReadU32At(bytes, 0x10, &header.entry_method_id)) return Fail("header read failed");
  if (!ReadU32At(bytes, 0x14, &header.reserved0)) return Fail("header read failed");
  if (!ReadU32At(bytes, 0x18, &header.reserved1)) return Fail("header read failed");
  if (!ReadU32At(bytes, 0x1C, &header.reserved2)) return Fail("header read failed");

  if (header.magic != kSbcMagic) return Fail("bad magic");
  if (header.version != kSbcVersion) return Fail("unsupported version");
  if (header.endian != 1) return Fail("unsupported endian");
  if (header.reserved0 != 0 || header.reserved1 != 0 || header.reserved2 != 0) {
    return Fail("reserved header fields must be zero");
  }
  if (header.section_count == 0) return Fail("section_count must be > 0");
  if (header.section_table_offset % 4 != 0) return Fail("section table offset must be 4-byte aligned");

  size_t table_size = static_cast<size_t>(header.section_count) * 16u;
  if (header.section_table_offset + table_size > bytes.size()) {
    return Fail("section table out of bounds");
  }

  module.sections.resize(header.section_count);
  std::unordered_set<uint32_t> seen_ids;
  for (uint32_t i = 0; i < header.section_count; ++i) {
    size_t off = header.section_table_offset + static_cast<size_t>(i) * 16u;
    SectionEntry entry;
    if (!ReadU32At(bytes, off + 0, &entry.id)) return Fail("section read failed");
    if (!ReadU32At(bytes, off + 4, &entry.offset)) return Fail("section read failed");
    if (!ReadU32At(bytes, off + 8, &entry.size)) return Fail("section read failed");
    if (!ReadU32At(bytes, off + 12, &entry.count)) return Fail("section read failed");
    if (entry.offset % 4 != 0) return Fail("section offset must be 4-byte aligned");
    if (entry.offset + entry.size > bytes.size()) return Fail("section out of bounds");
    if (!seen_ids.insert(entry.id).second) return Fail("duplicate section id");
    if (entry.id < static_cast<uint32_t>(SectionId::Types) ||
        entry.id > static_cast<uint32_t>(SectionId::Debug)) {
      return Fail("unknown section id");
    }
    module.sections[i] = entry;
  }

  std::vector<SectionEntry> sorted = module.sections;
  std::sort(sorted.begin(), sorted.end(), [](const SectionEntry& a, const SectionEntry& b) {
    return a.offset < b.offset;
  });
  for (size_t i = 1; i < sorted.size(); ++i) {
    if (sorted[i - 1].offset + sorted[i - 1].size > sorted[i].offset) {
      return Fail("section overlap detected");
    }
  }

  const SectionEntry* types = FindSection(module.sections, SectionId::Types);
  const SectionEntry* fields = FindSection(module.sections, SectionId::Fields);
  const SectionEntry* methods = FindSection(module.sections, SectionId::Methods);
  const SectionEntry* sigs = FindSection(module.sections, SectionId::Sigs);
  const SectionEntry* const_pool = FindSection(module.sections, SectionId::ConstPool);
  const SectionEntry* globals = FindSection(module.sections, SectionId::Globals);
  const SectionEntry* functions = FindSection(module.sections, SectionId::Functions);
  const SectionEntry* code = FindSection(module.sections, SectionId::Code);
  const SectionEntry* debug = FindSection(module.sections, SectionId::Debug);

  if (types) {
    constexpr size_t kRowSize = 20;
    if (types->count * kRowSize != types->size) return Fail("type table size mismatch");
    module.types.resize(types->count);
    for (uint32_t i = 0; i < types->count; ++i) {
      size_t off = types->offset + static_cast<size_t>(i) * kRowSize;
      TypeRow row;
      if (!ReadU32At(bytes, off + 0, &row.name_str)) return Fail("type row read failed");
      if (!ReadU8At(bytes, off + 4, &row.kind)) return Fail("type row read failed");
      if (!ReadU8At(bytes, off + 5, &row.flags)) return Fail("type row read failed");
      if (!ReadU16At(bytes, off + 6, &row.reserved)) return Fail("type row read failed");
      if (!ReadU32At(bytes, off + 8, &row.size)) return Fail("type row read failed");
      if (!ReadU32At(bytes, off + 12, &row.field_start)) return Fail("type row read failed");
      if (!ReadU32At(bytes, off + 16, &row.field_count)) return Fail("type row read failed");
      module.types[i] = row;
    }
  }

  if (fields) {
    constexpr size_t kRowSize = 16;
    if (fields->count * kRowSize != fields->size) return Fail("field table size mismatch");
    module.fields.resize(fields->count);
    for (uint32_t i = 0; i < fields->count; ++i) {
      size_t off = fields->offset + static_cast<size_t>(i) * kRowSize;
      FieldRow row;
      if (!ReadU32At(bytes, off + 0, &row.name_str)) return Fail("field row read failed");
      if (!ReadU32At(bytes, off + 4, &row.type_id)) return Fail("field row read failed");
      if (!ReadU32At(bytes, off + 8, &row.offset)) return Fail("field row read failed");
      if (!ReadU32At(bytes, off + 12, &row.flags)) return Fail("field row read failed");
      module.fields[i] = row;
    }
  }

  if (methods) {
    constexpr size_t kRowSize = 16;
    if (methods->count * kRowSize != methods->size) return Fail("method table size mismatch");
    module.methods.resize(methods->count);
    for (uint32_t i = 0; i < methods->count; ++i) {
      size_t off = methods->offset + static_cast<size_t>(i) * kRowSize;
      MethodRow row;
      if (!ReadU32At(bytes, off + 0, &row.name_str)) return Fail("method row read failed");
      if (!ReadU32At(bytes, off + 4, &row.sig_id)) return Fail("method row read failed");
      if (!ReadU32At(bytes, off + 8, &row.code_offset)) return Fail("method row read failed");
      if (!ReadU16At(bytes, off + 12, &row.local_count)) return Fail("method row read failed");
      if (!ReadU16At(bytes, off + 14, &row.flags)) return Fail("method row read failed");
      module.methods[i] = row;
    }
  }

  if (sigs) {
    constexpr size_t kRowSize = 12;
    if (sigs->count * kRowSize != sigs->size) return Fail("signature table size mismatch");
    module.sigs.resize(sigs->count);
    for (uint32_t i = 0; i < sigs->count; ++i) {
      size_t off = sigs->offset + static_cast<size_t>(i) * kRowSize;
      SigRow row;
      if (!ReadU32At(bytes, off + 0, &row.ret_type_id)) return Fail("sig row read failed");
      if (!ReadU16At(bytes, off + 4, &row.param_count)) return Fail("sig row read failed");
      if (!ReadU16At(bytes, off + 6, &row.call_conv)) return Fail("sig row read failed");
      if (!ReadU32At(bytes, off + 8, &row.param_type_start)) return Fail("sig row read failed");
      module.sigs[i] = row;
    }
  }

  if (globals) {
    constexpr size_t kRowSize = 16;
    if (globals->count * kRowSize != globals->size) return Fail("globals table size mismatch");
    module.globals.resize(globals->count);
    for (uint32_t i = 0; i < globals->count; ++i) {
      size_t off = globals->offset + static_cast<size_t>(i) * kRowSize;
      GlobalRow row;
      if (!ReadU32At(bytes, off + 0, &row.name_str)) return Fail("global row read failed");
      if (!ReadU32At(bytes, off + 4, &row.type_id)) return Fail("global row read failed");
      if (!ReadU32At(bytes, off + 8, &row.flags)) return Fail("global row read failed");
      if (!ReadU32At(bytes, off + 12, &row.init_const_id)) return Fail("global row read failed");
      module.globals[i] = row;
    }
  }

  if (functions) {
    constexpr size_t kRowSize = 16;
    if (functions->count * kRowSize != functions->size) return Fail("functions table size mismatch");
    module.functions.resize(functions->count);
    for (uint32_t i = 0; i < functions->count; ++i) {
      size_t off = functions->offset + static_cast<size_t>(i) * kRowSize;
      FunctionRow row;
      if (!ReadU32At(bytes, off + 0, &row.method_id)) return Fail("function row read failed");
      if (!ReadU32At(bytes, off + 4, &row.code_offset)) return Fail("function row read failed");
      if (!ReadU32At(bytes, off + 8, &row.code_size)) return Fail("function row read failed");
      if (!ReadU32At(bytes, off + 12, &row.stack_max)) return Fail("function row read failed");
      module.functions[i] = row;
    }
  }

  if (code) {
    if (!ReadBytes(bytes, code->offset, code->size, &module.code)) {
      return Fail("failed to read code section");
    }
  }

  if (const_pool) {
    if (!ReadBytes(bytes, const_pool->offset, const_pool->size, &module.const_pool)) {
      return Fail("failed to read const pool section");
    }
  }

  if (debug) {
    if (!ReadBytes(bytes, debug->offset, debug->size, &module.debug)) {
      return Fail("failed to read debug section");
    }
  }

  auto const_ok = [&](uint32_t const_id) -> bool {
    if (module.const_pool.empty()) return false;
    if (const_id + 4 > module.const_pool.size()) return false;
    uint32_t kind = 0;
    ReadU32At(module.const_pool, const_id, &kind);
    if (kind == 4) {
      return const_id + 12 <= module.const_pool.size();
    }
    if (const_id + 8 > module.const_pool.size()) return false;
    uint32_t payload = 0;
    ReadU32At(module.const_pool, const_id + 4, &payload);
    if (kind == 0) {
      if (payload >= module.const_pool.size()) return false;
      size_t pos = payload;
      while (pos < module.const_pool.size() && module.const_pool[pos] != 0) {
        ++pos;
      }
      return pos < module.const_pool.size();
    }
    if (kind == 1 || kind == 2) {
      if (payload + 4 > module.const_pool.size()) return false;
      uint32_t blob_len = 0;
      ReadU32At(module.const_pool, payload, &blob_len);
      if (payload + 4 + blob_len > module.const_pool.size()) return false;
      return blob_len == 16;
    }
    if (kind == 3 || kind == 5) {
      return true;
    }
    return false;
  };

  if (!module.functions.empty() && !code) return Fail("code section required when functions exist");
  if (header.entry_method_id != 0xFFFFFFFFu) {
    if (header.entry_method_id >= module.methods.size()) return Fail("entry method id out of range");
  }
  for (const auto& row : module.globals) {
    if (row.init_const_id != 0xFFFFFFFFu) {
      if (!const_ok(row.init_const_id)) return Fail("global init const out of bounds");
    }
  }
  for (size_t i = 0; i < module.types.size(); ++i) {
    const auto& row = module.types[i];
    if (row.field_start + row.field_count > module.fields.size()) return Fail("type field range out of bounds");
    if (row.size > 0) {
      for (uint32_t f = 0; f < row.field_count; ++f) {
        const auto& field = module.fields[row.field_start + f];
        if (field.offset >= row.size) return Fail("field offset out of bounds");
        if (field.type_id < module.types.size()) {
          const auto& field_type = module.types[field.type_id];
          if (field_type.size > 0 && field.offset + field_type.size > row.size) {
            return Fail("field size out of bounds");
          }
        }
      }
    }
  }
  for (size_t i = 0; i < module.methods.size(); ++i) {
    const auto& row = module.methods[i];
    if (row.sig_id >= module.sigs.size()) return Fail("method signature out of range");
    if (code && row.code_offset >= module.code.size()) return Fail("method code offset out of range");
  }
  for (size_t i = 0; i < module.functions.size(); ++i) {
    const auto& row = module.functions[i];
    if (row.method_id >= module.methods.size()) return Fail("function method id out of range");
    if (code && row.code_offset + row.code_size > module.code.size()) return Fail("function code out of range");
  }
  for (size_t i = 0; i < module.fields.size(); ++i) {
    const auto& row = module.fields[i];
    if (row.type_id >= module.types.size()) return Fail("field type id out of range");
  }
  for (size_t i = 0; i < module.globals.size(); ++i) {
    const auto& row = module.globals[i];
    if (row.type_id >= module.types.size()) return Fail("global type id out of range");
  }

  LoadResult result;
  result.ok = true;
  result.module = std::move(module);
  return result;
}

} // namespace simplevm
