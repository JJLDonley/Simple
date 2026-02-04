#include "lang_sir.h"

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "lang_parser.h"
#include "lang_validate.h"

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

bool IsSupportedType(const TypeRef& type) {
  if (!type.type_args.empty()) return false;
  if (type.is_proc) return false;
  if (!type.dims.empty()) {
    if (type.name == "void") return false;
    return IsNumericType(type.name) || type.name == "bool" || type.name == "char" || type.name == "string";
  }
  return IsNumericType(type.name) || type.name == "bool" || type.name == "char" || type.name == "string" ||
         type.name == "void";
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
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\n':
      case '\r':
      case '\t':
        if (error) *error = "string literal contains control characters unsupported in SIR";
        return {};
      case '"':
      case '\\':
        if (error) *error = "string literal contains characters unsupported in SIR";
        return {};
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
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

const char* VmOpSuffixForType(const TypeRef& type) {
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

bool EmitAssignmentExpr(EmitState& st, const Expr& expr, std::string* error) {
  if (expr.children.size() != 2) {
    if (error) *error = "assignment missing operands";
    return false;
  }
  const Expr& target = expr.children[0];
  if (target.kind != ExprKind::Identifier) {
    if (error) *error = "assignment target not supported in SIR emission";
    return false;
  }
  auto it = st.local_indices.find(target.text);
  if (it == st.local_indices.end()) {
    if (error) *error = "unknown local '" + target.text + "'";
    return false;
  }
  auto type_it = st.local_types.find(target.text);
  if (type_it == st.local_types.end()) {
    if (error) *error = "unknown type for local '" + target.text + "'";
    return false;
  }
  if (!EmitExpr(st, expr.children[1], &type_it->second, error)) return false;
  (*st.out) << "  stloc " << it->second << "\n";
  PopStack(st, 1);
  (*st.out) << "  ldloc " << it->second << "\n";
  PushStack(st, 1);
  return true;
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

  if (expr.op == "=") {
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

  const char* op_type = NormalizeNumericOpType(type.name);
  if (!op_type) {
    if (error) *error = "unsupported operand type for '" + expr.op + "'";
    return false;
  }

  if (!EmitExpr(st, expr.children[0], &type, error)) return false;
  if (!EmitExpr(st, expr.children[1], &type, error)) return false;
  PopStack(st, 1);
  if (expr.op == "==" || expr.op == "!=" || expr.op == "<" || expr.op == "<=" ||
      expr.op == ">" || expr.op == ">=") {
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
  if (expr.op == "+") {
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
      if (callee.kind != ExprKind::Identifier) {
        if (error) *error = "call target not supported in SIR emission";
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
      if (stmt.assign_op != "=") {
        if (error) *error = "compound assignment not supported in SIR emission";
        return false;
      }
      if (stmt.target.kind == ExprKind::Identifier) {
        auto it = st.local_indices.find(stmt.target.text);
        if (it == st.local_indices.end()) {
          if (error) *error = "unknown local '" + stmt.target.text + "'";
          return false;
        }
        auto type_it = st.local_types.find(stmt.target.text);
        if (type_it == st.local_types.end()) {
          if (error) *error = "unknown type for local '" + stmt.target.text + "'";
          return false;
        }
        if (!EmitExpr(st, stmt.expr, &type_it->second, error)) return false;
        (*st.out) << "  stloc " << it->second << "\n";
        PopStack(st, 1);
        return true;
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
        if (!EmitExpr(st, stmt.expr, &element_type, error)) return false;
        if (container_type.dims.front().is_list) {
          (*st.out) << "  list.set." << op_suffix << "\n";
        } else {
          (*st.out) << "  array.set." << op_suffix << "\n";
        }
        PopStack(st, 3);
        return true;
      }
      if (error) *error = "assignment target not supported in SIR emission";
      return false;
    }
    case StmtKind::Expr: {
      if (!EmitExpr(st, stmt.expr, nullptr, error)) return false;
      (*st.out) << "  pop\n";
      PopStack(st, 1);
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

bool EmitFunction(EmitState& st, const FuncDecl& fn, std::string* out, std::string* error) {
  if (!fn.generics.empty()) {
    if (error) *error = "generic functions not supported in SIR emission";
    return false;
  }
  if (!IsSupportedType(fn.return_type)) {
    if (error) *error = "unsupported return type for function '" + fn.name + "'";
    return false;
  }
  st.current_func = fn.name;
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
  uint16_t total_locals = static_cast<uint16_t>(locals_count + param_count);
  std::ostringstream func_out;
  st.out = &func_out;

  (*st.out) << "func " << fn.name << " locals=" << total_locals << " stack=0 sig=" << fn.name << "\n";
  (*st.out) << "  enter " << total_locals << "\n";

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
        *error = "in function '" + fn.name + "': " + *error;
      }
      return false;
    }
  }

  if (!st.saw_return) {
    (*st.out) << "  ret\n";
  }

  std::string func_body = func_out.str();
  st.out = nullptr;

  size_t header_end = func_body.find('\n');
  std::string header = func_body.substr(0, header_end);
  std::string body = func_body.substr(header_end + 1);

  header = "func " + fn.name +
           " locals=" + std::to_string(total_locals) +
           " stack=" + std::to_string(st.stack_max > 0 ? st.stack_max : 8) +
           " sig=" + fn.name;

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

  std::vector<const FuncDecl*> functions;
  for (const auto& decl : program.decls) {
    if (decl.kind != DeclKind::Function) {
      if (error) *error = "only top-level functions are supported in SIR emission";
      return false;
    }
    functions.push_back(&decl.func);
  }
  if (functions.empty()) {
    if (error) *error = "program has no functions";
    return false;
  }

  for (size_t i = 0; i < functions.size(); ++i) {
    st.func_ids[functions[i]->name] = static_cast<uint32_t>(i);
    TypeRef ret;
    if (!CloneTypeRef(functions[i]->return_type, &ret)) return false;
    st.func_returns.emplace(functions[i]->name, std::move(ret));
    std::vector<TypeRef> params;
    params.reserve(functions[i]->params.size());
    for (const auto& param : functions[i]->params) {
      TypeRef cloned;
      if (!CloneTypeRef(param.type, &cloned)) return false;
      params.push_back(std::move(cloned));
    }
    st.func_params.emplace(functions[i]->name, std::move(params));
  }

  std::ostringstream result;
  result << "sigs:\n";
  for (const auto* fn : functions) {
    result << "  sig " << fn->name << ": (";
    for (size_t i = 0; i < fn->params.size(); ++i) {
      if (i > 0) result << ", ";
      result << fn->params[i].type.name;
    }
    result << ") -> " << fn->return_type.name << "\n";
  }

  std::vector<std::string> function_text;
  function_text.reserve(functions.size());
  for (const auto* fn : functions) {
    std::string func_body;
    if (!EmitFunction(st, *fn, &func_body, error)) return false;
    function_text.push_back(std::move(func_body));
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

  std::string entry_name = functions[0]->name;
  for (const auto* fn : functions) {
    if (fn->name == "main") {
      entry_name = fn->name;
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
