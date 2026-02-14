#include "lang_sir.h"

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lang_parser.h"
#include "lang_reserved.h"
#include "lang_validate.h"
#include "intrinsic_ids.h"

namespace Simple::Lang {
namespace {

struct EmitState {
  std::ostringstream* out = nullptr;
  std::string* error = nullptr;

  std::unordered_map<std::string, std::string> string_consts;
  std::vector<std::string> const_lines;
  uint32_t string_index = 0;

  std::unordered_map<std::string, TypeRef> local_types;
  std::unordered_map<std::string, std::string> local_dl_modules;
  std::unordered_map<std::string, uint16_t> local_indices;
  uint16_t next_local = 0;

  std::unordered_map<std::string, uint32_t> func_ids;
  std::unordered_map<std::string, TypeRef> func_returns;
  std::unordered_map<std::string, std::vector<TypeRef>> func_params;
  std::unordered_map<std::string, std::string> module_func_names;
  std::unordered_map<std::string, std::string> artifact_method_names;
  uint32_t base_func_count = 0;
  uint32_t lambda_counter = 0;
  std::vector<FuncDecl> lambda_funcs;
  std::unordered_map<std::string, std::string> proc_sig_names;
  std::vector<std::string> proc_sig_lines;
  std::unordered_set<std::string> reserved_imports;
  std::unordered_map<std::string, std::string> reserved_import_aliases;
  std::unordered_map<std::string, std::string> extern_ids;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> extern_ids_by_module;
  std::unordered_map<std::string, std::vector<TypeRef>> extern_params;
  std::unordered_map<std::string, TypeRef> extern_returns;
  std::unordered_map<std::string, std::unordered_map<std::string, std::vector<TypeRef>>> extern_params_by_module;
  std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>> extern_returns_by_module;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dl_call_import_ids_by_module;
  std::unordered_map<std::string, uint32_t> global_indices;
  std::unordered_map<std::string, TypeRef> global_types;
  std::unordered_map<std::string, Mutability> global_mutability;
  std::unordered_map<std::string, std::string> global_dl_modules;
  std::string global_init_func_name;
  std::vector<const VarDecl*> global_decls;

  struct ImportItem {
    std::string name;
    std::string module;
    std::string symbol;
    std::string sig_name;
    uint32_t flags = 0;
    std::vector<TypeRef> params;
    TypeRef ret;
  };
  std::vector<ImportItem> imports;

  struct FieldLayout {
    uint32_t offset = 0;
    std::string name;
    TypeRef type;
    std::string sir_type;
  };
  struct ArtifactLayout {
    uint32_t size = 0;
    std::vector<FieldLayout> fields;
    std::unordered_map<std::string, size_t> field_index;
  };

  std::unordered_map<std::string, const ArtifactDecl*> artifacts;
  std::unordered_map<std::string, ArtifactLayout> artifact_layouts;
  std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> enum_values;

  uint32_t stack_cur = 0;
  uint32_t stack_max = 0;
  bool saw_return = false;
  std::string current_func;

  uint32_t label_counter = 0;
  struct LoopLabels {
    std::string break_label;
    std::string continue_label;
  };
  std::vector<LoopLabels> loop_stack;
};

struct FuncItem {
  const FuncDecl* decl = nullptr;
  std::string emit_name;
  std::string display_name;
  bool has_self = false;
  TypeRef self_type;
  const std::vector<Stmt>* script_body = nullptr;
};

bool PushStack(EmitState& st, uint32_t count);
bool PopStack(EmitState& st, uint32_t count);
bool AddStringConst(EmitState& st, const std::string& value, std::string* out_name);
bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
              std::string* error);

bool IsIntegralType(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "i128" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "u128";
}

bool IsIntegerLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Integer;
}

bool IsFloatLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Float;
}

bool IsFloatType(const std::string& name) {
  return name == "f32" || name == "f64";
}

bool IsNumericType(const std::string& name) {
  return IsIntegralType(name) || IsFloatType(name);
}

bool IsPrimitiveCastName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
         name == "f32" || name == "f64" || name == "bool" || name == "char";
}

bool GetAtCastTargetName(const std::string& name, std::string* out_target) {
  if (name.size() < 2 || name[0] != '@') return false;
  const std::string target = name.substr(1);
  if (!IsPrimitiveCastName(target)) return false;
  if (out_target) *out_target = target;
  return true;
}

enum class CastVmKind : uint8_t { Invalid, I32, I64, F32, F64 };

CastVmKind GetCastVmKind(const std::string& type_name) {
  if (type_name == "i8" || type_name == "i16" || type_name == "i32" ||
      type_name == "u8" || type_name == "u16" || type_name == "u32" ||
      type_name == "bool" || type_name == "char") {
    return CastVmKind::I32;
  }
  if (type_name == "i64" || type_name == "u64") return CastVmKind::I64;
  if (type_name == "f32") return CastVmKind::F32;
  if (type_name == "f64") return CastVmKind::F64;
  return CastVmKind::Invalid;
}

bool IsIoPrintName(const std::string& name) {
  return name == "print" || name == "println";
}

TypeRef MakeTypeRef(const char* name) {
  TypeRef out;
  out.name = name;
  return out;
}

std::string NormalizeCoreDlMember(const std::string& name) {
  if (name == "Open") return "open";
  if (name == "Sym") return "sym";
  if (name == "Close") return "close";
  if (name == "LastError") return "last_error";
  if (name == "CallI32") return "call_i32";
  if (name == "CallI64") return "call_i64";
  if (name == "CallF32") return "call_f32";
  if (name == "CallF64") return "call_f64";
  if (name == "CallStr0") return "call_str0";
  return name;
}

bool GetModuleNameFromExpr(const Expr& base, std::string* out) {
  if (!out) return false;
  if (base.kind == ExprKind::Identifier) {
    *out = base.text;
    return true;
  }
  if (base.kind == ExprKind::Member && base.op == "." && !base.children.empty()) {
    const Expr& root = base.children[0];
    if (root.kind == ExprKind::Identifier &&
        (root.text == "Core" || root.text == "System")) {
      *out = root.text + "." + base.text;
      return true;
    }
  }
  return false;
}

bool ResolveReservedModuleName(const EmitState& st,
                               const std::string& name,
                               std::string* out) {
  if (!out) return false;
  std::string canonical;
  if (CanonicalizeReservedImportPath(name, &canonical) &&
      st.reserved_imports.find(canonical) != st.reserved_imports.end()) {
    *out = canonical;
    return true;
  }
  auto it = st.reserved_import_aliases.find(name);
  if (it != st.reserved_import_aliases.end()) {
    *out = it->second;
    return true;
  }
  return false;
}

bool IsIoPrintCallExpr(const Expr& callee, const EmitState& st) {
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  if (!IsIoPrintName(callee.text)) return false;
  if (callee.children[0].kind == ExprKind::Identifier && callee.children[0].text == "IO") return true;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  std::string resolved;
  return ResolveReservedModuleName(st, module_name, &resolved) && resolved == "Core.IO";
}

bool HostIsLinux() {
#if defined(__linux__)
  return true;
#else
  return false;
#endif
}

bool HostIsMacOs() {
#if defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

bool HostIsWindows() {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

bool HostHasDl() {
  return HostIsLinux() || HostIsMacOs();
}

bool IsCoreDlOpenCallExpr(const Expr& expr, const EmitState& st) {
  if (expr.kind != ExprKind::Call || expr.children.empty()) return false;
  const Expr& callee = expr.children[0];
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  std::string resolved;
  if (!ResolveReservedModuleName(st, module_name, &resolved)) return false;
  return resolved == "Core.DL" && NormalizeCoreDlMember(callee.text) == "open";
}

bool GetDlOpenManifestModule(const Expr& expr, const EmitState& st, std::string* out_module) {
  if (!out_module) return false;
  if (!IsCoreDlOpenCallExpr(expr, st)) return false;
  if (expr.args.size() != 2) return false;
  if (expr.args[1].kind != ExprKind::Identifier) return false;
  const std::string& module = expr.args[1].text;
  auto mod_it = st.extern_returns_by_module.find(module);
  if (mod_it == st.extern_returns_by_module.end() || mod_it->second.empty()) return false;
  *out_module = module;
  return true;
}

bool ResolveDlModuleForIdentifier(const std::string& ident,
                                  const EmitState& st,
                                  std::string* out_module) {
  if (!out_module) return false;
  auto dl_local_it = st.local_dl_modules.find(ident);
  if (dl_local_it != st.local_dl_modules.end()) {
    *out_module = dl_local_it->second;
    return true;
  }
  auto dl_global_it = st.global_dl_modules.find(ident);
  if (dl_global_it != st.global_dl_modules.end()) {
    *out_module = dl_global_it->second;
    return true;
  }
  for (const auto* glob : st.global_decls) {
    if (!glob || glob->name != ident || !glob->has_init_expr) continue;
    if (GetDlOpenManifestModule(glob->init_expr, st, out_module)) {
      return true;
    }
  }
  return false;
}

bool GetCoreDlSymImportId(const EmitState& st, std::string* out_id) {
  if (!out_id) return false;
  for (const auto& entry : st.extern_ids_by_module) {
    std::string resolved;
    if (!ResolveReservedModuleName(st, entry.first, &resolved) || resolved != "Core.DL") continue;
    auto it = entry.second.find("sym");
    if (it != entry.second.end()) {
      *out_id = it->second;
      return true;
    }
  }
  return false;
}

bool IsSupportedDlAbiType(const TypeRef& type, const EmitState& st, bool allow_void) {
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) return false;
  if (type.pointer_depth > 0) return true;
  if (allow_void && type.name == "void") return true;
  if (type.name == "i8" || type.name == "i16" || type.name == "i32" || type.name == "i64" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64" ||
      type.name == "f32" || type.name == "f64" || type.name == "bool" || type.name == "char" ||
      type.name == "string") {
    return true;
  }
  if (st.enum_values.find(type.name) != st.enum_values.end()) return true;
  return st.artifacts.find(type.name) != st.artifacts.end();
}

bool GetPrintAnyTagForType(const TypeRef& type, uint32_t* out, std::string* error) {
  if (!out) return false;
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) {
    if (error) *error = "IO.print expects scalar value";
    return false;
  }
  const std::string& name = type.name;
  if (name == "i8") { *out = Simple::VM::kPrintAnyTagI8; return true; }
  if (name == "i16") { *out = Simple::VM::kPrintAnyTagI16; return true; }
  if (name == "i32") { *out = Simple::VM::kPrintAnyTagI32; return true; }
  if (name == "i64") { *out = Simple::VM::kPrintAnyTagI64; return true; }
  if (name == "u8") { *out = Simple::VM::kPrintAnyTagU8; return true; }
  if (name == "u16") { *out = Simple::VM::kPrintAnyTagU16; return true; }
  if (name == "u32") { *out = Simple::VM::kPrintAnyTagU32; return true; }
  if (name == "u64") { *out = Simple::VM::kPrintAnyTagU64; return true; }
  if (name == "f32") { *out = Simple::VM::kPrintAnyTagF32; return true; }
  if (name == "f64") { *out = Simple::VM::kPrintAnyTagF64; return true; }
  if (name == "bool") { *out = Simple::VM::kPrintAnyTagBool; return true; }
  if (name == "char") { *out = Simple::VM::kPrintAnyTagChar; return true; }
  if (name == "string") { *out = Simple::VM::kPrintAnyTagString; return true; }
  if (error) *error = "IO.print supports numeric, bool, char, or string";
  return false;
}

bool EmitPrintAnyValue(EmitState& st,
                       const Expr& arg_expr,
                       const TypeRef& arg_type,
                       std::string* error) {
  if (!EmitExpr(st, arg_expr, &arg_type, error)) return false;
  uint32_t tag = 0;
  if (!GetPrintAnyTagForType(arg_type, &tag, error)) return false;
  (*st.out) << "  const.i32 " << static_cast<int32_t>(tag) << "\n";
  PushStack(st, 1);
  (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicPrintAny << "\n";
  PopStack(st, 2);
  return true;
}

bool EmitPrintNewline(EmitState& st, std::string* error) {
  (void)error;
  std::string newline_name;
  if (!AddStringConst(st, "\n", &newline_name)) return false;
  (*st.out) << "  const.string " << newline_name << "\n";
  PushStack(st, 1);
  (*st.out) << "  const.i32 " << static_cast<int32_t>(Simple::VM::kPrintAnyTagString) << "\n";
  PushStack(st, 1);
  (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicPrintAny << "\n";
  PopStack(st, 2);
  return true;
}

bool IsSupportedType(const TypeRef& type) {
  if (!type.type_args.empty()) return false;
  if (type.pointer_depth > 0) return true;
  if (type.is_proc) return true;
  if (!type.dims.empty()) {
    if (type.name == "void") return false;
    return true;
  }
  if (type.name == "void") return true;
  if (IsNumericType(type.name) || type.name == "bool" || type.name == "char" || type.name == "string") return true;
  return true;
}

bool CloneTypeRef(const TypeRef& src, TypeRef* out) {
  if (!out) return false;
  out->name = src.name;
  out->pointer_depth = src.pointer_depth;
  out->type_args.clear();
  out->type_args.reserve(src.type_args.size());
  for (const auto& arg : src.type_args) {
    TypeRef cloned;
    if (!CloneTypeRef(arg, &cloned)) return false;
    out->type_args.push_back(std::move(cloned));
  }
  out->dims = src.dims;
  out->is_proc = src.is_proc;
  out->proc_is_callback = src.proc_is_callback;
  out->proc_return_mutability = src.proc_return_mutability;
  out->proc_params.clear();
  out->proc_params.reserve(src.proc_params.size());
  for (const auto& param : src.proc_params) {
    TypeRef cloned;
    if (!CloneTypeRef(param, &cloned)) return false;
    out->proc_params.push_back(std::move(cloned));
  }
  if (src.proc_return) {
    TypeRef cloned;
    if (!CloneTypeRef(*src.proc_return, &cloned)) return false;
    out->proc_return = std::make_unique<TypeRef>(std::move(cloned));
  } else {
    out->proc_return.reset();
  }
  return true;
}

std::string EscapeStringLiteral(const std::string& value, std::string* error) {
  (void)error;
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          static const char kHex[] = "0123456789ABCDEF";
          unsigned char byte = static_cast<unsigned char>(ch);
          out += "\\x";
          out.push_back(kHex[(byte >> 4) & 0xF]);
          out.push_back(kHex[byte & 0xF]);
          break;
        }
        out.push_back(ch);
        break;
    }
  }
  return out;
}

bool ParseIntegerLiteralText(const std::string& text, int64_t* out) {
  if (!out) return false;
  try {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
      *out = static_cast<int64_t>(std::stoull(text.substr(2), nullptr, 16));
      return true;
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
      uint64_t value = 0;
      for (size_t i = 2; i < text.size(); ++i) {
        char c = text[i];
        if (c != '0' && c != '1') return false;
        value = (value << 1) | static_cast<uint64_t>(c - '0');
      }
      *out = static_cast<int64_t>(value);
      return true;
    }
    *out = static_cast<int64_t>(std::stoll(text, nullptr, 10));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::string NewLabel(EmitState& st, const std::string& prefix) {
  return prefix + std::to_string(st.label_counter++);
}

const char* NormalizeNumericOpType(const std::string& name) {
  if (name == "i8" || name == "i16" || name == "i32" || name == "char") return "i32";
  if (name == "u8" || name == "u16" || name == "u32") return "u32";
  if (name == "i64") return "i64";
  if (name == "u64") return "u64";
  if (name == "f32") return "f32";
  if (name == "f64") return "f64";
  return nullptr;
}

const char* NormalizeBitwiseOpType(const std::string& name) {
  if (name == "i8" || name == "i16" || name == "i32" || name == "char") return "i32";
  if (name == "u8" || name == "u16" || name == "u32") return "i32";
  if (name == "i64" || name == "u64") return "i64";
  return nullptr;
}

const char* IncOpForType(const std::string& name) {
  if (name == "i8") return "inc.i8";
  if (name == "i16") return "inc.i16";
  if (name == "i32" || name == "char" || name == "bool") return "inc.i32";
  if (name == "i64") return "inc.i64";
  if (name == "u8") return "inc.u8";
  if (name == "u16") return "inc.u16";
  if (name == "u32") return "inc.u32";
  if (name == "u64") return "inc.u64";
  if (name == "f32") return "inc.f32";
  if (name == "f64") return "inc.f64";
  return nullptr;
}

const char* DecOpForType(const std::string& name) {
  if (name == "i8") return "dec.i8";
  if (name == "i16") return "dec.i16";
  if (name == "i32" || name == "char" || name == "bool") return "dec.i32";
  if (name == "i64") return "dec.i64";
  if (name == "u8") return "dec.u8";
  if (name == "u16") return "dec.u16";
  if (name == "u32") return "dec.u32";
  if (name == "u64") return "dec.u64";
  if (name == "f32") return "dec.f32";
  if (name == "f64") return "dec.f64";
  return nullptr;
}

const char* VmOpSuffixForType(const TypeRef& type) {
  if (type.is_proc) return "ref";
  if (!type.dims.empty()) return "ref";
  if (type.name == "string") return "ref";
  if (type.name == "bool" || type.name == "char" || type.name == "i8" || type.name == "i16" || type.name == "i32" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32") {
    return "i32";
  }
  if (type.name == "i64" || type.name == "u64") return "i64";
  if (type.name == "f32") return "f32";
  if (type.name == "f64") return "f64";
  return nullptr;
}

const char* VmTypeNameForElement(const TypeRef& type) {
  const char* suffix = VmOpSuffixForType(type);
  if (!suffix) return nullptr;
  if (std::string(suffix) == "i32") return "i32";
  if (std::string(suffix) == "i64") return "i64";
  if (std::string(suffix) == "f32") return "f32";
  if (std::string(suffix) == "f64") return "f64";
  return "ref";
}

bool CloneElementType(const TypeRef& container, TypeRef* out) {
  if (!out) return false;
  if (container.dims.empty()) return false;
  if (!CloneTypeRef(container, out)) return false;
  out->dims.erase(out->dims.begin());
  return true;
}

uint32_t FieldSizeForType(const TypeRef& type) {
  if (type.is_proc) return 4;
  if (!type.dims.empty()) return 4;
  if (type.name == "string") return 4;
  if (type.name == "bool" || type.name == "char" || type.name == "i8" || type.name == "i16" ||
      type.name == "i32" || type.name == "u8" || type.name == "u16" || type.name == "u32") {
    return 4;
  }
  if (type.name == "i64" || type.name == "u64" || type.name == "f64") return 8;
  if (type.name == "f32") return 4;
  return 4;
}

uint32_t FieldAlignForType(const TypeRef& type) {
  uint32_t size = FieldSizeForType(type);
  if (size == 0) return 1;
  if (size > 8) return 8;
  return size;
}

uint32_t AlignTo(uint32_t value, uint32_t align) {
  if (align <= 1) return value;
  uint32_t mask = align - 1;
  return (value + mask) & ~mask;
}

std::string FieldSirTypeName(const TypeRef& type, const EmitState& st) {
  if (type.pointer_depth > 0) return "i64";
  if (type.is_proc) return "ref";
  if (!type.dims.empty()) return "ref";
  if (type.name == "string") return "string";
  if (IsNumericType(type.name) || type.name == "bool" || type.name == "char") return type.name;
  if (st.artifacts.find(type.name) != st.artifacts.end()) return "ref";
  if (st.enum_values.find(type.name) != st.enum_values.end()) return "i32";
  return "ref";
}

std::string SigTypeNameFromType(const TypeRef& type, const EmitState& st, std::string* error) {
  if (type.pointer_depth > 0) return "i64";
  if (type.is_proc) return "ref";
  if (!type.dims.empty()) return "ref";
  if (type.name == "void") return "void";
  if (type.name == "string") return "string";
  if (IsNumericType(type.name) || type.name == "bool" || type.name == "char") return type.name;
  if (st.artifacts.find(type.name) != st.artifacts.end()) return type.name;
  if (st.enum_values.find(type.name) != st.enum_values.end()) return "i32";
  if (error) *error = "unsupported type in signature: " + type.name;
  return {};
}

std::string GetProcSigName(EmitState& st,
                           const TypeRef& proc_type,
                           std::string* error) {
  std::string local_error;
  std::string* err = error ? error : &local_error;
  std::ostringstream key;
  std::string ret = "void";
  if (proc_type.proc_return) {
    ret = SigTypeNameFromType(*proc_type.proc_return, st, err);
    if (!err->empty()) return {};
  }
  key << ret << "|";
  for (size_t i = 0; i < proc_type.proc_params.size(); ++i) {
    if (i > 0) key << ",";
    std::string param = SigTypeNameFromType(proc_type.proc_params[i], st, err);
    if (!err->empty()) return {};
    key << param;
  }
  std::string key_str = key.str();
  auto it = st.proc_sig_names.find(key_str);
  if (it != st.proc_sig_names.end()) return it->second;

  std::string name = "sig_proc_" + std::to_string(st.proc_sig_names.size());
  std::ostringstream line;
  line << "  sig " << name << ": (";
  for (size_t i = 0; i < proc_type.proc_params.size(); ++i) {
    if (i > 0) line << ", ";
    std::string param = SigTypeNameFromType(proc_type.proc_params[i], st, err);
    if (!err->empty()) return {};
    line << param;
  }
  line << ") -> " << ret;
  st.proc_sig_names.emplace(std::move(key_str), name);
  st.proc_sig_lines.push_back(line.str());
  return name;
}

bool PushStack(EmitState& st, uint32_t count) {
  st.stack_cur += count;
  if (st.stack_cur > st.stack_max) st.stack_max = st.stack_cur;
  return true;
}

bool PopStack(EmitState& st, uint32_t count) {
  if (st.stack_cur < count) st.stack_cur = 0;
  else st.stack_cur -= count;
  return true;
}

bool EmitDup(EmitState& st) {
  (*st.out) << "  dup\n";
  return PushStack(st, 1);
}

bool EmitDup2(EmitState& st) {
  (*st.out) << "  dup2\n";
  return PushStack(st, 2);
}

bool AddStringConst(EmitState& st, const std::string& value, std::string* out_name) {
  auto it = st.string_consts.find(value);
  if (it != st.string_consts.end()) {
    *out_name = it->second;
    return true;
  }
  std::string error;
  std::string escaped = EscapeStringLiteral(value, &error);
  if (!error.empty()) {
    if (st.error) *st.error = error;
    return false;
  }
  std::string name = "str" + std::to_string(st.string_index++);
  st.string_consts.emplace(value, name);
  st.const_lines.push_back("  const " + name + " string \"" + escaped + "\"");
  *out_name = name;
  return true;
}

bool AddGlobalInitConst(EmitState& st, const std::string& global_name, const TypeRef& type, std::string* out_name) {
  if (!out_name) return false;
  auto make_name = [&]() {
    return "__ginit_" + global_name;
  };
  if (type.name == "f32") {
    std::string name = make_name();
    st.const_lines.push_back("  const " + name + " f32 0.0");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "f64") {
    std::string name = make_name();
    st.const_lines.push_back("  const " + name + " f64 0.0");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "string") {
    std::string name = make_name();
    st.const_lines.push_back("  const " + name + " string \"\"");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "i8" || type.name == "i16" || type.name == "i32" || type.name == "i64" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64" ||
      type.name == "bool" || type.name == "char") {
    std::string name = make_name();
    // IR global init constants currently support string/f32/f64 const-id lookup.
    st.const_lines.push_back("  const " + name + " f64 0.0");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "void") return false;
  // Keep non-scalar globals verifier-initialized; __global_init performs real init when present.
  std::string name = make_name();
  st.const_lines.push_back("  const " + name + " f64 0.0");
  *out_name = std::move(name);
  return true;
}

bool InferExprType(const Expr& expr,
                   const EmitState& st,
                   TypeRef* out,
                   std::string* error);
