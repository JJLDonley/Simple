#include "ir_lang.h"

#include <cctype>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ir_builder.h"
#include "sbc_emitter.h"

namespace Simple::IR::Text {
namespace {

std::string Trim(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) end--;
  return text.substr(start, end - start);
}

std::string StripComment(const std::string& line) {
  size_t cut = line.find_first_of(";#");
  if (cut == std::string::npos) return line;
  return line.substr(0, cut);
}

std::vector<std::string> SplitTokens(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok) {
    out.push_back(tok);
  }
  return out;
}

bool ParseUint(const std::string& text, uint64_t* out) {
  if (!out) return false;
  if (text.empty() || text[0] == '-') return false;
  try {
    size_t idx = 0;
    uint64_t value = std::stoull(text, &idx, 0);
    if (idx != text.size()) return false;
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseInt(const std::string& text, int64_t* out) {
  if (!out) return false;
  try {
    size_t idx = 0;
    int64_t value = std::stoll(text, &idx, 0);
    if (idx != text.size()) return false;
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseFloat(const std::string& text, double* out) {
  if (!out) return false;
  try {
    size_t idx = 0;
    double value = std::stod(text, &idx);
    if (idx != text.size()) return false;
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

template <typename T>
bool FitsSigned(int64_t value) {
  return value >= static_cast<int64_t>(std::numeric_limits<T>::min()) &&
         value <= static_cast<int64_t>(std::numeric_limits<T>::max());
}

template <typename T>
bool FitsUnsigned(uint64_t value) {
  return value <= static_cast<uint64_t>(std::numeric_limits<T>::max());
}

std::string Lower(const std::string& text) {
  std::string out = text;
  for (char& ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return out;
}

bool IsValidLabelName(const std::string& name) {
  if (name.empty()) return false;
  unsigned char first = static_cast<unsigned char>(name[0]);
  if (!(std::isalpha(first) || first == '_')) return false;
  for (size_t i = 1; i < name.size(); ++i) {
    unsigned char ch = static_cast<unsigned char>(name[i]);
    if (!(std::isalnum(ch) || ch == '_')) return false;
  }
  return true;
}

std::vector<std::string> SplitCommaList(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : text) {
    if (ch == ',') {
      if (!cur.empty()) {
        out.push_back(Trim(cur));
        cur.clear();
      }
      continue;
    }
    cur.push_back(ch);
  }
  if (!cur.empty()) out.push_back(Trim(cur));
  return out;
}

bool ParseSigLine(const std::string& line, IrTextSig* out, std::string* error) {
  if (!out) return false;
  size_t name_start = line.find(' ');
  if (name_start == std::string::npos) {
    if (error) *error = "sig missing name";
    return false;
  }
  std::string rest = Trim(line.substr(name_start + 1));
  size_t colon = rest.find(':');
  if (colon == std::string::npos) {
    if (error) *error = "sig missing ':'";
    return false;
  }
  out->name = Trim(rest.substr(0, colon));
  std::string sig = Trim(rest.substr(colon + 1));
  size_t lparen = sig.find('(');
  size_t rparen = sig.find(')');
  size_t arrow = sig.find("->");
  if (lparen == std::string::npos || rparen == std::string::npos || arrow == std::string::npos) {
    if (error) *error = "sig expects (params) -> ret";
    return false;
  }
  if (rparen < lparen || arrow < rparen) {
    if (error) *error = "sig expects (params) -> ret";
    return false;
  }
  std::string params = Trim(sig.substr(lparen + 1, rparen - lparen - 1));
  std::string ret = Trim(sig.substr(arrow + 2));
  out->params.clear();
  if (!params.empty()) {
    std::vector<std::string> parts = SplitCommaList(params);
    for (const auto& p : parts) {
      if (p.empty()) continue;
      out->params.push_back(p);
    }
  }
  out->ret = ret;
  return true;
}

bool ParseConstLine(const std::string& line, IrTextConst* out, std::string* error) {
  if (!out) return false;
  std::vector<std::string> tokens = SplitTokens(line);
  if (tokens.size() < 4) {
    if (error) *error = "const expects name type value";
    return false;
  }
  out->name = tokens[1];
  out->kind = tokens[2];
  std::string value;
  size_t pos = line.find(tokens[2]);
  if (pos == std::string::npos) {
    if (error) *error = "const parse failed";
    return false;
  }
  pos += tokens[2].size();
  value = Trim(line.substr(pos));
  if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
    if (value.size() < 2 || value.back() != value.front()) {
      if (error) *error = "const string missing closing quote";
      return false;
    }
    value = value.substr(1, value.size() - 2);
  }
  out->value = value;
  return true;
}

} // namespace

bool ParseIrTextModule(const std::string& text, IrTextModule* out, std::string* error) {
  if (!out) {
    if (error) *error = "output module is null";
    return false;
  }
  out->functions.clear();
  out->types.clear();
  out->sigs.clear();
  out->consts.clear();
  out->imports.clear();
  out->entry_name.clear();
  out->entry_index = 0;

  bool entry_set = false;
  std::unordered_set<std::string> func_names;

  enum class Section {
    None,
    Types,
    Sigs,
    Consts,
    Imports,
  };
  Section section = Section::None;
  IrTextType* current_type = nullptr;
  IrTextFunction* current = nullptr;
  std::istringstream input(text);
  std::string raw;
  size_t line_no = 0;
  while (std::getline(input, raw)) {
    line_no++;
    std::string line = Trim(StripComment(raw));
    if (line.empty()) continue;

    if (line == "types:") {
      section = Section::Types;
      current_type = nullptr;
      continue;
    }
    if (line == "sigs:") {
      section = Section::Sigs;
      continue;
    }
    if (line == "consts:") {
      section = Section::Consts;
      continue;
    }
    if (line == "imports:") {
      section = Section::Imports;
      continue;
    }

    if (section == Section::Types) {
      if (line.rfind("type ", 0) == 0) {
        std::vector<std::string> tokens = SplitTokens(line);
        if (tokens.size() < 2) {
          if (error) *error = "type missing name at line " + std::to_string(line_no);
          return false;
        }
        IrTextType type;
        type.name = tokens[1];
        for (size_t i = 2; i < tokens.size(); ++i) {
          const std::string& kv = tokens[i];
          size_t eq = kv.find('=');
          if (eq == std::string::npos) continue;
          std::string key = kv.substr(0, eq);
          std::string val = kv.substr(eq + 1);
          uint64_t num = 0;
          if (key == "size" && ParseUint(val, &num)) {
            type.size = static_cast<uint32_t>(num);
          } else if (key == "kind") {
            type.kind = val;
          }
        }
        if (type.size == 0) {
          if (error) *error = "type missing size at line " + std::to_string(line_no);
          return false;
        }
        out->types.push_back(std::move(type));
        current_type = &out->types.back();
        continue;
      }
      if (line.rfind("field ", 0) == 0) {
        if (!current_type) {
          if (error) *error = "field without type at line " + std::to_string(line_no);
          return false;
        }
        std::vector<std::string> tokens = SplitTokens(line);
        if (tokens.size() < 4) {
          if (error) *error = "field expects name type offset at line " + std::to_string(line_no);
          return false;
        }
        IrTextField field;
        field.name = tokens[1];
        field.type = tokens[2];
        for (size_t i = 3; i < tokens.size(); ++i) {
          const std::string& kv = tokens[i];
          size_t eq = kv.find('=');
          if (eq == std::string::npos) continue;
          std::string key = kv.substr(0, eq);
          std::string val = kv.substr(eq + 1);
          uint64_t num = 0;
          if (key == "offset" && ParseUint(val, &num)) {
            field.offset = static_cast<uint32_t>(num);
          }
        }
        current_type->fields.push_back(std::move(field));
        continue;
      }
    }

    if (section == Section::Sigs) {
      if (line.rfind("sig ", 0) == 0) {
        IrTextSig sig;
        std::string err;
        if (!ParseSigLine(line, &sig, &err)) {
          if (error) *error = err + " at line " + std::to_string(line_no);
          return false;
        }
        out->sigs.push_back(std::move(sig));
        continue;
      }
    }

    if (section == Section::Consts) {
      if (line.rfind("const ", 0) == 0) {
        IrTextConst c;
        std::string err;
        if (!ParseConstLine(line, &c, &err)) {
          if (error) *error = err + " at line " + std::to_string(line_no);
          return false;
        }
        out->consts.push_back(std::move(c));
        continue;
      }
    }

    if (section == Section::Imports) {
      if (line.rfind("syscall ", 0) == 0 || line.rfind("intrinsic ", 0) == 0) {
        std::vector<std::string> tokens = SplitTokens(line);
        if (tokens.size() < 3) {
          if (error) *error = "import expects name and id at line " + std::to_string(line_no);
          return false;
        }
        IrTextImport imp;
        imp.kind = tokens[0];
        imp.name = tokens[1];
        std::string id_token = tokens[2];
        if (id_token == "=" && tokens.size() >= 4) {
          id_token = tokens[3];
        }
        uint64_t num = 0;
        if (!ParseUint(id_token, &num)) {
          if (error) *error = "import expects numeric id at line " + std::to_string(line_no);
          return false;
        }
        imp.id = static_cast<uint32_t>(num);
        out->imports.push_back(std::move(imp));
        continue;
      }
    }

    if (line.rfind("func ", 0) == 0) {
      section = Section::None;
      std::vector<std::string> tokens = SplitTokens(line);
      if (tokens.size() < 2) {
        if (error) *error = "func missing name at line " + std::to_string(line_no);
        return false;
      }
      const std::string& func_name = tokens[1];
      if (func_names.find(func_name) != func_names.end()) {
        if (error) *error = "duplicate func name at line " + std::to_string(line_no);
        return false;
      }
      func_names.insert(func_name);
      out->functions.push_back(IrTextFunction{});
      current = &out->functions.back();
      current->name = func_name;
      bool locals_set = false;
      bool stack_set = false;
      for (size_t i = 2; i < tokens.size(); ++i) {
        const std::string& kv = tokens[i];
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);
        uint64_t num = 0;
        if (key == "locals") {
          if (!ParseUint(val, &num)) {
            if (error) *error = "invalid locals value at line " + std::to_string(line_no);
            return false;
          }
          if (!FitsUnsigned<uint16_t>(num)) {
            if (error) *error = "locals out of range at line " + std::to_string(line_no);
            return false;
          }
          current->locals = static_cast<uint16_t>(num);
          locals_set = true;
        } else if (key == "stack") {
          if (!ParseUint(val, &num)) {
            if (error) *error = "invalid stack value at line " + std::to_string(line_no);
            return false;
          }
          if (!FitsUnsigned<uint32_t>(num)) {
            if (error) *error = "stack out of range at line " + std::to_string(line_no);
            return false;
          }
          current->stack_max = static_cast<uint32_t>(num);
          stack_set = true;
        } else if (key == "sig") {
          if (ParseUint(val, &num)) {
            if (!FitsUnsigned<uint32_t>(num)) {
              if (error) *error = "sig out of range at line " + std::to_string(line_no);
              return false;
            }
            current->sig_id = static_cast<uint32_t>(num);
            current->sig_is_name = false;
          } else {
            if (!IsValidLabelName(val)) {
              if (error) *error = "invalid sig name at line " + std::to_string(line_no);
              return false;
            }
            current->sig_name = val;
            current->sig_is_name = true;
          }
        }
      }
      if (!locals_set || !stack_set) {
        if (error) *error = "func missing locals/stack at line " + std::to_string(line_no);
        return false;
      }
      continue;
    }

    if (line == "end") {
      current = nullptr;
      continue;
    }

    if (line.rfind("entry ", 0) == 0) {
      section = Section::None;
      std::vector<std::string> tokens = SplitTokens(line);
      if (tokens.size() != 2) {
        if (error) *error = "entry expects a function name at line " + std::to_string(line_no);
        return false;
      }
      if (entry_set) {
        if (error) *error = "duplicate entry at line " + std::to_string(line_no);
        return false;
      }
      out->entry_name = tokens[1];
      entry_set = true;
      continue;
    }

    if (!current) {
      if (error) *error = "instruction outside func at line " + std::to_string(line_no);
      return false;
    }

    if (line.rfind("locals:", 0) == 0) {
      std::string rest = Trim(line.substr(7));
      if (rest.empty()) {
        if (error) *error = "locals expects names at line " + std::to_string(line_no);
        return false;
      }
      std::vector<std::string> parts = SplitCommaList(rest);
      uint16_t index = 0;
      for (const auto& entry : parts) {
        if (entry.empty()) continue;
        std::string item = entry;
        std::string name = item;
        size_t colon = item.find(':');
        if (colon != std::string::npos) {
          name = Trim(item.substr(0, colon));
        }
        if (name.empty()) continue;
        current->locals_map[name] = index++;
      }
      if (!current->locals_map.empty() && current->locals_map.size() != current->locals) {
        if (error) *error = "locals name count mismatch at line " + std::to_string(line_no);
        return false;
      }
      continue;
    }

    if (!line.empty() && line.back() == ':') {
      IrTextInst inst;
      inst.kind = InstKind::Label;
      inst.label = Trim(line.substr(0, line.size() - 1));
      if (!IsValidLabelName(inst.label)) {
        if (error) *error = "invalid label name at line " + std::to_string(line_no);
        return false;
      }
      current->insts.push_back(std::move(inst));
      continue;
    }

    std::vector<std::string> tokens = SplitTokens(line);
    if (tokens.empty()) continue;
    IrTextInst inst;
    inst.kind = InstKind::Op;
    inst.op = tokens[0];
    for (size_t i = 1; i < tokens.size(); ++i) {
      inst.args.push_back(tokens[i]);
    }
    current->insts.push_back(std::move(inst));
  }

  if (out->entry_name.empty()) {
    if (error) *error = "entry missing";
    return false;
  }
  for (size_t i = 0; i < out->functions.size(); ++i) {
    if (out->functions[i].name == out->entry_name) {
      out->entry_index = static_cast<uint32_t>(i);
      return true;
    }
  }
  if (error) *error = "entry function not found";
  return false;
}

bool LowerIrTextToModule(const IrTextModule& text, Simple::IR::IrModule* out, std::string* error) {
  if (!out) {
    if (error) *error = "output module is null";
    return false;
  }
  out->functions.clear();
  out->sig_specs.clear();
  out->types_bytes.clear();
  out->fields_bytes.clear();
  out->const_pool.clear();
  out->globals_bytes.clear();
  out->imports_bytes.clear();
  out->exports_bytes.clear();
  out->debug_bytes.clear();
  out->entry_method_id = text.entry_index;

  std::vector<uint8_t> const_pool;
  auto add_name = [&](const std::string& name) -> uint32_t {
    return static_cast<uint32_t>(Simple::Byte::sbc::AppendStringToPool(const_pool, name));
  };

  struct TypeBuildRow {
    uint32_t name_str = 0;
    uint8_t kind = 0;
    uint8_t flags = 0;
    uint32_t size = 0;
    uint32_t field_start = 0;
    uint32_t field_count = 0;
  };

  struct FieldBuildRow {
    uint32_t name_str = 0;
    uint32_t type_id = 0;
    uint32_t offset = 0;
    uint32_t flags = 0;
  };

  std::vector<TypeBuildRow> types;
  std::vector<FieldBuildRow> fields;
  std::unordered_map<std::string, uint32_t> type_ids;
  std::vector<std::unordered_map<std::string, uint32_t>> field_ids_by_type;
  std::unordered_map<std::string, uint32_t> field_ids;
  const uint32_t kAmbiguousField = 0xFFFFFFFFu;

  auto add_type = [&](const std::string& name,
                      Simple::Byte::TypeKind kind,
                      uint8_t flags,
                      uint32_t size) -> bool {
    if (type_ids.find(name) != type_ids.end()) {
      if (error) *error = "duplicate type name: " + name;
      return false;
    }
    TypeBuildRow row;
    row.name_str = add_name(name);
    row.kind = static_cast<uint8_t>(kind);
    row.flags = flags;
    row.size = size;
    row.field_start = 0;
    row.field_count = 0;
    uint32_t id = static_cast<uint32_t>(types.size());
    types.push_back(row);
    type_ids[name] = id;
    return true;
  };

  auto add_builtin = [&](const std::string& name, Simple::Byte::TypeKind kind, uint32_t size) -> bool {
    return add_type(name, kind, 0, size);
  };

  if (!add_builtin("i32", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("i64", Simple::Byte::TypeKind::I64, 8)) return false;
  if (!add_builtin("f32", Simple::Byte::TypeKind::F32, 4)) return false;
  if (!add_builtin("f64", Simple::Byte::TypeKind::F64, 8)) return false;
  if (!add_builtin("ref", Simple::Byte::TypeKind::Ref, 4)) return false;
  if (!add_builtin("string", Simple::Byte::TypeKind::Ref, 4)) return false;
  if (!add_builtin("bool", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("char", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("i8", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("i16", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("u8", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("u16", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("u32", Simple::Byte::TypeKind::I32, 4)) return false;
  if (!add_builtin("u64", Simple::Byte::TypeKind::I64, 8)) return false;

  auto parse_type_kind = [&](const std::string& kind_text,
                             Simple::Byte::TypeKind* out_kind,
                             uint8_t* out_flags) -> bool {
    if (!out_kind || !out_flags) return false;
    std::string kind = Lower(kind_text);
    *out_flags = 0;
    if (kind == "i32") { *out_kind = Simple::Byte::TypeKind::I32; return true; }
    if (kind == "i64") { *out_kind = Simple::Byte::TypeKind::I64; return true; }
    if (kind == "f32") { *out_kind = Simple::Byte::TypeKind::F32; return true; }
    if (kind == "f64") { *out_kind = Simple::Byte::TypeKind::F64; return true; }
    if (kind == "ref") { *out_kind = Simple::Byte::TypeKind::Ref; return true; }
    if (kind == "artifact" || kind == "object" || kind == "struct" || kind == "unspecified") {
      *out_kind = Simple::Byte::TypeKind::Unspecified;
      *out_flags = 1;
      return true;
    }
    return false;
  };

  for (const auto& type : text.types) {
    Simple::Byte::TypeKind kind = Simple::Byte::TypeKind::Unspecified;
    uint8_t flags = 0;
    if (type.kind.empty()) {
      if (type.fields.empty()) {
        if (error) *error = "type missing kind: " + type.name;
        return false;
      }
      kind = Simple::Byte::TypeKind::Unspecified;
      flags = 1;
    } else if (!parse_type_kind(type.kind, &kind, &flags)) {
      if (error) *error = "unsupported type kind: " + type.kind;
      return false;
    }
    uint32_t size = type.size;
    if (kind == Simple::Byte::TypeKind::I32 && size != 4) {
      if (error) *error = "type size mismatch for i32: " + type.name;
      return false;
    }
    if (kind == Simple::Byte::TypeKind::I64 && size != 8) {
      if (error) *error = "type size mismatch for i64: " + type.name;
      return false;
    }
    if (kind == Simple::Byte::TypeKind::F32 && size != 4) {
      if (error) *error = "type size mismatch for f32: " + type.name;
      return false;
    }
    if (kind == Simple::Byte::TypeKind::F64 && size != 8) {
      if (error) *error = "type size mismatch for f64: " + type.name;
      return false;
    }
    if (kind == Simple::Byte::TypeKind::Ref && !(size == 0 || size == 4 || size == 8)) {
      if (error) *error = "type size mismatch for ref: " + type.name;
      return false;
    }
    if (!add_type(type.name, kind, flags, size)) return false;
  }

  field_ids_by_type.resize(types.size());
  for (const auto& type : text.types) {
    auto type_it = type_ids.find(type.name);
    if (type_it == type_ids.end()) {
      if (error) *error = "type not found for fields: " + type.name;
      return false;
    }
    uint32_t type_id = type_it->second;
    uint32_t field_start = static_cast<uint32_t>(fields.size());
    uint32_t field_count = 0;
    for (const auto& field : type.fields) {
      auto field_type_it = type_ids.find(field.type);
      if (field_type_it == type_ids.end()) {
        if (error) *error = "field type not found: " + field.type;
        return false;
      }
      FieldBuildRow row;
      row.name_str = add_name(field.name);
      row.type_id = field_type_it->second;
      row.offset = field.offset;
      row.flags = 0;
      uint32_t field_id = static_cast<uint32_t>(fields.size());
      fields.push_back(row);
      field_ids_by_type[type_id][field.name] = field_id;
      auto global_it = field_ids.find(field.name);
      if (global_it == field_ids.end()) {
        field_ids[field.name] = field_id;
      } else {
        field_ids[field.name] = kAmbiguousField;
      }
      field_count++;
    }
    types[type_id].field_start = field_start;
    types[type_id].field_count = field_count;
  }

  std::unordered_map<std::string, uint32_t> sig_ids;
  for (const auto& sig : text.sigs) {
    if (sig_ids.find(sig.name) != sig_ids.end()) {
      if (error) *error = "duplicate sig name: " + sig.name;
      return false;
    }
    Simple::Byte::sbc::SigSpec spec;
    if (Lower(sig.ret) == "void") {
      spec.ret_type_id = 0xFFFFFFFFu;
    } else {
      auto ret_it = type_ids.find(sig.ret);
      if (ret_it == type_ids.end()) {
        if (error) *error = "sig return type not found: " + sig.ret;
        return false;
      }
      spec.ret_type_id = ret_it->second;
    }
    spec.param_count = static_cast<uint16_t>(sig.params.size());
    for (const auto& param : sig.params) {
      auto param_it = type_ids.find(param);
      if (param_it == type_ids.end()) {
        if (error) *error = "sig param type not found: " + param;
        return false;
      }
      spec.param_types.push_back(param_it->second);
    }
    uint32_t sig_id = static_cast<uint32_t>(out->sig_specs.size());
    out->sig_specs.push_back(std::move(spec));
    sig_ids[sig.name] = sig_id;
  }

  std::unordered_map<std::string, IrTextConst> const_map;
  std::unordered_map<std::string, uint32_t> const_string_ids;
  for (const auto& c : text.consts) {
    if (const_map.find(c.name) != const_map.end()) {
      if (error) *error = "duplicate const name: " + c.name;
      return false;
    }
    std::string kind = Lower(c.kind);
    if (kind != "i8" && kind != "i16" && kind != "i32" && kind != "i64" &&
        kind != "u8" && kind != "u16" && kind != "u32" && kind != "u64" &&
        kind != "f32" && kind != "f64" && kind != "bool" && kind != "char" &&
        kind != "string") {
      if (error) *error = "unsupported const kind: " + c.kind;
      return false;
    }
    if (kind == "string") {
      uint32_t str_offset = add_name(c.value);
      uint32_t const_id = 0;
      Simple::Byte::sbc::AppendConstString(const_pool, str_offset, &const_id);
      const_string_ids[c.name] = const_id;
    }
    const_map[c.name] = c;
  }

  std::unordered_map<std::string, uint32_t> syscall_ids;
  std::unordered_map<std::string, uint32_t> intrinsic_ids;
  for (const auto& imp : text.imports) {
    if (imp.kind == "syscall") {
      if (syscall_ids.find(imp.name) != syscall_ids.end()) {
        if (error) *error = "duplicate syscall name: " + imp.name;
        return false;
      }
      syscall_ids[imp.name] = imp.id;
    } else if (imp.kind == "intrinsic") {
      if (intrinsic_ids.find(imp.name) != intrinsic_ids.end()) {
        if (error) *error = "duplicate intrinsic name: " + imp.name;
        return false;
      }
      intrinsic_ids[imp.name] = imp.id;
    }
    uint32_t module_name = add_name(imp.kind);
    uint32_t symbol_name = add_name(imp.name);
    Simple::Byte::sbc::AppendU32(out->imports_bytes, module_name);
    Simple::Byte::sbc::AppendU32(out->imports_bytes, symbol_name);
    Simple::Byte::sbc::AppendU32(out->imports_bytes, 0);
    Simple::Byte::sbc::AppendU32(out->imports_bytes, 0);
  }

  for (const auto& row : types) {
    Simple::Byte::sbc::AppendU32(out->types_bytes, row.name_str);
    Simple::Byte::sbc::AppendU8(out->types_bytes, row.kind);
    Simple::Byte::sbc::AppendU8(out->types_bytes, row.flags);
    Simple::Byte::sbc::AppendU16(out->types_bytes, 0);
    Simple::Byte::sbc::AppendU32(out->types_bytes, row.size);
    Simple::Byte::sbc::AppendU32(out->types_bytes, row.field_start);
    Simple::Byte::sbc::AppendU32(out->types_bytes, row.field_count);
  }

  for (const auto& row : fields) {
    Simple::Byte::sbc::AppendU32(out->fields_bytes, row.name_str);
    Simple::Byte::sbc::AppendU32(out->fields_bytes, row.type_id);
    Simple::Byte::sbc::AppendU32(out->fields_bytes, row.offset);
    Simple::Byte::sbc::AppendU32(out->fields_bytes, row.flags);
  }

  out->const_pool = const_pool;

  std::unordered_map<std::string, uint32_t> func_ids;
  for (size_t i = 0; i < text.functions.size(); ++i) {
    func_ids[text.functions[i].name] = static_cast<uint32_t>(i);
  }

  auto resolve_type_id = [&](const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    auto it = type_ids.find(token);
    if (it == type_ids.end()) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_sig_id = [&](const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    auto it = sig_ids.find(token);
    if (it == sig_ids.end()) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_func_id = [&](const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    auto it = func_ids.find(token);
    if (it == func_ids.end()) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_local = [&](const IrTextFunction& fn, const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    auto it = fn.locals_map.find(token);
    if (it == fn.locals_map.end()) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_field_id = [&](const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    size_t dot = token.find('.');
    if (dot != std::string::npos) {
      std::string type = token.substr(0, dot);
      std::string field = token.substr(dot + 1);
      auto type_it = type_ids.find(type);
      if (type_it == type_ids.end()) return false;
      uint32_t type_id = type_it->second;
      if (type_id >= field_ids_by_type.size()) return false;
      auto field_it = field_ids_by_type[type_id].find(field);
      if (field_it == field_ids_by_type[type_id].end()) return false;
      *out_id = field_it->second;
      return true;
    }
    auto it = field_ids.find(token);
    if (it == field_ids.end() || it->second == kAmbiguousField) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_const_string_id = [&](const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    auto it = const_string_ids.find(token);
    if (it == const_string_ids.end()) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_intrinsic_id = [&](const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    auto it = intrinsic_ids.find(token);
    if (it == intrinsic_ids.end()) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_syscall_id = [&](const std::string& token, uint32_t* out_id) -> bool {
    uint64_t value = 0;
    if (ParseUint(token, &value)) {
      if (!FitsUnsigned<uint32_t>(value)) return false;
      *out_id = static_cast<uint32_t>(value);
      return true;
    }
    auto it = syscall_ids.find(token);
    if (it == syscall_ids.end()) return false;
    *out_id = it->second;
    return true;
  };

  auto resolve_named_const = [&](const std::string& expected_kind,
                                 const std::string& token,
                                 std::string* out_value) -> bool {
    auto it = const_map.find(token);
    if (it == const_map.end()) return false;
    if (Lower(it->second.kind) != expected_kind) return false;
    if (out_value) *out_value = it->second.value;
    return true;
  };

  for (const auto& fn : text.functions) {
    uint32_t func_sig_id = fn.sig_id;
    if (fn.sig_is_name) {
      auto sig_it = sig_ids.find(fn.sig_name);
      if (sig_it == sig_ids.end()) {
        if (error) *error = "unknown sig name: " + fn.sig_name;
        return false;
      }
      func_sig_id = sig_it->second;
    }
    Simple::IR::IrBuilder builder;
    std::unordered_map<std::string, IrLabel> labels;
    for (const auto& inst : fn.insts) {
      if (inst.kind == InstKind::Label && !inst.label.empty()) {
        if (labels.find(inst.label) == labels.end()) {
          labels[inst.label] = builder.CreateLabel();
        }
      }
    }

    for (const auto& inst : fn.insts) {
      if (inst.kind == InstKind::Label) {
        auto it = labels.find(inst.label);
        if (it == labels.end()) {
          if (error) *error = "label missing: " + inst.label;
          return false;
        }
        if (!builder.BindLabel(it->second, error)) return false;
        continue;
      }

      std::string op = Lower(inst.op);
      if (op == "enter") {
        uint64_t locals = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &locals)) {
          if (error) *error = "enter expects locals";
          return false;
        }
        builder.EmitEnter(static_cast<uint16_t>(locals));
        continue;
      }
      if (op == "ret") {
        builder.EmitRet();
        continue;
      }
      if (op == "nop") {
        builder.EmitOp(Simple::IR::OpCode::Nop);
        continue;
      }
      if (op == "pop") {
        builder.EmitPop();
        continue;
      }
      if (op == "dup") {
        builder.EmitDup();
        continue;
      }
      if (op == "dup2") {
        builder.EmitDup2();
        continue;
      }
      if (op == "swap") {
        builder.EmitSwap();
        continue;
      }
      if (op == "rot") {
        builder.EmitRot();
        continue;
      }
      if (op == "const.i32") {
        int64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.i32 expects value";
          return false;
        }
        if (!ParseInt(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("i32", inst.args[0], &named) ||
              !ParseInt(named, &value)) {
            if (error) *error = "const.i32 expects value";
            return false;
          }
        }
        if (!FitsSigned<int32_t>(value)) {
          if (error) *error = "const.i32 out of range";
          return false;
        }
        builder.EmitConstI32(static_cast<int32_t>(value));
        continue;
      }
      if (op == "const.i8") {
        int64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.i8 expects value";
          return false;
        }
        if (!ParseInt(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("i8", inst.args[0], &named) ||
              !ParseInt(named, &value)) {
            if (error) *error = "const.i8 expects value";
            return false;
          }
        }
        if (!FitsSigned<int8_t>(value)) {
          if (error) *error = "const.i8 out of range";
          return false;
        }
        builder.EmitConstI8(static_cast<int8_t>(value));
        continue;
      }
      if (op == "const.i16") {
        int64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.i16 expects value";
          return false;
        }
        if (!ParseInt(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("i16", inst.args[0], &named) ||
              !ParseInt(named, &value)) {
            if (error) *error = "const.i16 expects value";
            return false;
          }
        }
        if (!FitsSigned<int16_t>(value)) {
          if (error) *error = "const.i16 out of range";
          return false;
        }
        builder.EmitConstI16(static_cast<int16_t>(value));
        continue;
      }
      if (op == "const.i64") {
        int64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.i64 expects value";
          return false;
        }
        if (!ParseInt(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("i64", inst.args[0], &named) ||
              !ParseInt(named, &value)) {
            if (error) *error = "const.i64 expects value";
            return false;
          }
        }
        if (!FitsSigned<int64_t>(value)) {
          if (error) *error = "const.i64 out of range";
          return false;
        }
        builder.EmitConstI64(value);
        continue;
      }
      if (op == "const.u8") {
        uint64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.u8 expects value";
          return false;
        }
        if (!ParseUint(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("u8", inst.args[0], &named) ||
              !ParseUint(named, &value)) {
            if (error) *error = "const.u8 expects value";
            return false;
          }
        }
        if (!FitsUnsigned<uint8_t>(value)) {
          if (error) *error = "const.u8 out of range";
          return false;
        }
        builder.EmitConstU8(static_cast<uint8_t>(value));
        continue;
      }
      if (op == "const.u16") {
        uint64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.u16 expects value";
          return false;
        }
        if (!ParseUint(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("u16", inst.args[0], &named) ||
              !ParseUint(named, &value)) {
            if (error) *error = "const.u16 expects value";
            return false;
          }
        }
        if (!FitsUnsigned<uint16_t>(value)) {
          if (error) *error = "const.u16 out of range";
          return false;
        }
        builder.EmitConstU16(static_cast<uint16_t>(value));
        continue;
      }
      if (op == "const.u32") {
        uint64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.u32 expects value";
          return false;
        }
        if (!ParseUint(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("u32", inst.args[0], &named) ||
              !ParseUint(named, &value)) {
            if (error) *error = "const.u32 expects value";
            return false;
          }
        }
        if (!FitsUnsigned<uint32_t>(value)) {
          if (error) *error = "const.u32 out of range";
          return false;
        }
        builder.EmitConstU32(static_cast<uint32_t>(value));
        continue;
      }
      if (op == "const.u64") {
        uint64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.u64 expects value";
          return false;
        }
        if (!ParseUint(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("u64", inst.args[0], &named) ||
              !ParseUint(named, &value)) {
            if (error) *error = "const.u64 expects value";
            return false;
          }
        }
        if (!FitsUnsigned<uint64_t>(value)) {
          if (error) *error = "const.u64 out of range";
          return false;
        }
        builder.EmitConstU64(value);
        continue;
      }
      if (op == "const.f32") {
        double value = 0.0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.f32 expects value";
          return false;
        }
        if (!ParseFloat(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("f32", inst.args[0], &named) ||
              !ParseFloat(named, &value)) {
            if (error) *error = "const.f32 expects value";
            return false;
          }
        }
        builder.EmitConstF32(static_cast<float>(value));
        continue;
      }
      if (op == "const.f64") {
        double value = 0.0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.f64 expects value";
          return false;
        }
        if (!ParseFloat(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("f64", inst.args[0], &named) ||
              !ParseFloat(named, &value)) {
            if (error) *error = "const.f64 expects value";
            return false;
          }
        }
        builder.EmitConstF64(value);
        continue;
      }
      if (op == "const.bool") {
        uint64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.bool expects value";
          return false;
        }
        if (!ParseUint(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("bool", inst.args[0], &named) ||
              !ParseUint(named, &value)) {
            if (error) *error = "const.bool expects value";
            return false;
          }
        }
        builder.EmitConstBool(value != 0);
        continue;
      }
      if (op == "const.char") {
        uint64_t value = 0;
        if (inst.args.size() != 1) {
          if (error) *error = "const.char expects value";
          return false;
        }
        if (!ParseUint(inst.args[0], &value)) {
          std::string named;
          if (!resolve_named_const("char", inst.args[0], &named) ||
              !ParseUint(named, &value)) {
            if (error) *error = "const.char expects value";
            return false;
          }
        }
        builder.EmitConstChar(static_cast<uint16_t>(value));
        continue;
      }
      if (op == "const.string") {
        uint32_t const_id = 0;
        if (inst.args.size() != 1 || !resolve_const_string_id(inst.args[0], &const_id)) {
          if (error) *error = "const.string expects const_id";
          return false;
        }
        builder.EmitConstString(const_id);
        continue;
      }
      if (op == "const.null") {
        builder.EmitConstNull();
        continue;
      }
      if (op == "add.i32") {
        builder.EmitAddI32();
        continue;
      }
      if (op == "sub.i32") {
        builder.EmitSubI32();
        continue;
      }
      if (op == "mul.i32") {
        builder.EmitMulI32();
        continue;
      }
      if (op == "div.i32") {
        builder.EmitDivI32();
        continue;
      }
      if (op == "mod.i32") {
        builder.EmitModI32();
        continue;
      }
      if (op == "add.i64") {
        builder.EmitAddI64();
        continue;
      }
      if (op == "sub.i64") {
        builder.EmitSubI64();
        continue;
      }
      if (op == "mul.i64") {
        builder.EmitMulI64();
        continue;
      }
      if (op == "div.i64") {
        builder.EmitDivI64();
        continue;
      }
      if (op == "mod.i64") {
        builder.EmitModI64();
        continue;
      }
      if (op == "add.f32") {
        builder.EmitAddF32();
        continue;
      }
      if (op == "sub.f32") {
        builder.EmitSubF32();
        continue;
      }
      if (op == "mul.f32") {
        builder.EmitMulF32();
        continue;
      }
      if (op == "div.f32") {
        builder.EmitDivF32();
        continue;
      }
      if (op == "add.f64") {
        builder.EmitAddF64();
        continue;
      }
      if (op == "sub.f64") {
        builder.EmitSubF64();
        continue;
      }
      if (op == "mul.f64") {
        builder.EmitMulF64();
        continue;
      }
      if (op == "div.f64") {
        builder.EmitDivF64();
        continue;
      }
      if (op == "add.u32") {
        builder.EmitAddU32();
        continue;
      }
      if (op == "sub.u32") {
        builder.EmitSubU32();
        continue;
      }
      if (op == "mul.u32") {
        builder.EmitMulU32();
        continue;
      }
      if (op == "div.u32") {
        builder.EmitDivU32();
        continue;
      }
      if (op == "mod.u32") {
        builder.EmitModU32();
        continue;
      }
      if (op == "add.u64") {
        builder.EmitAddU64();
        continue;
      }
      if (op == "sub.u64") {
        builder.EmitSubU64();
        continue;
      }
      if (op == "mul.u64") {
        builder.EmitMulU64();
        continue;
      }
      if (op == "div.u64") {
        builder.EmitDivU64();
        continue;
      }
      if (op == "mod.u64") {
        builder.EmitModU64();
        continue;
      }
      if (op == "and.i32") {
        builder.EmitAndI32();
        continue;
      }
      if (op == "or.i32") {
        builder.EmitOrI32();
        continue;
      }
      if (op == "xor.i32") {
        builder.EmitXorI32();
        continue;
      }
      if (op == "shl.i32") {
        builder.EmitShlI32();
        continue;
      }
      if (op == "shr.i32") {
        builder.EmitShrI32();
        continue;
      }
      if (op == "and.i64") {
        builder.EmitAndI64();
        continue;
      }
      if (op == "or.i64") {
        builder.EmitOrI64();
        continue;
      }
      if (op == "xor.i64") {
        builder.EmitXorI64();
        continue;
      }
      if (op == "shl.i64") {
        builder.EmitShlI64();
        continue;
      }
      if (op == "shr.i64") {
        builder.EmitShrI64();
        continue;
      }
      if (op == "neg.i32") {
        builder.EmitNegI32();
        continue;
      }
      if (op == "neg.i64") {
        builder.EmitNegI64();
        continue;
      }
      if (op == "neg.f32") {
        builder.EmitNegF32();
        continue;
      }
      if (op == "neg.f64") {
        builder.EmitNegF64();
        continue;
      }
      if (op == "neg.i8") {
        builder.EmitNegI8();
        continue;
      }
      if (op == "neg.i16") {
        builder.EmitNegI16();
        continue;
      }
      if (op == "neg.u8") {
        builder.EmitNegU8();
        continue;
      }
      if (op == "neg.u16") {
        builder.EmitNegU16();
        continue;
      }
      if (op == "neg.u32") {
        builder.EmitNegU32();
        continue;
      }
      if (op == "neg.u64") {
        builder.EmitNegU64();
        continue;
      }
      if (op == "inc.i32") {
        builder.EmitIncI32();
        continue;
      }
      if (op == "dec.i32") {
        builder.EmitDecI32();
        continue;
      }
      if (op == "inc.i64") {
        builder.EmitIncI64();
        continue;
      }
      if (op == "dec.i64") {
        builder.EmitDecI64();
        continue;
      }
      if (op == "inc.f32") {
        builder.EmitIncF32();
        continue;
      }
      if (op == "dec.f32") {
        builder.EmitDecF32();
        continue;
      }
      if (op == "inc.f64") {
        builder.EmitIncF64();
        continue;
      }
      if (op == "dec.f64") {
        builder.EmitDecF64();
        continue;
      }
      if (op == "inc.u32") {
        builder.EmitIncU32();
        continue;
      }
      if (op == "dec.u32") {
        builder.EmitDecU32();
        continue;
      }
      if (op == "inc.u64") {
        builder.EmitIncU64();
        continue;
      }
      if (op == "dec.u64") {
        builder.EmitDecU64();
        continue;
      }
      if (op == "inc.i8") {
        builder.EmitIncI8();
        continue;
      }
      if (op == "dec.i8") {
        builder.EmitDecI8();
        continue;
      }
      if (op == "inc.i16") {
        builder.EmitIncI16();
        continue;
      }
      if (op == "dec.i16") {
        builder.EmitDecI16();
        continue;
      }
      if (op == "inc.u8") {
        builder.EmitIncU8();
        continue;
      }
      if (op == "dec.u8") {
        builder.EmitDecU8();
        continue;
      }
      if (op == "inc.u16") {
        builder.EmitIncU16();
        continue;
      }
      if (op == "dec.u16") {
        builder.EmitDecU16();
        continue;
      }
      if (op == "cmp.eq.i32") {
        builder.EmitCmpEqI32();
        continue;
      }
      if (op == "cmp.ne.i32") {
        builder.EmitCmpNeI32();
        continue;
      }
      if (op == "cmp.lt.i32") {
        builder.EmitCmpLtI32();
        continue;
      }
      if (op == "cmp.le.i32") {
        builder.EmitCmpLeI32();
        continue;
      }
      if (op == "cmp.gt.i32") {
        builder.EmitCmpGtI32();
        continue;
      }
      if (op == "cmp.ge.i32") {
        builder.EmitCmpGeI32();
        continue;
      }
      if (op == "cmp.eq.i64") {
        builder.EmitCmpEqI64();
        continue;
      }
      if (op == "cmp.ne.i64") {
        builder.EmitCmpNeI64();
        continue;
      }
      if (op == "cmp.lt.i64") {
        builder.EmitCmpLtI64();
        continue;
      }
      if (op == "cmp.le.i64") {
        builder.EmitCmpLeI64();
        continue;
      }
      if (op == "cmp.gt.i64") {
        builder.EmitCmpGtI64();
        continue;
      }
      if (op == "cmp.ge.i64") {
        builder.EmitCmpGeI64();
        continue;
      }
      if (op == "cmp.eq.u32") {
        builder.EmitCmpEqU32();
        continue;
      }
      if (op == "cmp.ne.u32") {
        builder.EmitCmpNeU32();
        continue;
      }
      if (op == "cmp.lt.u32") {
        builder.EmitCmpLtU32();
        continue;
      }
      if (op == "cmp.le.u32") {
        builder.EmitCmpLeU32();
        continue;
      }
      if (op == "cmp.gt.u32") {
        builder.EmitCmpGtU32();
        continue;
      }
      if (op == "cmp.ge.u32") {
        builder.EmitCmpGeU32();
        continue;
      }
      if (op == "cmp.eq.u64") {
        builder.EmitCmpEqU64();
        continue;
      }
      if (op == "cmp.ne.u64") {
        builder.EmitCmpNeU64();
        continue;
      }
      if (op == "cmp.lt.u64") {
        builder.EmitCmpLtU64();
        continue;
      }
      if (op == "cmp.le.u64") {
        builder.EmitCmpLeU64();
        continue;
      }
      if (op == "cmp.gt.u64") {
        builder.EmitCmpGtU64();
        continue;
      }
      if (op == "cmp.ge.u64") {
        builder.EmitCmpGeU64();
        continue;
      }
      if (op == "cmp.eq.f32") {
        builder.EmitCmpEqF32();
        continue;
      }
      if (op == "cmp.ne.f32") {
        builder.EmitCmpNeF32();
        continue;
      }
      if (op == "cmp.lt.f32") {
        builder.EmitCmpLtF32();
        continue;
      }
      if (op == "cmp.le.f32") {
        builder.EmitCmpLeF32();
        continue;
      }
      if (op == "cmp.gt.f32") {
        builder.EmitCmpGtF32();
        continue;
      }
      if (op == "cmp.ge.f32") {
        builder.EmitCmpGeF32();
        continue;
      }
      if (op == "cmp.eq.f64") {
        builder.EmitCmpEqF64();
        continue;
      }
      if (op == "cmp.ne.f64") {
        builder.EmitCmpNeF64();
        continue;
      }
      if (op == "cmp.lt.f64") {
        builder.EmitCmpLtF64();
        continue;
      }
      if (op == "cmp.le.f64") {
        builder.EmitCmpLeF64();
        continue;
      }
      if (op == "cmp.gt.f64") {
        builder.EmitCmpGtF64();
        continue;
      }
      if (op == "cmp.ge.f64") {
        builder.EmitCmpGeF64();
        continue;
      }
      if (op == "bool.not") {
        builder.EmitBoolNot();
        continue;
      }
      if (op == "bool.and") {
        builder.EmitBoolAnd();
        continue;
      }
      if (op == "bool.or") {
        builder.EmitBoolOr();
        continue;
      }
      if (op == "jmp") {
        if (inst.args.size() != 1) {
          if (error) *error = "jmp expects label";
          return false;
        }
        if (!IsValidLabelName(inst.args[0])) {
          if (error) *error = "invalid label: " + inst.args[0];
          return false;
        }
        auto it = labels.find(inst.args[0]);
        if (it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        builder.EmitJmp(it->second);
        continue;
      }
      if (op == "jmp.true") {
        if (inst.args.size() != 1) {
          if (error) *error = "jmp.true expects label";
          return false;
        }
        if (!IsValidLabelName(inst.args[0])) {
          if (error) *error = "invalid label: " + inst.args[0];
          return false;
        }
        auto it = labels.find(inst.args[0]);
        if (it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        builder.EmitJmpTrue(it->second);
        continue;
      }
      if (op == "jmp.false") {
        if (inst.args.size() != 1) {
          if (error) *error = "jmp.false expects label";
          return false;
        }
        if (!IsValidLabelName(inst.args[0])) {
          if (error) *error = "invalid label: " + inst.args[0];
          return false;
        }
        auto it = labels.find(inst.args[0]);
        if (it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        builder.EmitJmpFalse(it->second);
        continue;
      }
      if (op == "jmptable") {
        if (inst.args.size() < 2) {
          if (error) *error = "jmptable expects default and cases";
          return false;
        }
        if (!IsValidLabelName(inst.args[0])) {
          if (error) *error = "invalid label: " + inst.args[0];
          return false;
        }
        auto def_it = labels.find(inst.args[0]);
        if (def_it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        IrLabel def = def_it->second;
        std::vector<IrLabel> cases;
        for (size_t i = 1; i < inst.args.size(); ++i) {
          if (!IsValidLabelName(inst.args[i])) {
            if (error) *error = "invalid label: " + inst.args[i];
            return false;
          }
          auto it = labels.find(inst.args[i]);
          if (it == labels.end()) {
            if (error) *error = "unknown label: " + inst.args[i];
            return false;
          }
          cases.push_back(it->second);
        }
        builder.EmitJmpTable(cases, def);
        continue;
      }
      if (op == "call") {
        if (inst.args.size() != 2) {
          if (error) *error = "call expects func_id arg_count";
          return false;
        }
        uint32_t func_id = 0;
        uint64_t arg_count = 0;
        if (!resolve_func_id(inst.args[0], &func_id) || !ParseUint(inst.args[1], &arg_count)) {
          if (error) *error = "call expects numeric args";
          return false;
        }
        builder.EmitCall(func_id, static_cast<uint8_t>(arg_count));
        continue;
      }
      if (op == "call.indirect") {
        if (inst.args.size() != 2) {
          if (error) *error = "call.indirect expects sig_id arg_count";
          return false;
        }
        uint32_t sig_id = 0;
        uint64_t arg_count = 0;
        if (!resolve_sig_id(inst.args[0], &sig_id) || !ParseUint(inst.args[1], &arg_count)) {
          if (error) *error = "call.indirect expects numeric args";
          return false;
        }
        builder.EmitCallIndirect(sig_id, static_cast<uint8_t>(arg_count));
        continue;
      }
      if (op == "tailcall") {
        if (inst.args.size() != 2) {
          if (error) *error = "tailcall expects func_id arg_count";
          return false;
        }
        uint32_t func_id = 0;
        uint64_t arg_count = 0;
        if (!resolve_func_id(inst.args[0], &func_id) || !ParseUint(inst.args[1], &arg_count)) {
          if (error) *error = "tailcall expects numeric args";
          return false;
        }
        builder.EmitTailCall(func_id, static_cast<uint8_t>(arg_count));
        continue;
      }
      if (op == "conv.i32.i64") {
        builder.EmitConvI32ToI64();
        continue;
      }
      if (op == "conv.i64.i32") {
        builder.EmitConvI64ToI32();
        continue;
      }
      if (op == "conv.i32.f32") {
        builder.EmitConvI32ToF32();
        continue;
      }
      if (op == "conv.i32.f64") {
        builder.EmitConvI32ToF64();
        continue;
      }
      if (op == "conv.f32.i32") {
        builder.EmitConvF32ToI32();
        continue;
      }
      if (op == "conv.f64.i32") {
        builder.EmitConvF64ToI32();
        continue;
      }
      if (op == "conv.f32.f64") {
        builder.EmitConvF32ToF64();
        continue;
      }
      if (op == "conv.f64.f32") {
        builder.EmitConvF64ToF32();
        continue;
      }
      if (op == "ldloc" || op == "load.local") {
        uint32_t index = 0;
        if (inst.args.size() != 1 || !resolve_local(fn, inst.args[0], &index)) {
          if (error) *error = "ldloc expects index";
          return false;
        }
        builder.EmitLoadLocal(index);
        continue;
      }
      if (op == "stloc" || op == "store.local") {
        uint32_t index = 0;
        if (inst.args.size() != 1 || !resolve_local(fn, inst.args[0], &index)) {
          if (error) *error = "stloc expects index";
          return false;
        }
        builder.EmitStoreLocal(index);
        continue;
      }
      if (op == "callcheck") {
        builder.EmitCallCheck();
        continue;
      }
      if (op == "intrinsic") {
        uint32_t id = 0;
        if (inst.args.size() != 1 || !resolve_intrinsic_id(inst.args[0], &id)) {
          if (error) *error = "intrinsic expects id";
          return false;
        }
        builder.EmitIntrinsic(id);
        continue;
      }
      if (op == "syscall") {
        uint32_t id = 0;
        if (inst.args.size() != 1 || !resolve_syscall_id(inst.args[0], &id)) {
          if (error) *error = "syscall expects id";
          return false;
        }
        builder.EmitSysCall(id);
        continue;
      }
      if (op == "newobj") {
        uint32_t type_id = 0;
        if (inst.args.size() != 1 || !resolve_type_id(inst.args[0], &type_id)) {
          if (error) *error = "newobj expects type_id";
          return false;
        }
        builder.EmitNewObject(type_id);
        continue;
      }
      if (op == "ldfld") {
        uint32_t field_id = 0;
        if (inst.args.size() != 1 || !resolve_field_id(inst.args[0], &field_id)) {
          if (error) *error = "ldfld expects field_id";
          return false;
        }
        builder.EmitLoadField(field_id);
        continue;
      }
      if (op == "stfld") {
        uint32_t field_id = 0;
        if (inst.args.size() != 1 || !resolve_field_id(inst.args[0], &field_id)) {
          if (error) *error = "stfld expects field_id";
          return false;
        }
        builder.EmitStoreField(field_id);
        continue;
      }
      if (op == "typeof") {
        builder.EmitTypeOf();
        continue;
      }
      if (op == "isnull") {
        builder.EmitIsNull();
        continue;
      }
      if (op == "ref.eq") {
        builder.EmitRefEq();
        continue;
      }
      if (op == "ref.ne") {
        builder.EmitRefNe();
        continue;
      }
      if (op == "newclosure") {
        uint32_t method_id = 0;
        uint64_t upvalues = 0;
        if (inst.args.size() != 2 || !resolve_func_id(inst.args[0], &method_id) ||
            !ParseUint(inst.args[1], &upvalues)) {
          if (error) *error = "newclosure expects method_id upvalue_count";
          return false;
        }
        builder.EmitNewClosure(method_id, static_cast<uint8_t>(upvalues));
        continue;
      }
      if (op == "newarray") {
        uint32_t type_id = 0;
        uint64_t length = 0;
        if (inst.args.size() != 2 || !resolve_type_id(inst.args[0], &type_id) ||
            !ParseUint(inst.args[1], &length)) {
          if (error) *error = "newarray expects type_id length";
          return false;
        }
        builder.EmitNewArray(type_id, static_cast<uint32_t>(length));
        continue;
      }
      if (op == "array.len") {
        builder.EmitArrayLen();
        continue;
      }
      if (op == "array.get.i32") {
        builder.EmitArrayGetI32();
        continue;
      }
      if (op == "array.set.i32") {
        builder.EmitArraySetI32();
        continue;
      }
      if (op == "array.get.i64") {
        builder.EmitArrayGetI64();
        continue;
      }
      if (op == "array.set.i64") {
        builder.EmitArraySetI64();
        continue;
      }
      if (op == "array.get.f32") {
        builder.EmitArrayGetF32();
        continue;
      }
      if (op == "array.set.f32") {
        builder.EmitArraySetF32();
        continue;
      }
      if (op == "array.get.f64") {
        builder.EmitArrayGetF64();
        continue;
      }
      if (op == "array.set.f64") {
        builder.EmitArraySetF64();
        continue;
      }
      if (op == "array.get.ref") {
        builder.EmitArrayGetRef();
        continue;
      }
      if (op == "array.set.ref") {
        builder.EmitArraySetRef();
        continue;
      }
      if (op == "newlist") {
        uint32_t type_id = 0;
        uint64_t cap = 0;
        if (inst.args.size() != 2 || !resolve_type_id(inst.args[0], &type_id) ||
            !ParseUint(inst.args[1], &cap)) {
          if (error) *error = "newlist expects type_id capacity";
          return false;
        }
        builder.EmitNewList(type_id, static_cast<uint32_t>(cap));
        continue;
      }
      if (op == "list.len") {
        builder.EmitListLen();
        continue;
      }
      if (op == "list.get.i32") {
        builder.EmitListGetI32();
        continue;
      }
      if (op == "list.set.i32") {
        builder.EmitListSetI32();
        continue;
      }
      if (op == "list.push.i32") {
        builder.EmitListPushI32();
        continue;
      }
      if (op == "list.pop.i32") {
        builder.EmitListPopI32();
        continue;
      }
      if (op == "list.get.i64") {
        builder.EmitListGetI64();
        continue;
      }
      if (op == "list.set.i64") {
        builder.EmitListSetI64();
        continue;
      }
      if (op == "list.push.i64") {
        builder.EmitListPushI64();
        continue;
      }
      if (op == "list.pop.i64") {
        builder.EmitListPopI64();
        continue;
      }
      if (op == "list.get.f32") {
        builder.EmitListGetF32();
        continue;
      }
      if (op == "list.set.f32") {
        builder.EmitListSetF32();
        continue;
      }
      if (op == "list.push.f32") {
        builder.EmitListPushF32();
        continue;
      }
      if (op == "list.pop.f32") {
        builder.EmitListPopF32();
        continue;
      }
      if (op == "list.get.f64") {
        builder.EmitListGetF64();
        continue;
      }
      if (op == "list.set.f64") {
        builder.EmitListSetF64();
        continue;
      }
      if (op == "list.push.f64") {
        builder.EmitListPushF64();
        continue;
      }
      if (op == "list.pop.f64") {
        builder.EmitListPopF64();
        continue;
      }
      if (op == "list.get.ref") {
        builder.EmitListGetRef();
        continue;
      }
      if (op == "list.set.ref") {
        builder.EmitListSetRef();
        continue;
      }
      if (op == "list.push.ref") {
        builder.EmitListPushRef();
        continue;
      }
      if (op == "list.pop.ref") {
        builder.EmitListPopRef();
        continue;
      }
      if (op == "list.insert.i32") {
        builder.EmitListInsertI32();
        continue;
      }
      if (op == "list.remove.i32") {
        builder.EmitListRemoveI32();
        continue;
      }
      if (op == "list.insert.i64") {
        builder.EmitListInsertI64();
        continue;
      }
      if (op == "list.remove.i64") {
        builder.EmitListRemoveI64();
        continue;
      }
      if (op == "list.insert.f32") {
        builder.EmitListInsertF32();
        continue;
      }
      if (op == "list.remove.f32") {
        builder.EmitListRemoveF32();
        continue;
      }
      if (op == "list.insert.f64") {
        builder.EmitListInsertF64();
        continue;
      }
      if (op == "list.remove.f64") {
        builder.EmitListRemoveF64();
        continue;
      }
      if (op == "list.insert.ref") {
        builder.EmitListInsertRef();
        continue;
      }
      if (op == "list.remove.ref") {
        builder.EmitListRemoveRef();
        continue;
      }
      if (op == "list.clear") {
        builder.EmitListClear();
        continue;
      }
      if (op == "string.len") {
        builder.EmitStringLen();
        continue;
      }
      if (op == "string.concat") {
        builder.EmitStringConcat();
        continue;
      }
      if (op == "string.get.char") {
        builder.EmitStringGetChar();
        continue;
      }
      if (op == "string.slice") {
        builder.EmitStringSlice();
        continue;
      }
      if (op == "ldglob" || op == "load.global") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "ldglob expects index";
          return false;
        }
        builder.EmitLoadGlobal(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "stglob" || op == "store.global") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "stglob expects index";
          return false;
        }
        builder.EmitStoreGlobal(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "ldupv" || op == "load.upvalue") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "ldupv expects index";
          return false;
        }
        builder.EmitLoadUpvalue(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "stupv" || op == "store.upvalue") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "stupv expects index";
          return false;
        }
        builder.EmitStoreUpvalue(static_cast<uint32_t>(index));
        continue;
      }

      if (error) *error = "unknown op: " + inst.op;
      return false;
    }

    std::vector<uint8_t> code;
    if (!builder.Finish(&code, error)) return false;
    Simple::IR::IrFunction out_fn;
    out_fn.code = std::move(code);
    out_fn.local_count = fn.locals;
    out_fn.stack_max = fn.stack_max;
    out_fn.sig_id = func_sig_id;
    out->functions.push_back(std::move(out_fn));
  }

  return true;
}

} // namespace Simple::IR::Text
