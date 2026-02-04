#include "lang_sir.h"

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "lang_parser.h"
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
};

bool IsIntegralType(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "i128" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "u128";
}

bool IsFloatType(const std::string& name) {
  return name == "f32" || name == "f64";
}

bool IsNumericType(const std::string& name) {
  return IsIntegralType(name) || IsFloatType(name);
}

bool IsIoPrintName(const std::string& name) {
  return name == "print" || name == "println";
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

bool IsSupportedType(const TypeRef& type) {
  if (!type.type_args.empty()) return false;
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
  out->type_args.clear();
  out->type_args.reserve(src.type_args.size());
  for (const auto& arg : src.type_args) {
    TypeRef cloned;
    if (!CloneTypeRef(arg, &cloned)) return false;
    out->type_args.push_back(std::move(cloned));
  }
  out->dims = src.dims;
  out->is_proc = src.is_proc;
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
  if (type.is_proc) return "ref";
  if (!type.dims.empty()) return "ref";
  if (type.name == "string") return "string";
  if (IsNumericType(type.name) || type.name == "bool" || type.name == "char") return type.name;
  if (st.artifacts.find(type.name) != st.artifacts.end()) return "ref";
  if (st.enum_values.find(type.name) != st.enum_values.end()) return "i32";
  return "ref";
}

std::string SigTypeNameFromType(const TypeRef& type, const EmitState& st, std::string* error) {
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

bool InferExprType(const Expr& expr,
                   const EmitState& st,
                   TypeRef* out,
                   std::string* error);
bool EmitDefaultInit(EmitState& st, const TypeRef& type, std::string* error);

bool InferLiteralType(const Expr& expr, TypeRef* out) {
  switch (expr.literal_kind) {
    case LiteralKind::Integer:
      out->name = "i32";
      return true;
    case LiteralKind::Float:
      out->name = "f64";
      return true;
    case LiteralKind::String:
      out->name = "string";
      return true;
    case LiteralKind::Char:
      out->name = "char";
      return true;
    case LiteralKind::Bool:
      out->name = "bool";
      return true;
  }
  return false;
}

bool InferExprType(const Expr& expr,
                   const EmitState& st,
                   TypeRef* out,
                   std::string* error) {
  if (!out) return false;
  switch (expr.kind) {
    case ExprKind::Identifier: {
      auto it = st.local_types.find(expr.text);
      if (it == st.local_types.end()) {
        if (error) *error = "unknown local '" + expr.text + "'";
        return false;
      }
      return CloneTypeRef(it->second, out);
    }
    case ExprKind::Literal:
      return InferLiteralType(expr, out);
    case ExprKind::Unary: {
      if (expr.children.empty()) {
        if (error) *error = "unary missing operand";
        return false;
      }
      return InferExprType(expr.children[0], st, out, error);
    }
    case ExprKind::Binary: {
      if (expr.children.size() < 2) {
        if (error) *error = "binary missing operands";
        return false;
      }
      TypeRef left;
      TypeRef right;
      if (!InferExprType(expr.children[0], st, &left, error)) return false;
      if (!InferExprType(expr.children[1], st, &right, error)) return false;
      if (left.name == right.name) {
        return CloneTypeRef(left, out);
      }
      if (left.name == "i32" && right.name == "i32") {
        return CloneTypeRef(left, out);
      }
      if (error) *error = "operand type mismatch for '" + expr.op + "'";
      return false;
    }
    case ExprKind::Index: {
      if (expr.children.size() < 2) {
        if (error) *error = "index expression missing operands";
        return false;
      }
      TypeRef container;
      if (!InferExprType(expr.children[0], st, &container, error)) return false;
      if (container.dims.empty()) {
        if (error) *error = "indexing is only valid on arrays and lists";
        return false;
      }
      if (!CloneElementType(container, out)) {
        if (error) *error = "failed to determine index element type";
        return false;
      }
      return true;
    }
    case ExprKind::ArtifactLiteral:
      if (error) *error = "artifact literal requires expected type";
      return false;
    case ExprKind::Member: {
      if (expr.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier) {
        auto enum_it = st.enum_values.find(base.text);
        if (enum_it != st.enum_values.end()) {
          out->name = base.text;
          return true;
        }
      }
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      const auto& layout = layout_it->second;
      auto field_it = layout.field_index.find(expr.text);
      if (field_it == layout.field_index.end()) {
        if (error) *error = "unknown field '" + expr.text + "'";
        return false;
      }
      return CloneTypeRef(layout.fields[field_it->second].type, out);
    }
    case ExprKind::Call: {
      if (expr.children.empty()) {
        if (error) *error = "call missing callee";
        return false;
      }
      const Expr& callee = expr.children[0];
      if (callee.kind == ExprKind::Identifier) {
        if (callee.text == "len") {
          out->name = "i32";
          return true;
        }
        if (callee.text == "str") {
          out->name = "string";
          return true;
        }
        if (callee.text == "i32") {
          out->name = "i32";
          return true;
        }
        if (callee.text == "f64") {
          out->name = "f64";
          return true;
        }
        auto it = st.func_returns.find(callee.text);
        if (it != st.func_returns.end()) {
          return CloneTypeRef(it->second, out);
        }
      }
      if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
        const Expr& base = callee.children[0];
        if (base.kind == ExprKind::Identifier && base.text == "IO" && IsIoPrintName(callee.text)) {
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          const std::string key = base.text + "." + callee.text;
          auto module_it = st.module_func_names.find(key);
          if (module_it != st.module_func_names.end()) {
            auto ret_it = st.func_returns.find(module_it->second);
            if (ret_it != st.func_returns.end()) {
              return CloneTypeRef(ret_it->second, out);
            }
          }
        }
        TypeRef base_type;
        if (InferExprType(base, st, &base_type, nullptr)) {
          const std::string key = base_type.name + "." + callee.text;
          auto method_it = st.artifact_method_names.find(key);
          if (method_it != st.artifact_method_names.end()) {
            auto ret_it = st.func_returns.find(method_it->second);
            if (ret_it != st.func_returns.end()) {
              return CloneTypeRef(ret_it->second, out);
            }
          }
        }
      }
      if (error) *error = "call type not supported in SIR emission";
      return false;
    }
    default:
      if (error) *error = "expression not supported for SIR emission";
      return false;
  }
}

bool EmitConstForType(EmitState& st,
                      const TypeRef& type,
                      const Expr& expr,
                      std::string* error) {
  if (expr.literal_kind == LiteralKind::String) {
    std::string name;
    if (!AddStringConst(st, expr.text, &name)) return false;
    (*st.out) << "  const.string " << name << "\n";
    return PushStack(st, 1);
  }
  if (expr.literal_kind == LiteralKind::Char) {
    uint16_t value = static_cast<unsigned char>(expr.text.empty() ? '\0' : expr.text[0]);
    (*st.out) << "  const.char " << value << "\n";
    return PushStack(st, 1);
  }
  if (expr.literal_kind == LiteralKind::Bool) {
    const std::string text = expr.text;
    uint32_t value = (text == "true") ? 1u : 0u;
    (*st.out) << "  const.bool " << value << "\n";
    return PushStack(st, 1);
  }

  if (!IsNumericType(type.name)) {
    if (error) *error = "literal type not supported for SIR emission";
    return false;
  }

  if (expr.literal_kind == LiteralKind::Float) {
    (*st.out) << "  const." << type.name << " " << expr.text << "\n";
    return PushStack(st, 1);
  }

  (*st.out) << "  const." << type.name << " " << expr.text << "\n";
  return PushStack(st, 1);
}

bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
              std::string* error);

bool EmitStmt(EmitState& st, const Stmt& stmt, std::string* error);

bool EmitIndexSetOp(EmitState& st,
                    const TypeRef& container_type,
                    const char* op_suffix) {
  if (container_type.dims.front().is_list) {
    (*st.out) << "  list.set." << op_suffix << "\n";
  } else {
    (*st.out) << "  array.set." << op_suffix << "\n";
  }
  PopStack(st, 3);
  return true;
}

bool EmitIndexGetOp(EmitState& st,
                    const TypeRef& container_type,
                    const char* op_suffix) {
  if (container_type.dims.front().is_list) {
    (*st.out) << "  list.get." << op_suffix << "\n";
  } else {
    (*st.out) << "  array.get." << op_suffix << "\n";
  }
  PopStack(st, 2);
  return PushStack(st, 1);
}

const char* AssignOpToBinaryOp(const std::string& op) {
  if (op == "+=") return "+";
  if (op == "-=") return "-";
  if (op == "*=") return "*";
  if (op == "/=") return "/";
  if (op == "%=") return "%";
  if (op == "&=") return "&";
  if (op == "|=") return "|";
  if (op == "^=") return "^";
  if (op == "<<=") return "<<";
  if (op == ">>=") return ">>";
  return nullptr;
}

bool EmitLocalAssignment(EmitState& st,
                         const std::string& name,
                         const TypeRef& type,
                         const Expr& value,
                         const std::string& op,
                         bool return_value,
                         std::string* error) {
  auto it = st.local_indices.find(name);
  if (it == st.local_indices.end()) {
    if (error) *error = "unknown local '" + name + "'";
    return false;
  }
  if (op == "=") {
    if (!EmitExpr(st, value, &type, error)) return false;
    (*st.out) << "  stloc " << it->second << "\n";
    PopStack(st, 1);
    if (return_value) {
      (*st.out) << "  ldloc " << it->second << "\n";
      PushStack(st, 1);
    }
    return true;
  }

  const char* bin_op = AssignOpToBinaryOp(op);
  if (!bin_op) {
    if (error) *error = "unsupported assignment operator '" + op + "'";
    return false;
  }
  (*st.out) << "  ldloc " << it->second << "\n";
  PushStack(st, 1);
  if (!EmitExpr(st, value, &type, error)) return false;
  PopStack(st, 1);
  const char* op_type = nullptr;
  if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
      std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
    op_type = NormalizeBitwiseOpType(type.name);
  } else {
    op_type = NormalizeNumericOpType(type.name);
  }
  if (!op_type) {
    if (error) *error = "unsupported operand type for '" + op + "'";
    return false;
  }
  if (std::string(bin_op) == "+") {
    (*st.out) << "  add." << op_type << "\n";
  } else if (std::string(bin_op) == "-") {
    (*st.out) << "  sub." << op_type << "\n";
  } else if (std::string(bin_op) == "*") {
    (*st.out) << "  mul." << op_type << "\n";
  } else if (std::string(bin_op) == "/") {
    (*st.out) << "  div." << op_type << "\n";
  } else if (std::string(bin_op) == "%" && IsIntegralType(type.name)) {
    (*st.out) << "  mod." << op_type << "\n";
  } else if (std::string(bin_op) == "&") {
    (*st.out) << "  and." << op_type << "\n";
  } else if (std::string(bin_op) == "|") {
    (*st.out) << "  or." << op_type << "\n";
  } else if (std::string(bin_op) == "^") {
    (*st.out) << "  xor." << op_type << "\n";
  } else if (std::string(bin_op) == "<<") {
    (*st.out) << "  shl." << op_type << "\n";
  } else if (std::string(bin_op) == ">>") {
    (*st.out) << "  shr." << op_type << "\n";
  } else {
    if (error) *error = "unsupported assignment operator '" + op + "'";
    return false;
  }
  (*st.out) << "  stloc " << it->second << "\n";
  PopStack(st, 1);
  if (return_value) {
    (*st.out) << "  ldloc " << it->second << "\n";
    PushStack(st, 1);
  }
  return true;
}

bool EmitAssignmentExpr(EmitState& st, const Expr& expr, std::string* error) {
  if (expr.children.size() != 2) {
    if (error) *error = "assignment missing operands";
    return false;
  }
  const Expr& target = expr.children[0];
  if (target.kind == ExprKind::Identifier) {
    auto type_it = st.local_types.find(target.text);
    if (type_it == st.local_types.end()) {
      if (error) *error = "unknown type for local '" + target.text + "'";
      return false;
    }
    return EmitLocalAssignment(st, target.text, type_it->second, expr.children[1], expr.op, true, error);
  }
  if (target.kind == ExprKind::Index) {
    if (target.children.size() != 2) {
      if (error) *error = "index assignment expects target and index";
      return false;
    }
    TypeRef container_type;
    if (!InferExprType(target.children[0], st, &container_type, error)) return false;
    if (container_type.dims.empty()) {
      if (error) *error = "index assignment expects array or list target";
      return false;
    }
    TypeRef element_type;
    if (!CloneElementType(container_type, &element_type)) {
      if (error) *error = "failed to resolve index element type";
      return false;
    }
    const char* op_suffix = VmOpSuffixForType(element_type);
    if (!op_suffix) {
      if (error) *error = "unsupported index assignment element type for SIR emission";
      return false;
    }
    if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
    TypeRef index_type;
    index_type.name = "i32";
    if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
    if (expr.op != "=") {
      if (!EmitDup2(st)) return false;
      if (!EmitIndexGetOp(st, container_type, op_suffix)) return false;
      if (!EmitExpr(st, expr.children[1], &element_type, error)) return false;
      PopStack(st, 1);
      const char* bin_op = AssignOpToBinaryOp(expr.op);
      if (!bin_op) {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      const char* op_type = nullptr;
      if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
          std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
        op_type = NormalizeBitwiseOpType(element_type.name);
      } else {
        op_type = NormalizeNumericOpType(element_type.name);
      }
      if (!op_type) {
        if (error) *error = "unsupported operand type for '" + expr.op + "'";
        return false;
      }
      if (std::string(bin_op) == "+") {
        (*st.out) << "  add." << op_type << "\n";
      } else if (std::string(bin_op) == "-") {
        (*st.out) << "  sub." << op_type << "\n";
      } else if (std::string(bin_op) == "*") {
        (*st.out) << "  mul." << op_type << "\n";
      } else if (std::string(bin_op) == "/") {
        (*st.out) << "  div." << op_type << "\n";
      } else if (std::string(bin_op) == "%" && IsIntegralType(element_type.name)) {
        (*st.out) << "  mod." << op_type << "\n";
      } else if (std::string(bin_op) == "&") {
        (*st.out) << "  and." << op_type << "\n";
      } else if (std::string(bin_op) == "|") {
        (*st.out) << "  or." << op_type << "\n";
      } else if (std::string(bin_op) == "^") {
        (*st.out) << "  xor." << op_type << "\n";
      } else if (std::string(bin_op) == "<<") {
        (*st.out) << "  shl." << op_type << "\n";
      } else if (std::string(bin_op) == ">>") {
        (*st.out) << "  shr." << op_type << "\n";
      } else {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      if (!EmitDup(st)) return false;
      if (!EmitIndexSetOp(st, container_type, op_suffix)) return false;
      return true;
    }
    if (!EmitExpr(st, expr.children[1], &element_type, error)) return false;
    if (!EmitDup(st)) return false;
    if (!EmitIndexSetOp(st, container_type, op_suffix)) return false;
    return true;
  }
  if (target.kind == ExprKind::Member) {
    if (target.children.empty()) {
      if (error) *error = "member assignment missing base";
      return false;
    }
    const Expr& base = target.children[0];
    TypeRef base_type;
    if (!InferExprType(base, st, &base_type, error)) return false;
    auto layout_it = st.artifact_layouts.find(base_type.name);
    if (layout_it == st.artifact_layouts.end()) {
      if (error) *error = "member assignment base is not an artifact";
      return false;
    }
    auto field_it = layout_it->second.field_index.find(target.text);
    if (field_it == layout_it->second.field_index.end()) {
      if (error) *error = "unknown field '" + target.text + "'";
      return false;
    }
    const TypeRef& field_type = layout_it->second.fields[field_it->second].type;
    if (!EmitExpr(st, base, &base_type, error)) return false;
    if (expr.op != "=") {
      if (!EmitDup(st)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << target.text << "\n";
      if (!EmitExpr(st, expr.children[1], &field_type, error)) return false;
      PopStack(st, 1);
      const char* bin_op = AssignOpToBinaryOp(expr.op);
      if (!bin_op) {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      const char* op_type = nullptr;
      if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
          std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
        op_type = NormalizeBitwiseOpType(field_type.name);
      } else {
        op_type = NormalizeNumericOpType(field_type.name);
      }
      if (!op_type) {
        if (error) *error = "unsupported operand type for '" + expr.op + "'";
        return false;
      }
      if (std::string(bin_op) == "+") {
        (*st.out) << "  add." << op_type << "\n";
      } else if (std::string(bin_op) == "-") {
        (*st.out) << "  sub." << op_type << "\n";
      } else if (std::string(bin_op) == "*") {
        (*st.out) << "  mul." << op_type << "\n";
      } else if (std::string(bin_op) == "/") {
        (*st.out) << "  div." << op_type << "\n";
      } else if (std::string(bin_op) == "%" && IsIntegralType(field_type.name)) {
        (*st.out) << "  mod." << op_type << "\n";
      } else if (std::string(bin_op) == "&") {
        (*st.out) << "  and." << op_type << "\n";
      } else if (std::string(bin_op) == "|") {
        (*st.out) << "  or." << op_type << "\n";
      } else if (std::string(bin_op) == "^") {
        (*st.out) << "  xor." << op_type << "\n";
      } else if (std::string(bin_op) == "<<") {
        (*st.out) << "  shl." << op_type << "\n";
      } else if (std::string(bin_op) == ">>") {
        (*st.out) << "  shr." << op_type << "\n";
      } else {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      if (!EmitDup(st)) return false;
      (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
      PopStack(st, 2);
      return true;
    }
    if (!EmitExpr(st, expr.children[1], &field_type, error)) return false;
    if (!EmitDup(st)) return false;
    (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
    PopStack(st, 2);
    return true;
  }
  if (error) *error = "assignment target not supported in SIR emission";
  return false;
}

bool EmitUnary(EmitState& st,
               const Expr& expr,
               const TypeRef* expected,
               std::string* error) {
  if (expr.children.empty()) {
    if (error) *error = "unary missing operand";
    return false;
  }
  TypeRef operand_type;
  if (!InferExprType(expr.children[0], st, &operand_type, error)) return false;
  const TypeRef* use_type = expected ? expected : &operand_type;
  if (expr.op == "++" || expr.op == "--") {
    const char* op_name = expr.op == "++" ? IncOpForType(use_type->name) : DecOpForType(use_type->name);
    if (!op_name) {
      if (error) *error = "unsupported inc/dec type '" + use_type->name + "'";
      return false;
    }
    if (expr.children[0].kind == ExprKind::Identifier) {
      auto it = st.local_indices.find(expr.children[0].text);
      if (it == st.local_indices.end()) {
        if (error) *error = "unknown local '" + expr.children[0].text + "'";
        return false;
      }
      (*st.out) << "  ldloc " << it->second << "\n";
      PushStack(st, 1);
      (*st.out) << "  " << op_name << "\n";
      (*st.out) << "  dup\n";
      PushStack(st, 1);
      (*st.out) << "  stloc " << it->second << "\n";
      PopStack(st, 1);
      return true;
    }
    if (expr.children[0].kind == ExprKind::Index) {
      const Expr& target = expr.children[0];
      if (target.children.size() != 2) {
        if (error) *error = "index expression expects target and index";
        return false;
      }
      TypeRef container_type;
      if (!InferExprType(target.children[0], st, &container_type, error)) return false;
      if (container_type.dims.empty()) {
        if (error) *error = "indexing is only valid on arrays and lists";
        return false;
      }
      TypeRef element_type;
      if (!CloneElementType(container_type, &element_type)) {
        if (error) *error = "failed to resolve index element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type);
      if (!op_suffix) {
        if (error) *error = "unsupported index element type for SIR emission";
        return false;
      }
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      TypeRef index_type;
      index_type.name = "i32";
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      if (!EmitIndexGetOp(st, container_type, op_suffix)) return false;
      (*st.out) << "  " << op_name << "\n";
      if (!EmitDup(st)) return false;
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      (*st.out) << "  rot\n";
      return EmitIndexSetOp(st, container_type, op_suffix);
    }
    if (expr.children[0].kind == ExprKind::Member) {
      const Expr& target = expr.children[0];
      if (target.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = target.children[0];
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << target.text << "\n";
      (*st.out) << "  " << op_name << "\n";
      if (!EmitDup(st)) return false;
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  swap\n";
      (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
      PopStack(st, 2);
      return true;
    }
    if (error) *error = "inc/dec target not supported in SIR emission";
    return false;
  }
  if (expr.op == "post++" || expr.op == "post--") {
    const char* op_name = expr.op == "post++" ? IncOpForType(use_type->name) : DecOpForType(use_type->name);
    if (!op_name) {
      if (error) *error = "unsupported inc/dec type '" + use_type->name + "'";
      return false;
    }
    if (expr.children[0].kind == ExprKind::Identifier) {
      auto it = st.local_indices.find(expr.children[0].text);
      if (it == st.local_indices.end()) {
        if (error) *error = "unknown local '" + expr.children[0].text + "'";
        return false;
      }
      (*st.out) << "  ldloc " << it->second << "\n";
      PushStack(st, 1);
      (*st.out) << "  dup\n";
      PushStack(st, 1);
      (*st.out) << "  " << op_name << "\n";
      (*st.out) << "  stloc " << it->second << "\n";
      PopStack(st, 1);
      return true;
    }
    if (expr.children[0].kind == ExprKind::Index) {
      const Expr& target = expr.children[0];
      if (target.children.size() != 2) {
        if (error) *error = "index expression expects target and index";
        return false;
      }
      TypeRef container_type;
      if (!InferExprType(target.children[0], st, &container_type, error)) return false;
      if (container_type.dims.empty()) {
        if (error) *error = "indexing is only valid on arrays and lists";
        return false;
      }
      TypeRef element_type;
      if (!CloneElementType(container_type, &element_type)) {
        if (error) *error = "failed to resolve index element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type);
      if (!op_suffix) {
        if (error) *error = "unsupported index element type for SIR emission";
        return false;
      }
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      TypeRef index_type;
      index_type.name = "i32";
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      if (!EmitIndexGetOp(st, container_type, op_suffix)) return false;
      if (!EmitDup(st)) return false;
      (*st.out) << "  " << op_name << "\n";
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      (*st.out) << "  rot\n";
      return EmitIndexSetOp(st, container_type, op_suffix);
    }
    if (expr.children[0].kind == ExprKind::Member) {
      const Expr& target = expr.children[0];
      if (target.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = target.children[0];
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << target.text << "\n";
      if (!EmitDup(st)) return false;
      (*st.out) << "  " << op_name << "\n";
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  swap\n";
      (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
      PopStack(st, 2);
      return true;
    }
    if (error) *error = "inc/dec target not supported in SIR emission";
    return false;
  }
  if (!EmitExpr(st, expr.children[0], use_type, error)) return false;
  if (expr.op == "-" && IsNumericType(use_type->name)) {
    (*st.out) << "  neg." << use_type->name << "\n";
    return true;
  }
  if (expr.op == "!" && use_type->name == "bool") {
    (*st.out) << "  bool.not\n";
    return true;
  }
  if (error) *error = "unsupported unary operator '" + expr.op + "'";
  return false;
}

bool EmitBinary(EmitState& st,
                const Expr& expr,
                const TypeRef* expected,
                std::string* error) {
  if (expr.children.size() < 2) {
    if (error) *error = "binary missing operands";
    return false;
  }
  TypeRef left_type;
  if (!InferExprType(expr.children[0], st, &left_type, error)) return false;
  TypeRef right_type;
  if (!InferExprType(expr.children[1], st, &right_type, error)) return false;
  if (left_type.name != right_type.name && !expected) {
    if (error) *error = "operand type mismatch for '" + expr.op + "'";
    return false;
  }

  if (expr.op == "=" || AssignOpToBinaryOp(expr.op)) {
    if (expected) {
      if (error) *error = "assignment expression not supported in typed context";
      return false;
    }
    return EmitAssignmentExpr(st, expr, error);
  }

  if (expr.op == "&&" || expr.op == "||") {
    TypeRef bool_type;
    bool_type.name = "bool";
    if (!EmitExpr(st, expr.children[0], &bool_type, error)) return false;
    std::string short_label = NewLabel(st, expr.op == "&&" ? "and_false_" : "or_true_");
    std::string end_label = NewLabel(st, "bool_end_");
    if (expr.op == "&&") {
      (*st.out) << "  jmp.false " << short_label << "\n";
      PopStack(st, 1);
      if (!EmitExpr(st, expr.children[1], &bool_type, error)) return false;
      (*st.out) << "  jmp.false " << short_label << "\n";
      PopStack(st, 1);
      (*st.out) << "  const.bool 1\n";
      PushStack(st, 1);
      (*st.out) << "  jmp " << end_label << "\n";
      (*st.out) << short_label << ":\n";
      (*st.out) << "  const.bool 0\n";
      PushStack(st, 1);
      (*st.out) << end_label << ":\n";
      return true;
    }
    (*st.out) << "  jmp.true " << short_label << "\n";
    PopStack(st, 1);
    if (!EmitExpr(st, expr.children[1], &bool_type, error)) return false;
    (*st.out) << "  jmp.true " << short_label << "\n";
    PopStack(st, 1);
    (*st.out) << "  const.bool 0\n";
    PushStack(st, 1);
    (*st.out) << "  jmp " << end_label << "\n";
    (*st.out) << short_label << ":\n";
    (*st.out) << "  const.bool 1\n";
    PushStack(st, 1);
    (*st.out) << end_label << ":\n";
    return true;
  }

  TypeRef type;
  if (!CloneTypeRef(left_type, &type)) {
    if (error) *error = "failed to clone type";
    return false;
  }
  if (expected) {
    if (!CloneTypeRef(*expected, &type)) {
      if (error) *error = "failed to clone expected type";
      return false;
    }
  }

  if (!EmitExpr(st, expr.children[0], &type, error)) return false;
  if (!EmitExpr(st, expr.children[1], &type, error)) return false;
  PopStack(st, 1);
  if (expr.op == "==" || expr.op == "!=" || expr.op == "<" || expr.op == "<=" ||
      expr.op == ">" || expr.op == ">=") {
    const char* op_type = NormalizeNumericOpType(type.name);
    if (!op_type) {
      if (error) *error = "unsupported operand type for '" + expr.op + "'";
      return false;
    }
    if (type.name == "bool") {
      if (error) *error = "bool comparisons not supported in SIR emission";
      return false;
    }
    const char* cmp = nullptr;
    if (expr.op == "==") cmp = "cmp.eq.";
    else if (expr.op == "!=") cmp = "cmp.ne.";
    else if (expr.op == "<") cmp = "cmp.lt.";
    else if (expr.op == "<=") cmp = "cmp.le.";
    else if (expr.op == ">") cmp = "cmp.gt.";
    else if (expr.op == ">=") cmp = "cmp.ge.";
    (*st.out) << "  " << cmp << op_type << "\n";
    return true;
  }
  if (expr.op == "+" || expr.op == "-" || expr.op == "*" || expr.op == "/" || expr.op == "%") {
    const char* op_type = NormalizeNumericOpType(type.name);
    if (!op_type) {
      if (error) *error = "unsupported operand type for '" + expr.op + "'";
      return false;
    }
    if (expr.op == "+" ) {
    (*st.out) << "  add." << op_type << "\n";
    return true;
  }
    if (expr.op == "-") {
      (*st.out) << "  sub." << op_type << "\n";
      return true;
    }
    if (expr.op == "*") {
      (*st.out) << "  mul." << op_type << "\n";
      return true;
    }
    if (expr.op == "/") {
      (*st.out) << "  div." << op_type << "\n";
      return true;
    }
    if (expr.op == "%" && IsIntegralType(type.name)) {
      (*st.out) << "  mod." << op_type << "\n";
      return true;
    }
  }
  if (expr.op == "&" || expr.op == "|" || expr.op == "^" || expr.op == "<<" || expr.op == ">>") {
    const char* op_type = NormalizeBitwiseOpType(type.name);
    if (!op_type) {
      if (error) *error = "unsupported operand type for '" + expr.op + "'";
      return false;
    }
    if (expr.op == "&") {
      (*st.out) << "  and." << op_type << "\n";
    } else if (expr.op == "|") {
      (*st.out) << "  or." << op_type << "\n";
    } else if (expr.op == "^") {
      (*st.out) << "  xor." << op_type << "\n";
    } else if (expr.op == "<<") {
      (*st.out) << "  shl." << op_type << "\n";
    } else if (expr.op == ">>") {
      (*st.out) << "  shr." << op_type << "\n";
    }
    return true;
  }
  if (error) *error = "unsupported binary operator '" + expr.op + "'";
  return false;
}

bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
              std::string* error) {
  switch (expr.kind) {
    case ExprKind::Identifier: {
      auto it = st.local_indices.find(expr.text);
      if (it == st.local_indices.end()) {
        if (error) *error = "unknown local '" + expr.text + "'";
        return false;
      }
      (*st.out) << "  ldloc " << it->second << "\n";
      return PushStack(st, 1);
    }
    case ExprKind::Literal: {
      TypeRef literal_type;
      if (!InferLiteralType(expr, &literal_type)) {
        if (error) *error = "unknown literal type";
        return false;
      }
      const TypeRef* use_type = expected ? expected : &literal_type;
      if (!IsSupportedType(*use_type) || use_type->name == "void") {
        if (error) *error = "literal type not supported in SIR emission";
        return false;
      }
      if ((use_type->name == "i128" || use_type->name == "u128")) {
        if (error) *error = "i128/u128 const not supported in SIR";
        return false;
      }
      return EmitConstForType(st, *use_type, expr, error);
    }
    case ExprKind::Call: {
      if (expr.children.empty()) {
        if (error) *error = "call missing callee";
        return false;
      }
      const Expr& callee = expr.children[0];
      if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
        const Expr& base = callee.children[0];
        if (base.kind == ExprKind::Identifier && base.text == "IO" && IsIoPrintName(callee.text)) {
          if (expr.args.size() != 1) {
            if (error) *error = "call argument count mismatch for 'IO." + callee.text + "'";
            return false;
          }
          TypeRef arg_type;
          if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
          if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
          uint32_t tag = 0;
          if (!GetPrintAnyTagForType(arg_type, &tag, error)) return false;
          (*st.out) << "  const.i32 " << static_cast<int32_t>(tag) << "\n";
          PushStack(st, 1);
          (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicPrintAny << "\n";
          PopStack(st, 2);
          if (callee.text == "println") {
            std::string newline_name;
            if (!AddStringConst(st, "\n", &newline_name)) return false;
            (*st.out) << "  const.string " << newline_name << "\n";
            PushStack(st, 1);
            (*st.out) << "  const.i32 " << static_cast<int32_t>(Simple::VM::kPrintAnyTagString) << "\n";
            PushStack(st, 1);
            (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicPrintAny << "\n";
            PopStack(st, 2);
          }
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          const std::string key = base.text + "." + callee.text;
          auto module_it = st.module_func_names.find(key);
          if (module_it != st.module_func_names.end()) {
            const std::string& hoisted = module_it->second;
            auto params_it = st.func_params.find(hoisted);
            if (params_it == st.func_params.end()) {
              if (error) *error = "missing signature for '" + key + "'";
              return false;
            }
            const auto& params = params_it->second;
            if (expr.args.size() != params.size()) {
              if (error) *error = "call argument count mismatch for '" + key + "'";
              return false;
            }
            for (size_t i = 0; i < params.size(); ++i) {
              if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
            }
            auto id_it = st.func_ids.find(hoisted);
            if (id_it == st.func_ids.end()) {
              if (error) *error = "unknown function '" + key + "'";
              return false;
            }
            (*st.out) << "  call " << id_it->second << " " << params.size() << "\n";
            if (st.stack_cur >= params.size()) {
              st.stack_cur -= static_cast<uint32_t>(params.size());
            } else {
              st.stack_cur = 0;
            }
            auto ret_it = st.func_returns.find(hoisted);
            if (ret_it != st.func_returns.end() && ret_it->second.name != "void") {
              PushStack(st, 1);
            }
            return true;
          }
        }
        TypeRef base_type;
        if (!InferExprType(base, st, &base_type, nullptr)) {
          if (error) *error = "call target not supported in SIR emission";
          return false;
        }
        const std::string key = base_type.name + "." + callee.text;
        auto method_it = st.artifact_method_names.find(key);
        if (method_it != st.artifact_method_names.end()) {
          const std::string& hoisted = method_it->second;
          auto params_it = st.func_params.find(hoisted);
          if (params_it == st.func_params.end()) {
            if (error) *error = "missing signature for '" + key + "'";
            return false;
          }
          const auto& params = params_it->second;
          if (expr.args.size() + 1 != params.size()) {
            if (error) *error = "call argument count mismatch for '" + key + "'";
            return false;
          }
          if (!EmitExpr(st, base, &base_type, error)) return false;
          for (size_t i = 0; i < expr.args.size(); ++i) {
            if (!EmitExpr(st, expr.args[i], &params[i + 1], error)) return false;
          }
          auto id_it = st.func_ids.find(hoisted);
          if (id_it == st.func_ids.end()) {
            if (error) *error = "unknown function '" + key + "'";
            return false;
          }
          (*st.out) << "  call " << id_it->second << " " << params.size() << "\n";
          if (st.stack_cur >= params.size()) {
            st.stack_cur -= static_cast<uint32_t>(params.size());
          } else {
            st.stack_cur = 0;
          }
          auto ret_it = st.func_returns.find(hoisted);
          if (ret_it != st.func_returns.end() && ret_it->second.name != "void") {
            PushStack(st, 1);
          }
          return true;
        }
      }
      if (callee.kind == ExprKind::FnLiteral) {
        if (error) *error = "calling fn literal directly is not supported in SIR emission";
        return false;
      }
      const std::string& name = callee.text;
      if (name == "len") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for 'len'";
          return false;
        }
        TypeRef arg_type;
        if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
        if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
        if (arg_type.name == "string" && arg_type.dims.empty()) {
          (*st.out) << "  string.len\n";
        } else if (!arg_type.dims.empty()) {
          if (arg_type.dims.front().is_list) {
            (*st.out) << "  list.len\n";
          } else {
            (*st.out) << "  array.len\n";
          }
        } else {
          if (error) *error = "len expects array, list, or string argument";
          return false;
        }
        PopStack(st, 1);
        PushStack(st, 1);
        return true;
      }
      if (callee.kind == ExprKind::Identifier) {
        auto local_it = st.local_types.find(name);
        if (local_it != st.local_types.end()) {
          const TypeRef& proc_type = local_it->second;
          if (!proc_type.is_proc) {
            if (error) *error = "call target is not a function: " + name;
            return false;
          }
          if (expr.args.size() != proc_type.proc_params.size()) {
            if (error) *error = "call argument count mismatch for '" + name + "'";
            return false;
          }
          for (size_t i = 0; i < proc_type.proc_params.size(); ++i) {
            if (!EmitExpr(st, expr.args[i], &proc_type.proc_params[i], error)) return false;
          }
          if (!EmitExpr(st, callee, &proc_type, error)) return false;
          std::string sig_name = GetProcSigName(st, proc_type, error);
          if (sig_name.empty()) return false;
          (*st.out) << "  call.indirect " << sig_name << " " << proc_type.proc_params.size() << "\n";
          PopStack(st, static_cast<uint32_t>(proc_type.proc_params.size() + 1));
          if (proc_type.proc_return && proc_type.proc_return->name != "void") {
            PushStack(st, 1);
          }
          return true;
        }
        auto id_it = st.func_ids.find(name);
        if (id_it == st.func_ids.end()) {
          if (error) *error = "unknown function '" + name + "'";
          return false;
        }
        auto params_it = st.func_params.find(name);
        if (params_it == st.func_params.end()) {
          if (error) *error = "missing signature for '" + name + "'";
          return false;
        }
        const auto& params = params_it->second;
        if (expr.args.size() != params.size()) {
          if (error) *error = "call argument count mismatch for '" + name + "'";
          return false;
        }
        for (size_t i = 0; i < params.size(); ++i) {
          if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
        }
        (*st.out) << "  call " << id_it->second << " " << params.size() << "\n";
        if (st.stack_cur >= params.size()) {
          st.stack_cur -= static_cast<uint32_t>(params.size());
        } else {
          st.stack_cur = 0;
        }
        auto ret_it = st.func_returns.find(name);
        if (ret_it != st.func_returns.end() && ret_it->second.name != "void") {
          PushStack(st, 1);
        }
        return true;
      }

      TypeRef callee_type;
      if (!InferExprType(callee, st, &callee_type, error)) return false;
      if (!callee_type.is_proc) {
        if (error) *error = "call target not supported in SIR emission";
        return false;
      }
      if (expr.args.size() != callee_type.proc_params.size()) {
        if (error) *error = "call argument count mismatch for callee";
        return false;
      }
      for (size_t i = 0; i < callee_type.proc_params.size(); ++i) {
        if (!EmitExpr(st, expr.args[i], &callee_type.proc_params[i], error)) return false;
      }
      if (!EmitExpr(st, callee, &callee_type, error)) return false;
      std::string sig_name = GetProcSigName(st, callee_type, error);
      if (sig_name.empty()) return false;
      (*st.out) << "  call.indirect " << sig_name << " " << callee_type.proc_params.size() << "\n";
      PopStack(st, static_cast<uint32_t>(callee_type.proc_params.size() + 1));
      if (callee_type.proc_return && callee_type.proc_return->name != "void") {
        PushStack(st, 1);
      }
      return true;
    }
    case ExprKind::Unary:
      return EmitUnary(st, expr, expected, error);
    case ExprKind::Binary:
      return EmitBinary(st, expr, expected, error);
    case ExprKind::ArrayLiteral:
    case ExprKind::ListLiteral: {
      if (!expected) {
        if (error) *error = "array/list literal requires expected type";
        return false;
      }
      if (expected->dims.empty()) {
        if (error) *error = "array/list literal requires array or list type";
        return false;
      }
      bool is_list = expected->dims.front().is_list;
      TypeRef element_type;
      if (!CloneElementType(*expected, &element_type)) {
        if (error) *error = "failed to resolve array/list element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type);
      const char* type_name = VmTypeNameForElement(element_type);
      if (!op_suffix || !type_name) {
        if (error) *error = "unsupported array/list element type for SIR emission";
        return false;
      }
      uint32_t length = static_cast<uint32_t>(expr.children.size());
      if (is_list) {
        (*st.out) << "  newlist " << type_name << " " << length << "\n";
      } else {
        (*st.out) << "  newarray " << type_name << " " << length << "\n";
      }
      PushStack(st, 1);
      for (uint32_t i = 0; i < length; ++i) {
        (*st.out) << "  dup\n";
        PushStack(st, 1);
        if (!EmitExpr(st, expr.children[i], &element_type, error)) return false;
        if (is_list) {
          (*st.out) << "  list.push." << op_suffix << "\n";
          PopStack(st, 2);
        } else {
          (*st.out) << "  const.i32 " << i << "\n";
          PushStack(st, 1);
          (*st.out) << "  swap\n";
          (*st.out) << "  array.set." << op_suffix << "\n";
          PopStack(st, 3);
        }
      }
      return true;
    }
    case ExprKind::Index: {
      if (expr.children.size() != 2) {
        if (error) *error = "index expression expects target and index";
        return false;
      }
      TypeRef container_type;
      if (!InferExprType(expr.children[0], st, &container_type, error)) return false;
      if (container_type.dims.empty()) {
        if (error) *error = "indexing is only valid on arrays and lists";
        return false;
      }
      TypeRef element_type;
      if (!CloneElementType(container_type, &element_type)) {
        if (error) *error = "failed to resolve index element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type);
      if (!op_suffix) {
        if (error) *error = "unsupported index element type for SIR emission";
        return false;
      }
      if (!EmitExpr(st, expr.children[0], &container_type, error)) return false;
      TypeRef index_type;
      index_type.name = "i32";
      if (!EmitExpr(st, expr.children[1], &index_type, error)) return false;
      if (container_type.dims.front().is_list) {
        (*st.out) << "  list.get." << op_suffix << "\n";
      } else {
        (*st.out) << "  array.get." << op_suffix << "\n";
      }
      PopStack(st, 2);
      PushStack(st, 1);
      return true;
    }
    case ExprKind::ArtifactLiteral: {
      if (!expected) {
        if (error) *error = "artifact literal requires expected type";
        return false;
      }
      auto layout_it = st.artifact_layouts.find(expected->name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "artifact literal expects artifact type";
        return false;
      }
      const auto& layout = layout_it->second;
      std::vector<const Expr*> field_exprs(layout.fields.size(), nullptr);
      if (!expr.children.empty()) {
        if (expr.children.size() > layout.fields.size()) {
          if (error) *error = "artifact literal has too many positional values";
          return false;
        }
        for (size_t i = 0; i < expr.children.size(); ++i) {
          field_exprs[i] = &expr.children[i];
        }
      }
      for (size_t i = 0; i < expr.field_names.size(); ++i) {
        const std::string& field = expr.field_names[i];
        auto field_it = layout.field_index.find(field);
        if (field_it == layout.field_index.end()) {
          if (error) *error = "unknown artifact field '" + field + "'";
          return false;
        }
        size_t index = field_it->second;
        field_exprs[index] = &expr.field_values[i];
      }
      (*st.out) << "  newobj " << expected->name << "\n";
      PushStack(st, 1);
      for (size_t i = 0; i < layout.fields.size(); ++i) {
        const auto& field = layout.fields[i];
        (*st.out) << "  dup\n";
        PushStack(st, 1);
        if (field_exprs[i]) {
          if (!EmitExpr(st, *field_exprs[i], &field.type, error)) return false;
        } else {
          if (!EmitDefaultInit(st, field.type, error)) return false;
        }
        (*st.out) << "  stfld " << expected->name << "." << field.name << "\n";
        PopStack(st, 2);
      }
      return true;
    }
    case ExprKind::FnLiteral: {
      if (!expected || !expected->is_proc) {
        if (error) *error = "fn literal requires a proc-typed context";
        return false;
      }
      if (expr.fn_params.size() != expected->proc_params.size()) {
        if (error) *error = "fn literal parameter count mismatch";
        return false;
      }
      FuncDecl lambda;
      lambda.name = "__lambda" + std::to_string(st.lambda_counter++);
      lambda.return_mutability = expected->proc_return_mutability;
      if (expected->proc_return) {
        if (!CloneTypeRef(*expected->proc_return, &lambda.return_type)) return false;
      } else {
        lambda.return_type.name = "void";
      }
      lambda.params.clear();
      lambda.params.reserve(expr.fn_params.size());
      for (const auto& param : expr.fn_params) {
        ParamDecl cloned_param;
        cloned_param.name = param.name;
        cloned_param.mutability = param.mutability;
        if (!CloneTypeRef(param.type, &cloned_param.type)) return false;
        lambda.params.push_back(std::move(cloned_param));
      }

      std::vector<Token> tokens;
      size_t body_start = 0;
      if (!expr.fn_body_tokens.empty() && expr.fn_body_tokens[0].kind == TokenKind::LParen) {
        body_start = 1;
      }
      tokens.reserve(expr.fn_body_tokens.size() + 3);
      Token brace;
      brace.kind = TokenKind::LBrace;
      if (body_start < expr.fn_body_tokens.size()) {
        brace.line = expr.fn_body_tokens[body_start].line;
        brace.column = expr.fn_body_tokens[body_start].column;
      }
      tokens.push_back(brace);
      tokens.insert(tokens.end(), expr.fn_body_tokens.begin() + body_start, expr.fn_body_tokens.end());
      Token rbrace;
      rbrace.kind = TokenKind::RBrace;
      if (body_start < expr.fn_body_tokens.size()) {
        rbrace.line = expr.fn_body_tokens.back().line;
        rbrace.column = expr.fn_body_tokens.back().column;
      }
      tokens.push_back(rbrace);
      Token end;
      end.kind = TokenKind::End;
      tokens.push_back(end);

      Parser parser(std::move(tokens));
      if (!parser.ParseBlock(&lambda.body)) {
        if (error) *error = parser.Error();
        return false;
      }

      uint32_t func_id = st.base_func_count + static_cast<uint32_t>(st.lambda_funcs.size());
      st.func_ids[lambda.name] = func_id;
      TypeRef ret;
      if (!CloneTypeRef(lambda.return_type, &ret)) return false;
      st.func_returns.emplace(lambda.name, std::move(ret));
      std::vector<TypeRef> params;
      params.reserve(lambda.params.size());
      for (const auto& param : lambda.params) {
        TypeRef cloned;
        if (!CloneTypeRef(param.type, &cloned)) return false;
        params.push_back(std::move(cloned));
      }
      st.func_params.emplace(lambda.name, std::move(params));
      st.lambda_funcs.push_back(std::move(lambda));

      (*st.out) << "  newclosure " << st.lambda_funcs.back().name << " 0\n";
      return PushStack(st, 1);
    }
    case ExprKind::Member: {
      if (expr.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier) {
        auto enum_it = st.enum_values.find(base.text);
        if (enum_it != st.enum_values.end()) {
          auto member_it = enum_it->second.find(expr.text);
          if (member_it == enum_it->second.end()) {
            if (error) *error = "unknown enum member '" + expr.text + "'";
            return false;
          }
          (*st.out) << "  const.i32 " << member_it->second << "\n";
          return PushStack(st, 1);
        }
        const std::string key = base.text + "." + expr.text;
        if (st.module_func_names.find(key) != st.module_func_names.end()) {
          if (error) *error = "module function requires call: " + key;
          return false;
        }
        if (st.artifact_method_names.find(key) != st.artifact_method_names.end()) {
          if (error) *error = "artifact method requires call: " + key;
          return false;
        }
      }
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << expr.text << "\n";
      PopStack(st, 1);
      PushStack(st, 1);
      return true;
    }
    default:
      if (error) *error = "expression not supported for SIR emission";
      return false;
  }
}

bool EmitDefaultInit(EmitState& st, const TypeRef& type, std::string* error) {
  if (!IsSupportedType(type) || type.name == "void") {
    if (error) *error = "unsupported default init type '" + type.name + "'";
    return false;
  }
  if (type.is_proc) {
    (*st.out) << "  const.null\n";
    return PushStack(st, 1);
  }
  if (st.artifacts.find(type.name) != st.artifacts.end()) {
    (*st.out) << "  const.null\n";
    return PushStack(st, 1);
  }
  if (st.enum_values.find(type.name) != st.enum_values.end()) {
    (*st.out) << "  const.i32 0\n";
    return PushStack(st, 1);
  }
  if (!type.dims.empty()) {
    (*st.out) << "  const.null\n";
    return PushStack(st, 1);
  }
  if (type.name == "string") {
    Expr expr;
    expr.kind = ExprKind::Literal;
    expr.literal_kind = LiteralKind::String;
    expr.text.clear();
    return EmitConstForType(st, type, expr, error);
  }
  Expr expr;
  expr.kind = ExprKind::Literal;
  expr.literal_kind = LiteralKind::Integer;
  expr.text = "0";
  return EmitConstForType(st, type, expr, error);
}

bool EmitBlock(EmitState& st, const std::vector<Stmt>& body, std::string* error) {
  for (const auto& stmt : body) {
    if (!EmitStmt(st, stmt, error)) return false;
  }
  return true;
}

bool EmitIfChain(EmitState& st,
                 const std::vector<std::pair<Expr, std::vector<Stmt>>>& branches,
                 const std::vector<Stmt>& else_branch,
                 std::string* error) {
  std::string end_label = NewLabel(st, "if_end_");
  for (size_t i = 0; i < branches.size(); ++i) {
    const auto& branch = branches[i];
    std::string next_label = NewLabel(st, "if_next_");
    if (!EmitExpr(st, branch.first, nullptr, error)) return false;
    (*st.out) << "  jmp.false " << next_label << "\n";
    PopStack(st, 1);
    if (!EmitBlock(st, branch.second, error)) return false;
    (*st.out) << "  jmp " << end_label << "\n";
    (*st.out) << next_label << ":\n";
  }
  if (!else_branch.empty()) {
    if (!EmitBlock(st, else_branch, error)) return false;
  }
  (*st.out) << end_label << ":\n";
  return true;
}

bool EmitStmt(EmitState& st, const Stmt& stmt, std::string* error) {
  switch (stmt.kind) {
    case StmtKind::VarDecl: {
      const VarDecl& var = stmt.var_decl;
      if (!IsSupportedType(var.type)) {
        if (error) *error = "unsupported type for local '" + var.name + "'";
        return false;
      }
      if (st.local_indices.find(var.name) != st.local_indices.end()) {
        if (error) *error = "duplicate local '" + var.name + "'";
        return false;
      }
      uint16_t index = st.next_local++;
      st.local_indices[var.name] = index;
      TypeRef cloned;
      if (!CloneTypeRef(var.type, &cloned)) return false;
      st.local_types.emplace(var.name, std::move(cloned));
      if (var.has_init_expr) {
      if (!EmitExpr(st, var.init_expr, &var.type, error)) return false;
    } else {
      if (!EmitDefaultInit(st, var.type, error)) return false;
    }
    (*st.out) << "  stloc " << index << "\n";
    PopStack(st, 1);
    return true;
  }
    case StmtKind::Assign: {
      if (stmt.target.kind == ExprKind::Identifier) {
        auto type_it = st.local_types.find(stmt.target.text);
        if (type_it == st.local_types.end()) {
          if (error) *error = "unknown type for local '" + stmt.target.text + "'";
          return false;
        }
        return EmitLocalAssignment(st, stmt.target.text, type_it->second, stmt.expr, stmt.assign_op, false, error);
      }
      if (stmt.target.kind == ExprKind::Index) {
        if (stmt.target.children.size() != 2) {
          if (error) *error = "index assignment expects target and index";
          return false;
        }
        TypeRef container_type;
        if (!InferExprType(stmt.target.children[0], st, &container_type, error)) return false;
        if (container_type.dims.empty()) {
          if (error) *error = "index assignment expects array or list target";
          return false;
        }
        TypeRef element_type;
        if (!CloneElementType(container_type, &element_type)) {
          if (error) *error = "failed to resolve index element type";
          return false;
        }
        const char* op_suffix = VmOpSuffixForType(element_type);
        if (!op_suffix) {
          if (error) *error = "unsupported index assignment element type for SIR emission";
          return false;
        }
        if (!EmitExpr(st, stmt.target.children[0], &container_type, error)) return false;
        TypeRef index_type;
        index_type.name = "i32";
        if (!EmitExpr(st, stmt.target.children[1], &index_type, error)) return false;
        if (stmt.assign_op != "=") {
          if (!EmitDup2(st)) return false;
          if (container_type.dims.front().is_list) {
            (*st.out) << "  list.get." << op_suffix << "\n";
          } else {
            (*st.out) << "  array.get." << op_suffix << "\n";
          }
          PopStack(st, 2);
          PushStack(st, 1);
          if (!EmitExpr(st, stmt.expr, &element_type, error)) return false;
          PopStack(st, 1);
          const char* bin_op = AssignOpToBinaryOp(stmt.assign_op);
          if (!bin_op) {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          const char* op_type = nullptr;
          if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
              std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
            op_type = NormalizeBitwiseOpType(element_type.name);
          } else {
            op_type = NormalizeNumericOpType(element_type.name);
          }
          if (!op_type) {
            if (error) *error = "unsupported operand type for '" + stmt.assign_op + "'";
            return false;
          }
          if (std::string(bin_op) == "+") {
            (*st.out) << "  add." << op_type << "\n";
          } else if (std::string(bin_op) == "-") {
            (*st.out) << "  sub." << op_type << "\n";
          } else if (std::string(bin_op) == "*") {
            (*st.out) << "  mul." << op_type << "\n";
          } else if (std::string(bin_op) == "/") {
            (*st.out) << "  div." << op_type << "\n";
          } else if (std::string(bin_op) == "%" && IsIntegralType(element_type.name)) {
            (*st.out) << "  mod." << op_type << "\n";
          } else if (std::string(bin_op) == "&") {
            (*st.out) << "  and." << op_type << "\n";
          } else if (std::string(bin_op) == "|") {
            (*st.out) << "  or." << op_type << "\n";
          } else if (std::string(bin_op) == "^") {
            (*st.out) << "  xor." << op_type << "\n";
          } else if (std::string(bin_op) == "<<") {
            (*st.out) << "  shl." << op_type << "\n";
          } else if (std::string(bin_op) == ">>") {
            (*st.out) << "  shr." << op_type << "\n";
          } else {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          if (container_type.dims.front().is_list) {
            (*st.out) << "  list.set." << op_suffix << "\n";
          } else {
            (*st.out) << "  array.set." << op_suffix << "\n";
          }
          PopStack(st, 3);
          return true;
        }
        if (!EmitExpr(st, stmt.expr, &element_type, error)) return false;
        if (container_type.dims.front().is_list) {
          (*st.out) << "  list.set." << op_suffix << "\n";
        } else {
          (*st.out) << "  array.set." << op_suffix << "\n";
        }
        PopStack(st, 3);
        return true;
      }
      if (stmt.target.kind == ExprKind::Member) {
        if (stmt.target.children.empty()) {
          if (error) *error = "member assignment missing base";
          return false;
        }
        const Expr& base = stmt.target.children[0];
        TypeRef base_type;
        if (!InferExprType(base, st, &base_type, error)) return false;
        auto layout_it = st.artifact_layouts.find(base_type.name);
        if (layout_it == st.artifact_layouts.end()) {
          if (error) *error = "member assignment base is not an artifact";
          return false;
        }
        auto field_it = layout_it->second.field_index.find(stmt.target.text);
        if (field_it == layout_it->second.field_index.end()) {
          if (error) *error = "unknown field '" + stmt.target.text + "'";
          return false;
        }
        const TypeRef& field_type = layout_it->second.fields[field_it->second].type;
        if (!EmitExpr(st, base, &base_type, error)) return false;
        if (stmt.assign_op != "=") {
          if (!EmitDup(st)) return false;
          (*st.out) << "  ldfld " << base_type.name << "." << stmt.target.text << "\n";
          if (!EmitExpr(st, stmt.expr, &field_type, error)) return false;
          PopStack(st, 1);
          const char* bin_op = AssignOpToBinaryOp(stmt.assign_op);
          if (!bin_op) {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          const char* op_type = nullptr;
          if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
              std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
            op_type = NormalizeBitwiseOpType(field_type.name);
          } else {
            op_type = NormalizeNumericOpType(field_type.name);
          }
          if (!op_type) {
            if (error) *error = "unsupported operand type for '" + stmt.assign_op + "'";
            return false;
          }
          if (std::string(bin_op) == "+") {
            (*st.out) << "  add." << op_type << "\n";
          } else if (std::string(bin_op) == "-") {
            (*st.out) << "  sub." << op_type << "\n";
          } else if (std::string(bin_op) == "*") {
            (*st.out) << "  mul." << op_type << "\n";
          } else if (std::string(bin_op) == "/") {
            (*st.out) << "  div." << op_type << "\n";
          } else if (std::string(bin_op) == "%" && IsIntegralType(field_type.name)) {
            (*st.out) << "  mod." << op_type << "\n";
          } else if (std::string(bin_op) == "&") {
            (*st.out) << "  and." << op_type << "\n";
          } else if (std::string(bin_op) == "|") {
            (*st.out) << "  or." << op_type << "\n";
          } else if (std::string(bin_op) == "^") {
            (*st.out) << "  xor." << op_type << "\n";
          } else if (std::string(bin_op) == "<<") {
            (*st.out) << "  shl." << op_type << "\n";
          } else if (std::string(bin_op) == ">>") {
            (*st.out) << "  shr." << op_type << "\n";
          } else {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          (*st.out) << "  stfld " << base_type.name << "." << stmt.target.text << "\n";
          PopStack(st, 2);
          return true;
        }
        if (!EmitExpr(st, stmt.expr, &field_type, error)) return false;
        (*st.out) << "  stfld " << base_type.name << "." << stmt.target.text << "\n";
        PopStack(st, 2);
        return true;
      }
      if (error) *error = "assignment target not supported in SIR emission";
      return false;
    }
    case StmtKind::Expr: {
      bool pop_result = true;
      TypeRef expr_type;
      if (InferExprType(stmt.expr, st, &expr_type, nullptr) && expr_type.name == "void") {
        pop_result = false;
      }
      if (!EmitExpr(st, stmt.expr, nullptr, error)) return false;
      if (pop_result) {
        (*st.out) << "  pop\n";
        PopStack(st, 1);
      }
      return true;
    }
    case StmtKind::Return: {
      if (stmt.has_return_expr) {
        const TypeRef* expected = nullptr;
        auto ret_it = st.func_returns.find(st.current_func);
        if (ret_it != st.func_returns.end() && ret_it->second.name != "void") {
          expected = &ret_it->second;
        }
        if (!EmitExpr(st, stmt.expr, expected, error)) return false;
      }
      (*st.out) << "  ret\n";
      st.stack_cur = 0;
      st.saw_return = true;
      return true;
    }
    case StmtKind::IfChain:
      return EmitIfChain(st, stmt.if_branches, stmt.else_branch, error);
    case StmtKind::IfStmt: {
      std::string else_label = NewLabel(st, "if_else_");
      std::string end_label = NewLabel(st, "if_end_");
      if (!EmitExpr(st, stmt.if_cond, nullptr, error)) return false;
      (*st.out) << "  jmp.false " << else_label << "\n";
      PopStack(st, 1);
      if (!EmitBlock(st, stmt.if_then, error)) return false;
      (*st.out) << "  jmp " << end_label << "\n";
      (*st.out) << else_label << ":\n";
      if (!stmt.if_else.empty()) {
        if (!EmitBlock(st, stmt.if_else, error)) return false;
      }
      (*st.out) << end_label << ":\n";
      return true;
    }
    case StmtKind::WhileLoop: {
      std::string start_label = NewLabel(st, "while_start_");
      std::string end_label = NewLabel(st, "while_end_");
      st.loop_stack.push_back({end_label, start_label});
      (*st.out) << start_label << ":\n";
      if (!EmitExpr(st, stmt.loop_cond, nullptr, error)) return false;
      (*st.out) << "  jmp.false " << end_label << "\n";
      PopStack(st, 1);
      if (!EmitBlock(st, stmt.loop_body, error)) return false;
      (*st.out) << "  jmp " << start_label << "\n";
      (*st.out) << end_label << ":\n";
      st.loop_stack.pop_back();
      return true;
    }
    case StmtKind::ForLoop: {
      std::string start_label = NewLabel(st, "for_start_");
      std::string step_label = NewLabel(st, "for_step_");
      std::string end_label = NewLabel(st, "for_end_");
      if (!EmitExpr(st, stmt.loop_iter, nullptr, error)) return false;
      (*st.out) << "  pop\n";
      PopStack(st, 1);
      st.loop_stack.push_back({end_label, step_label});
      (*st.out) << start_label << ":\n";
      if (!EmitExpr(st, stmt.loop_cond, nullptr, error)) return false;
      (*st.out) << "  jmp.false " << end_label << "\n";
      PopStack(st, 1);
      if (!EmitBlock(st, stmt.loop_body, error)) return false;
      (*st.out) << step_label << ":\n";
      if (!EmitExpr(st, stmt.loop_step, nullptr, error)) return false;
      (*st.out) << "  pop\n";
      PopStack(st, 1);
      (*st.out) << "  jmp " << start_label << "\n";
      (*st.out) << end_label << ":\n";
      st.loop_stack.pop_back();
      return true;
    }
    case StmtKind::Break: {
      if (st.loop_stack.empty()) {
        if (error) *error = "break outside loop";
        return false;
      }
      (*st.out) << "  jmp " << st.loop_stack.back().break_label << "\n";
      return true;
    }
    case StmtKind::Skip: {
      if (st.loop_stack.empty()) {
        if (error) *error = "skip outside loop";
        return false;
      }
      (*st.out) << "  jmp " << st.loop_stack.back().continue_label << "\n";
      return true;
    }
    default:
      if (error) *error = "statement not supported for SIR emission";
      return false;
  }
}

bool EmitFunction(EmitState& st,
                  const FuncDecl& fn,
                  const std::string& emit_name,
                  const std::string& display_name,
                  const TypeRef* implicit_self,
                  std::string* out,
                  std::string* error) {
  if (!fn.generics.empty()) {
    if (error) *error = "generic functions not supported in SIR emission";
    return false;
  }
  if (!IsSupportedType(fn.return_type)) {
    if (error) *error = "unsupported return type for function '" + display_name + "'";
    return false;
  }
  st.current_func = emit_name;
  st.local_indices.clear();
  st.local_types.clear();
  st.next_local = 0;
  st.stack_cur = 0;
  st.stack_max = 0;
  st.saw_return = false;
  st.label_counter = 0;
  st.loop_stack.clear();
  uint16_t locals_count = 0;
  for (const auto& stmt : fn.body) {
    if (stmt.kind == StmtKind::VarDecl) locals_count++;
  }
  uint16_t param_count = static_cast<uint16_t>(fn.params.size());
  if (implicit_self) {
    param_count = static_cast<uint16_t>(param_count + 1);
  }
  uint16_t total_locals = static_cast<uint16_t>(locals_count + param_count);
  std::ostringstream func_out;
  st.out = &func_out;

  (*st.out) << "func " << emit_name << " locals=" << total_locals << " stack=0 sig=" << emit_name << "\n";
  (*st.out) << "  enter " << total_locals << "\n";

  if (implicit_self) {
    uint16_t index = st.next_local++;
    st.local_indices.emplace("self", index);
    TypeRef cloned;
    if (!CloneTypeRef(*implicit_self, &cloned)) return false;
    st.local_types.emplace("self", std::move(cloned));
  }

  for (const auto& param : fn.params) {
    uint16_t index = st.next_local++;
    st.local_indices.emplace(param.name, index);
    TypeRef cloned;
    if (!CloneTypeRef(param.type, &cloned)) return false;
    st.local_types.emplace(param.name, std::move(cloned));
  }

  for (const auto& stmt : fn.body) {
    if (!EmitStmt(st, stmt, error)) {
      if (error && !error->empty()) {
        *error = "in function '" + display_name + "': " + *error;
      }
      return false;
    }
  }

  if (!st.saw_return) {
    if (fn.name == "main" && fn.return_type.name == "i32") {
      (*st.out) << "  const.i32 0\n";
      PushStack(st, 1);
    }
    (*st.out) << "  ret\n";
  }

  std::string func_body = func_out.str();
  st.out = nullptr;

  size_t header_end = func_body.find('\n');
  std::string header = func_body.substr(0, header_end);
  std::string body = func_body.substr(header_end + 1);

  header = "func " + emit_name +
           " locals=" + std::to_string(total_locals) +
           " stack=" + std::to_string(st.stack_max > 0 ? st.stack_max : 8) +
           " sig=" + emit_name;

  func_out.str(std::string());
  func_out.clear();
  func_out << header << "\n" << body << "end\n";
  st.out = nullptr;
  func_body = func_out.str();
  if (out) *out = func_body;
  return true;
}

bool EmitProgramImpl(const Program& program, std::string* out, std::string* error) {
  EmitState st;
  st.error = error;

  std::vector<FuncItem> functions;
  std::vector<const ArtifactDecl*> artifacts;
  std::vector<const EnumDecl*> enums;
  for (const auto& decl : program.decls) {
    if (decl.kind == DeclKind::Function) {
      functions.push_back({&decl.func, decl.func.name, decl.func.name, false, {}});
    } else if (decl.kind == DeclKind::Artifact) {
      artifacts.push_back(&decl.artifact);
      st.artifacts.emplace(decl.artifact.name, &decl.artifact);
      for (const auto& method : decl.artifact.methods) {
        const std::string emit_name = decl.artifact.name + "__" + method.name;
        const std::string display = decl.artifact.name + "." + method.name;
        st.artifact_method_names.emplace(display, emit_name);
        FuncItem item;
        item.decl = &method;
        item.emit_name = emit_name;
        item.display_name = display;
        item.has_self = true;
        item.self_type.name = decl.artifact.name;
        functions.push_back(std::move(item));
      }
    } else if (decl.kind == DeclKind::Enum) {
      enums.push_back(&decl.enm);
      std::unordered_map<std::string, int64_t> values;
      for (const auto& member : decl.enm.members) {
        int64_t value = 0;
        if (member.has_value) {
          if (!ParseIntegerLiteralText(member.value_text, &value)) {
            if (error) *error = "invalid enum value for " + decl.enm.name + "." + member.name;
            return false;
          }
        }
        values.emplace(member.name, value);
      }
      st.enum_values.emplace(decl.enm.name, std::move(values));
    } else if (decl.kind == DeclKind::Module) {
      if (!decl.module.variables.empty()) {
        if (error) *error = "module variables are not supported in SIR emission";
        return false;
      }
      for (const auto& fn : decl.module.functions) {
        const std::string key = decl.module.name + "." + fn.name;
        const std::string emit_name = decl.module.name + "__" + fn.name;
        st.module_func_names.emplace(key, emit_name);
        functions.push_back({&fn, emit_name, key, false, {}});
      }
    } else if (decl.kind == DeclKind::Variable) {
      if (error) *error = "top-level variables are not supported in SIR emission";
      return false;
    } else {
      if (error) *error = "unsupported top-level declaration in SIR emission";
      return false;
    }
  }
  if (functions.empty()) {
    if (error) *error = "program has no functions";
    return false;
  }

  for (size_t i = 0; i < functions.size(); ++i) {
    st.func_ids[functions[i].emit_name] = static_cast<uint32_t>(i);
    TypeRef ret;
    if (!CloneTypeRef(functions[i].decl->return_type, &ret)) return false;
    st.func_returns.emplace(functions[i].emit_name, std::move(ret));
    std::vector<TypeRef> params;
    params.reserve(functions[i].decl->params.size() + (functions[i].has_self ? 1u : 0u));
    if (functions[i].has_self) {
      TypeRef cloned;
      if (!CloneTypeRef(functions[i].self_type, &cloned)) return false;
      params.push_back(std::move(cloned));
    }
    for (const auto& param : functions[i].decl->params) {
      TypeRef cloned;
      if (!CloneTypeRef(param.type, &cloned)) return false;
      params.push_back(std::move(cloned));
    }
    st.func_params.emplace(functions[i].emit_name, std::move(params));
  }
  st.base_func_count = static_cast<uint32_t>(functions.size());

  for (const auto* artifact : artifacts) {
    EmitState::ArtifactLayout layout;
    uint32_t offset = 0;
    uint32_t max_align = 1;
    layout.fields.reserve(artifact->fields.size());
    for (const auto& field : artifact->fields) {
      EmitState::FieldLayout field_layout;
      field_layout.name = field.name;
      if (!CloneTypeRef(field.type, &field_layout.type)) return false;
      field_layout.sir_type = FieldSirTypeName(field.type, st);
      uint32_t align = FieldAlignForType(field.type);
      uint32_t size = FieldSizeForType(field.type);
      offset = AlignTo(offset, align);
      field_layout.offset = offset;
      offset += size;
      if (align > max_align) max_align = align;
      layout.field_index[field.name] = layout.fields.size();
      layout.fields.push_back(std::move(field_layout));
    }
    layout.size = AlignTo(offset, max_align);
    st.artifact_layouts.emplace(artifact->name, std::move(layout));
  }

  std::vector<std::string> function_text;
  function_text.reserve(functions.size());
  for (const auto& item : functions) {
    std::string func_body;
    if (!EmitFunction(st,
                      *item.decl,
                      item.emit_name,
                      item.display_name,
                      item.has_self ? &item.self_type : nullptr,
                      &func_body,
                      error)) {
      return false;
    }
    function_text.push_back(std::move(func_body));
  }

  for (size_t i = 0; i < st.lambda_funcs.size(); ++i) {
    std::string func_body;
    if (!EmitFunction(st,
                      st.lambda_funcs[i],
                      st.lambda_funcs[i].name,
                      st.lambda_funcs[i].name,
                      nullptr,
                      &func_body,
                      error)) {
      return false;
    }
    function_text.push_back(std::move(func_body));
  }

  std::ostringstream result;
  if (!artifacts.empty() || !enums.empty()) {
    result << "types:\n";
    for (const auto* artifact : artifacts) {
      auto it = st.artifact_layouts.find(artifact->name);
      if (it == st.artifact_layouts.end()) return false;
      const auto& layout = it->second;
      result << "  type " << artifact->name << " size=" << layout.size << " kind=artifact\n";
      for (const auto& field : layout.fields) {
        result << "  field " << field.name << " " << field.sir_type << " offset=" << field.offset << "\n";
      }
    }
    for (const auto* enm : enums) {
      result << "  type " << enm->name << " size=4 kind=i32\n";
    }
  }

  result << "sigs:\n";
  struct SigItem {
    const FuncDecl* decl = nullptr;
    std::string name;
    bool has_self = false;
    TypeRef self_type;
  };
  std::vector<SigItem> all_functions;
  all_functions.reserve(functions.size() + st.lambda_funcs.size());
  for (const auto& item : functions) {
    SigItem sig;
    sig.decl = item.decl;
    sig.name = item.emit_name;
    sig.has_self = item.has_self;
    if (item.has_self) {
      if (!CloneTypeRef(item.self_type, &sig.self_type)) return false;
    }
    all_functions.push_back(std::move(sig));
  }
  for (const auto& fn : st.lambda_funcs) {
    all_functions.push_back({&fn, fn.name, false, {}});
  }
  for (const auto& fn : all_functions) {
    std::string ret = SigTypeNameFromType(fn.decl->return_type, st, error);
    if (ret.empty()) {
      if (error && error->empty()) *error = "unsupported return type in signature: " + fn.decl->return_type.name;
      return false;
    }
    result << "  sig " << fn.name << ": (";
    bool first = true;
    if (fn.has_self) {
      std::string param = SigTypeNameFromType(fn.self_type, st, error);
      if (param.empty()) {
        if (error && error->empty()) *error = "unsupported self type in signature";
        return false;
      }
      result << param;
      first = false;
    }
    for (size_t i = 0; i < fn.decl->params.size(); ++i) {
      if (!first) result << ", ";
      std::string param = SigTypeNameFromType(fn.decl->params[i].type, st, error);
      if (param.empty()) {
        if (error && error->empty()) {
          *error = "unsupported param type in signature: " + fn.decl->params[i].type.name;
        }
        return false;
      }
      result << param;
      first = false;
    }
    result << ") -> " << ret << "\n";
  }
  for (const auto& line : st.proc_sig_lines) {
    result << line << "\n";
  }

  if (!st.const_lines.empty()) {
    result << "consts:\n";
    for (const auto& line : st.const_lines) {
      result << line << "\n";
    }
  }

  for (const auto& text : function_text) {
    result << text;
  }

  std::string entry_name = functions[0].emit_name;
  for (const auto& fn : functions) {
    if (fn.decl->name == "main") {
      entry_name = fn.emit_name;
      break;
    }
  }
  result << "entry " << entry_name << "\n";

  if (out) *out = result.str();
  return true;
}

} // namespace

bool EmitSir(const Program& program, std::string* out, std::string* error) {
  std::string validate_error;
  if (!ValidateProgram(program, &validate_error)) {
    if (error) *error = validate_error;
    return false;
  }
  return EmitProgramImpl(program, out, error);
}

bool EmitSirFromString(const std::string& text, std::string* out, std::string* error) {
  Program program;
  std::string parse_error;
  if (!ParseProgramFromString(text, &program, &parse_error)) {
    if (error) *error = parse_error;
    return false;
  }
  return EmitSir(program, out, error);
}

} // namespace Simple::Lang
