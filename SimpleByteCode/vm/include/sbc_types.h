#ifndef SIMPLE_SBC_TYPES_H
#define SIMPLE_SBC_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace simplevm {

constexpr uint32_t kSbcMagic = 0x30434253u; // 'SBC0'
constexpr uint16_t kSbcVersion = 0x0001u;

struct SbcHeader {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint8_t endian = 0;
  uint8_t flags = 0;
  uint32_t section_count = 0;
  uint32_t section_table_offset = 0;
  uint32_t entry_method_id = 0xFFFFFFFFu;
  uint32_t reserved0 = 0;
  uint32_t reserved1 = 0;
  uint32_t reserved2 = 0;
};

struct SectionEntry {
  uint32_t id = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t count = 0;
};

enum class SectionId : uint32_t {
  Types = 1,
  Fields = 2,
  Methods = 3,
  Sigs = 4,
  ConstPool = 5,
  Globals = 6,
  Functions = 7,
  Code = 8,
  Debug = 9,
};

enum class TypeKind : uint8_t {
  Unspecified = 0,
  I32 = 1,
  I64 = 2,
  F32 = 3,
  F64 = 4,
  Ref = 5,
};

struct DebugHeader {
  uint32_t file_count = 0;
  uint32_t line_count = 0;
  uint32_t sym_count = 0;
  uint32_t reserved = 0;
};

struct DebugFileRow {
  uint32_t file_name_str = 0;
  uint32_t file_hash = 0;
};

struct DebugLineRow {
  uint32_t method_id = 0;
  uint32_t code_offset = 0;
  uint32_t file_id = 0;
  uint32_t line = 0;
  uint32_t column = 0;
};

struct DebugSymRow {
  uint32_t kind = 0;
  uint32_t owner_id = 0;
  uint32_t symbol_id = 0;
  uint32_t name_str = 0;
};

struct TypeRow {
  uint32_t name_str = 0;
  uint8_t kind = 0;
  uint8_t flags = 0;
  uint16_t reserved = 0;
  uint32_t size = 0;
  uint32_t field_start = 0;
  uint32_t field_count = 0;
};

struct FieldRow {
  uint32_t name_str = 0;
  uint32_t type_id = 0;
  uint32_t offset = 0;
  uint32_t flags = 0;
};

struct MethodRow {
  uint32_t name_str = 0;
  uint32_t sig_id = 0;
  uint32_t code_offset = 0;
  uint16_t local_count = 0;
  uint16_t flags = 0;
};

struct SigRow {
  uint32_t ret_type_id = 0;
  uint16_t param_count = 0;
  uint16_t call_conv = 0;
  uint32_t param_type_start = 0;
};

struct GlobalRow {
  uint32_t name_str = 0;
  uint32_t type_id = 0;
  uint32_t flags = 0;
  uint32_t init_const_id = 0;
};

struct FunctionRow {
  uint32_t method_id = 0;
  uint32_t code_offset = 0;
  uint32_t code_size = 0;
  uint32_t stack_max = 0;
};

struct SbcModule {
  SbcHeader header;
  std::vector<SectionEntry> sections;
  std::vector<TypeRow> types;
  std::vector<FieldRow> fields;
  std::vector<MethodRow> methods;
  std::vector<SigRow> sigs;
  std::vector<GlobalRow> globals;
  std::vector<FunctionRow> functions;
  std::vector<uint32_t> param_types;
  std::vector<uint8_t> code;
  std::vector<uint8_t> const_pool;
  std::vector<uint8_t> debug;
  DebugHeader debug_header;
  std::vector<DebugFileRow> debug_files;
  std::vector<DebugLineRow> debug_lines;
  std::vector<DebugSymRow> debug_syms;
};

struct LoadResult {
  bool ok = false;
  std::string error;
  SbcModule module;
};

} // namespace simplevm

#endif // SIMPLE_SBC_TYPES_H
