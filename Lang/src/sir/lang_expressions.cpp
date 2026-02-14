bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
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
      if (it != st.local_types.end()) {
        return CloneTypeRef(it->second, out);
      }
      auto git = st.global_types.find(expr.text);
      if (git != st.global_types.end()) {
        return CloneTypeRef(git->second, out);
      }
      if (error) *error = "unknown local '" + expr.text + "'";
      return false;
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
      if (IsIntegerLiteralExpr(expr.children[0]) && IsIntegralType(right.name)) {
        return CloneTypeRef(right, out);
      }
      if (IsIntegerLiteralExpr(expr.children[1]) && IsIntegralType(left.name)) {
        return CloneTypeRef(left, out);
      }
      if (IsFloatLiteralExpr(expr.children[0]) && IsFloatType(right.name)) {
        return CloneTypeRef(right, out);
      }
      if (IsFloatLiteralExpr(expr.children[1]) && IsFloatType(left.name)) {
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
        std::string resolved;
        if (ResolveReservedModuleName(st, base.text, &resolved) &&
            resolved == "Core.Math" && expr.text == "PI") {
          out->name = "f64";
          return true;
        }
        if (ResolveReservedModuleName(st, base.text, &resolved) &&
            resolved == "Core.DL" && expr.text == "supported") {
          out->name = "bool";
          return true;
        }
        if (ResolveReservedModuleName(st, base.text, &resolved) &&
            resolved == "Core.OS" &&
            (expr.text == "is_linux" || expr.text == "is_macos" ||
             expr.text == "is_windows" || expr.text == "has_dl")) {
          out->name = "bool";
          return true;
        }
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
        std::string cast_target;
        if (GetAtCastTargetName(callee.text, &cast_target)) {
          out->name = cast_target;
          return true;
        }
        auto it = st.func_returns.find(callee.text);
        if (it != st.func_returns.end()) {
          return CloneTypeRef(it->second, out);
        }
        auto ext_it = st.extern_returns.find(callee.text);
        if (ext_it != st.extern_returns.end()) {
          return CloneTypeRef(ext_it->second, out);
        }
        auto local_it = st.local_types.find(callee.text);
        if (local_it != st.local_types.end() && local_it->second.is_proc) {
          if (local_it->second.proc_return) return CloneTypeRef(*local_it->second.proc_return, out);
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_is_callback = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        auto global_it = st.global_types.find(callee.text);
        if (global_it != st.global_types.end() && global_it->second.is_proc) {
          if (global_it->second.proc_return) return CloneTypeRef(*global_it->second.proc_return, out);
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_is_callback = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
      }
      if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
        const Expr& base = callee.children[0];
        if (IsIoPrintCallExpr(callee, st)) {
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_is_callback = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          if (ResolveDlModuleForIdentifier(base.text, st, &dl_module)) {
            auto ext_mod_it = st.extern_returns_by_module.find(dl_module);
            if (ext_mod_it != st.extern_returns_by_module.end()) {
              auto ext_it = ext_mod_it->second.find(callee.text);
              if (ext_it != ext_mod_it->second.end()) {
                return CloneTypeRef(ext_it->second, out);
              }
            }
          }
        }
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name)) {
          std::string reserved_module;
          const bool has_reserved_module =
              ResolveReservedModuleName(st, module_name, &reserved_module);
          if (has_reserved_module) {
            const std::string member_name =
                (reserved_module == "Core.DL") ? NormalizeCoreDlMember(callee.text) : callee.text;
            if (reserved_module == "Core.Math" &&
                (member_name == "abs" || member_name == "min" || member_name == "max") &&
                !expr.args.empty()) {
              if (!InferExprType(expr.args[0], st, out, nullptr)) return false;
              return true;
            }
            if (reserved_module == "Core.Time" &&
                (member_name == "mono_ns" || member_name == "wall_ns")) {
              out->name = "i64";
              out->type_args.clear();
              out->dims.clear();
              out->is_proc = false;
              out->proc_is_callback = false;
              out->proc_params.clear();
              out->proc_return.reset();
              return true;
            }
          }
          auto ext_mod_it = st.extern_returns_by_module.find(module_name);
          std::string ext_module_name = module_name;
          const bool ext_is_core_dl =
              (module_name == "Core.DL") || (has_reserved_module && reserved_module == "Core.DL");
          if (ext_mod_it == st.extern_returns_by_module.end() && has_reserved_module) {
            ext_mod_it = st.extern_returns_by_module.find(reserved_module);
            if (ext_mod_it != st.extern_returns_by_module.end()) {
              ext_module_name = reserved_module;
            }
          }
          if (ext_mod_it != st.extern_returns_by_module.end()) {
            const std::string member_name =
                ext_is_core_dl ? NormalizeCoreDlMember(callee.text) : callee.text;
            auto ext_it = ext_mod_it->second.find(member_name);
            if (ext_it != ext_mod_it->second.end()) {
              return CloneTypeRef(ext_it->second, out);
            }
          }
          std::string resolved_module_name;
          const bool module_is_reserved =
              ResolveReservedModuleName(st, module_name, &resolved_module_name);
          const bool module_is_core_dl =
              module_name == "Core.DL" || (module_is_reserved && resolved_module_name == "Core.DL");
          const std::string member_name =
              module_is_core_dl ? NormalizeCoreDlMember(callee.text) : callee.text;
          const std::string key = module_name + "." + member_name;
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
          if (!base_type.dims.empty() && base_type.dims.front().is_list) {
            TypeRef element_type;
            if (!CloneElementType(base_type, &element_type)) return false;
            if (callee.text == "len") {
              out->name = "i32";
              out->type_args.clear();
              out->dims.clear();
              out->is_proc = false;
              out->proc_is_callback = false;
              out->proc_params.clear();
              out->proc_return.reset();
              return true;
            }
            if (callee.text == "push" || callee.text == "insert" || callee.text == "clear") {
              out->name = "void";
              out->type_args.clear();
              out->dims.clear();
              out->is_proc = false;
              out->proc_is_callback = false;
              out->proc_params.clear();
              out->proc_return.reset();
              return true;
            }
            if (callee.text == "pop" || callee.text == "remove") {
              return CloneTypeRef(element_type, out);
            }
          }
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

bool EmitListIndexSetOp(EmitState& st, const char* op_suffix);
bool EmitListIndexGetOp(EmitState& st, const char* op_suffix);
bool EmitListLiteral(EmitState& st,
                     const Expr& expr,
                     const TypeRef& element_type,
                     const char* op_suffix,
                     const char* type_name,
                     std::string* error);
bool EmitArrayIndexSetOp(EmitState& st, const char* op_suffix);
bool EmitArrayIndexGetOp(EmitState& st, const char* op_suffix);
bool EmitArrayLiteral(EmitState& st,
                      const Expr& expr,
                      const TypeRef& element_type,
                      const char* op_suffix,
                      const char* type_name,
                      std::string* error);
bool EmitArrayLenOp(EmitState& st);

bool EmitIndexSetOp(EmitState& st,
                    const TypeRef& container_type,
                    const char* op_suffix) {
  if (container_type.dims.front().is_list) {
    return EmitListIndexSetOp(st, op_suffix);
  }
  return EmitArrayIndexSetOp(st, op_suffix);
}

bool EmitIndexGetOp(EmitState& st,
                    const TypeRef& container_type,
                    const char* op_suffix) {
  if (container_type.dims.front().is_list) {
    return EmitListIndexGetOp(st, op_suffix);
  }
  return EmitArrayIndexGetOp(st, op_suffix);
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

bool EmitGlobalAssignment(EmitState& st,
                          const std::string& name,
                          const TypeRef& type,
                          const Expr& value,
                          const std::string& op,
                          bool return_value,
                          std::string* error) {
  auto it = st.global_indices.find(name);
  if (it == st.global_indices.end()) {
    if (error) *error = "unknown global '" + name + "'";
    return false;
  }
  if (op == "=") {
    if (!EmitExpr(st, value, &type, error)) return false;
    (*st.out) << "  stglob " << it->second << "\n";
    PopStack(st, 1);
    if (return_value) {
      (*st.out) << "  ldglob " << it->second << "\n";
      PushStack(st, 1);
    }
    return true;
  }

  const char* bin_op = AssignOpToBinaryOp(op);
  if (!bin_op) {
    if (error) *error = "unsupported assignment operator '" + op + "'";
    return false;
  }
  (*st.out) << "  ldglob " << it->second << "\n";
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
  (*st.out) << "  stglob " << it->second << "\n";
  PopStack(st, 1);
  if (return_value) {
    (*st.out) << "  ldglob " << it->second << "\n";
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
    if (type_it != st.local_types.end()) {
      return EmitLocalAssignment(st, target.text, type_it->second, expr.children[1], expr.op, true, error);
    }
    auto gtype_it = st.global_types.find(target.text);
    if (gtype_it != st.global_types.end()) {
      return EmitGlobalAssignment(st, target.text, gtype_it->second, expr.children[1], expr.op, true, error);
    }
    if (error) *error = "unknown type for local '" + target.text + "'";
    return false;
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
    const bool lhs_lit = IsIntegerLiteralExpr(expr.children[0]);
    const bool rhs_lit = IsIntegerLiteralExpr(expr.children[1]);
    const bool lhs_int = IsIntegralType(left_type.name);
    const bool rhs_int = IsIntegralType(right_type.name);
    if (lhs_lit && rhs_int) {
      if (!CloneTypeRef(right_type, &left_type)) return false;
    } else if (rhs_lit && lhs_int) {
      if (!CloneTypeRef(left_type, &right_type)) return false;
    } else if (IsFloatLiteralExpr(expr.children[0]) && IsFloatType(right_type.name)) {
      if (!CloneTypeRef(right_type, &left_type)) return false;
    } else if (IsFloatLiteralExpr(expr.children[1]) && IsFloatType(left_type.name)) {
      if (!CloneTypeRef(left_type, &right_type)) return false;
    } else {
      if (error) *error = "operand type mismatch for '" + expr.op + "'";
      return false;
    }
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
      if (it != st.local_indices.end()) {
        (*st.out) << "  ldloc " << it->second << "\n";
        return PushStack(st, 1);
      }
      auto git = st.global_indices.find(expr.text);
      if (git != st.global_indices.end()) {
        (*st.out) << "  ldglob " << git->second << "\n";
        return PushStack(st, 1);
      }
      if (error) *error = "unknown local '" + expr.text + "'";
      return false;
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
        if (IsIoPrintCallExpr(callee, st)) {
          if (expr.args.empty()) {
            if (error) *error = "call argument count mismatch for 'IO." + callee.text + "'";
            return false;
          }
          if (expr.args.size() == 1) {
            TypeRef arg_type;
            if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
            if (!EmitPrintAnyValue(st, expr.args[0], arg_type, error)) return false;
          } else {
            const Expr& fmt_expr = expr.args[0];
            if (!(fmt_expr.kind == ExprKind::Literal &&
                  fmt_expr.literal_kind == LiteralKind::String)) {
              if (error) *error = "IO.print format call expects string literal as first argument";
              return false;
            }
            size_t placeholder_count = 0;
            std::vector<std::string> segments;
            if (!CountFormatPlaceholders(fmt_expr.text, &placeholder_count, &segments, error)) {
              return false;
            }
            if (placeholder_count != expr.args.size() - 1) {
              if (error) {
                *error = "IO.print format placeholder count mismatch: expected " +
                         std::to_string(placeholder_count) + ", got " +
                         std::to_string(expr.args.size() - 1);
              }
              return false;
            }
            for (size_t i = 0; i < placeholder_count; ++i) {
              if (!segments[i].empty()) {
                TypeRef seg_type = MakeTypeRef("string");
                Expr seg_expr;
                seg_expr.kind = ExprKind::Literal;
                seg_expr.literal_kind = LiteralKind::String;
                seg_expr.text = segments[i];
                if (!EmitPrintAnyValue(st, seg_expr, seg_type, error)) return false;
              }
              TypeRef arg_type;
              if (!InferExprType(expr.args[i + 1], st, &arg_type, error)) return false;
              if (!EmitPrintAnyValue(st, expr.args[i + 1], arg_type, error)) return false;
            }
            if (!segments.empty() && !segments.back().empty()) {
              TypeRef seg_type = MakeTypeRef("string");
              Expr seg_expr;
              seg_expr.kind = ExprKind::Literal;
              seg_expr.literal_kind = LiteralKind::String;
              seg_expr.text = segments.back();
              if (!EmitPrintAnyValue(st, seg_expr, seg_type, error)) return false;
            }
          }
          if (callee.text == "println") {
            if (!EmitPrintNewline(st, error)) return false;
          }
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          ResolveDlModuleForIdentifier(base.text, st, &dl_module);
          if (!dl_module.empty()) {
            auto params_mod_it = st.extern_params_by_module.find(dl_module);
            auto returns_mod_it = st.extern_returns_by_module.find(dl_module);
            if (params_mod_it == st.extern_params_by_module.end() ||
                returns_mod_it == st.extern_returns_by_module.end()) {
              if (error) *error = "unknown dynamic DL manifest module: " + dl_module;
              return false;
            }
            auto params_it = params_mod_it->second.find(callee.text);
            auto ret_it = returns_mod_it->second.find(callee.text);
            if (params_it == params_mod_it->second.end() || ret_it == returns_mod_it->second.end()) {
              if (error) *error = "unknown dynamic symbol: " + base.text + "." + callee.text;
              return false;
            }
            const auto& params = params_it->second;
            if (expr.args.size() != params.size()) {
              if (error) *error = "call argument count mismatch for dynamic symbol '" +
                                  base.text + "." + callee.text + "'";
              return false;
            }
            auto call_mod_it = st.dl_call_import_ids_by_module.find(dl_module);
            if (call_mod_it == st.dl_call_import_ids_by_module.end()) {
              if (error) *error = "missing dynamic DL call import module: " + dl_module;
              return false;
            }
            auto call_id_it = call_mod_it->second.find(callee.text);
            if (call_id_it == call_mod_it->second.end()) {
              if (error) *error = "missing dynamic DL call import: " + dl_module + "." + callee.text;
              return false;
            }
            std::string sym_import_id;
            if (!GetCoreDlSymImportId(st, &sym_import_id)) {
              if (error) *error = "missing Core.DL.sym import for dynamic symbol calls";
              return false;
            }
            TypeRef ptr_type = MakeTypeRef("i64");
            if (!EmitExpr(st, base, &ptr_type, error)) return false;
            std::string symbol_name;
            if (!AddStringConst(st, callee.text, &symbol_name)) return false;
            (*st.out) << "  const.string " << symbol_name << "\n";
            PushStack(st, 1);
            (*st.out) << "  call " << sym_import_id << " 2\n";
            PopStack(st, 2);
            PushStack(st, 1);
            uint32_t abi_arg_count = 1;
            for (size_t i = 0; i < params.size(); ++i) {
              if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
              ++abi_arg_count;
            }
            if (abi_arg_count > 255) {
              if (error) *error = "dynamic DL call has too many ABI parameters";
              return false;
            }
            (*st.out) << "  call " << call_id_it->second << " " << abi_arg_count << "\n";
            PopStack(st, abi_arg_count);
            if (ret_it->second.name != "void") PushStack(st, 1);
            return true;
          }
        }
        TypeRef list_type;
        if (InferExprType(base, st, &list_type, nullptr) &&
            !list_type.dims.empty() && list_type.dims.front().is_list) {
          if (EmitListMethodCall(st, expr, callee.text, base, list_type, error)) return true;
        }
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name)) {
          std::string resolved;
          if (!ResolveReservedModuleName(st, module_name, &resolved)) {
            // Not a reserved module; fall through to normal call handling.
          } else {
            const std::string reserved_module = resolved;
            if (reserved_module == "Core.Math") {
              if (callee.text == "abs") {
                if (expr.args.size() != 1) {
                  if (error) *error = "call argument count mismatch for 'Math.abs'";
                  return false;
                }
                TypeRef arg_type;
                if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
                uint32_t id = 0;
                if (arg_type.name == "i32") {
                  id = Simple::VM::kIntrinsicAbsI32;
                } else if (arg_type.name == "i64") {
                  id = Simple::VM::kIntrinsicAbsI64;
                } else {
                  if (error) *error = "Math.abs expects i32 or i64";
                  return false;
                }
                (*st.out) << "  intrinsic " << id << "\n";
                PopStack(st, 1);
                PushStack(st, 1);
                return true;
              }
            }
            const std::string member_name =
                (reserved_module == "Core.DL") ? NormalizeCoreDlMember(callee.text) : callee.text;
            if (reserved_module == "Core.DL") {
              if (member_name == "open") {
                if (expr.args.size() != 1 && expr.args.size() != 2) {
                  if (error) *error = "call argument count mismatch for 'Core.DL.open'";
                  return false;
                }
                auto ext_mod_it = st.extern_ids_by_module.find(reserved_module);
                if (ext_mod_it == st.extern_ids_by_module.end()) {
                  if (error) *error = "missing extern module for 'Core.DL.open'";
                  return false;
                }
                auto id_it = ext_mod_it->second.find(member_name);
                if (id_it == ext_mod_it->second.end()) {
                  if (error) *error = "missing extern id for 'Core.DL.open'";
                  return false;
                }
                auto params_it = st.extern_params_by_module[reserved_module].find(member_name);
                auto ret_it = st.extern_returns_by_module[reserved_module].find(member_name);
                if (params_it == st.extern_params_by_module[reserved_module].end() ||
                    ret_it == st.extern_returns_by_module[reserved_module].end()) {
                  if (error) *error = "missing signature for extern 'Core.DL.open'";
                  return false;
                }
                const auto& params = params_it->second;
                if (params.size() != 1) {
                  if (error) *error = "invalid extern signature for 'Core.DL.open'";
                  return false;
                }
                if (!EmitExpr(st, expr.args[0], &params[0], error)) return false;
                (*st.out) << "  call " << id_it->second << " 1\n";
                if (st.stack_cur >= 1) st.stack_cur -= 1;
                else st.stack_cur = 0;
                if (ret_it->second.name != "void") PushStack(st, 1);
                return true;
              }
              if (member_name == "call_i32") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'Core.DL.call_i32'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("i32");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallI32 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_i64") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'Core.DL.call_i64'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("i64");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallI64 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_f32") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'Core.DL.call_f32'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("f32");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallF32 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_f64") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'Core.DL.call_f64'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("f64");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallF64 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_str0") {
                if (expr.args.size() != 1) {
                  if (error) *error = "call argument count mismatch for 'Core.DL.call_str0'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallStr0 << "\n";
                PopStack(st, 1);
                PushStack(st, 1);
                return true;
              }
            }
            if (member_name == "min" || member_name == "max") {
              if (expr.args.size() != 2) {
                if (error) *error = "call argument count mismatch for 'Math." + callee.text + "'";
                return false;
              }
              TypeRef arg_type;
              if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
              if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
              if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
              uint32_t id = 0;
              if (arg_type.name == "i32") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinI32 : Simple::VM::kIntrinsicMaxI32;
              } else if (arg_type.name == "i64") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinI64 : Simple::VM::kIntrinsicMaxI64;
              } else if (arg_type.name == "f32") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinF32 : Simple::VM::kIntrinsicMaxF32;
              } else if (arg_type.name == "f64") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinF64 : Simple::VM::kIntrinsicMaxF64;
              } else {
                if (error) *error = "Math." + callee.text + " expects numeric type";
                return false;
              }
              (*st.out) << "  intrinsic " << id << "\n";
              PopStack(st, 2);
              PushStack(st, 1);
              return true;
            }
          }
          if (resolved == "Core.Time") {
            if (callee.text == "mono_ns") {
              if (!expr.args.empty()) {
                if (error) *error = "Time.mono_ns expects no arguments";
                return false;
              }
              (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicMonoNs << "\n";
              PushStack(st, 1);
              return true;
            }
            if (callee.text == "wall_ns") {
              if (!expr.args.empty()) {
                if (error) *error = "Time.wall_ns expects no arguments";
                return false;
              }
              (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicWallNs << "\n";
              PushStack(st, 1);
              return true;
            }
          }
        }
        if (GetModuleNameFromExpr(base, &module_name)) {
          std::string resolved_module_name;
          const bool module_is_reserved =
              ResolveReservedModuleName(st, module_name, &resolved_module_name);
          const bool module_is_core_dl =
              module_name == "Core.DL" || (module_is_reserved && resolved_module_name == "Core.DL");
          const std::string member_name =
              module_is_core_dl ? NormalizeCoreDlMember(callee.text) : callee.text;
          const std::string key = module_name + "." + member_name;
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
          std::string ext_module_name = module_name;
          std::string resolved_module_name_for_ext;
          const bool has_resolved_module_for_ext =
              ResolveReservedModuleName(st, module_name, &resolved_module_name_for_ext);
          bool ext_is_core_dl = (ext_module_name == "Core.DL") ||
                                (has_resolved_module_for_ext &&
                                 resolved_module_name_for_ext == "Core.DL");
          auto ext_mod_it = st.extern_ids_by_module.find(ext_module_name);
          if (ext_mod_it == st.extern_ids_by_module.end()) {
            if (has_resolved_module_for_ext) {
              ext_module_name = resolved_module_name_for_ext;
              ext_mod_it = st.extern_ids_by_module.find(ext_module_name);
            }
          }
          if (ext_mod_it != st.extern_ids_by_module.end()) {
            const std::string extern_member_name =
                ext_is_core_dl ? NormalizeCoreDlMember(callee.text) : callee.text;
            const std::string ext_key = ext_module_name + "." + extern_member_name;
            auto id_it = ext_mod_it->second.find(extern_member_name);
            if (id_it != ext_mod_it->second.end()) {
              auto params_it = st.extern_params_by_module[ext_module_name].find(extern_member_name);
              auto ret_it = st.extern_returns_by_module[ext_module_name].find(extern_member_name);
              if (params_it == st.extern_params_by_module[ext_module_name].end() ||
                  ret_it == st.extern_returns_by_module[ext_module_name].end()) {
                if (error) *error = "missing signature for extern '" + ext_key + "'";
                return false;
              }
              const auto& params = params_it->second;
              if (expr.args.size() != params.size()) {
                if (error) *error = "call argument count mismatch for '" + ext_key + "'";
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
              if (ret_it->second.name != "void") {
                PushStack(st, 1);
              }
              return true;
            }
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
            if (!EmitArrayLenOp(st)) return false;
          }
        } else {
          if (error) *error = "len expects array, list, or string argument";
          return false;
        }
        PopStack(st, 1);
        PushStack(st, 1);
        return true;
      }
      std::string cast_target;
      if (GetAtCastTargetName(name, &cast_target)) {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for '" + cast_target + "'";
          return false;
        }
        TypeRef arg_type;
        if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
        if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
        CastVmKind src = GetCastVmKind(arg_type.name);
        CastVmKind dst = GetCastVmKind(cast_target);
        if (src == CastVmKind::Invalid || dst == CastVmKind::Invalid) {
          if (error) *error = "unsupported cast in SIR emission: " + arg_type.name + " -> " + cast_target;
          return false;
        }
        if (src != dst) {
          if (src == CastVmKind::I32 && dst == CastVmKind::I64) {
            (*st.out) << "  conv.i32.i64\n";
          } else if (src == CastVmKind::I64 && dst == CastVmKind::I32) {
            (*st.out) << "  conv.i64.i32\n";
          } else if (src == CastVmKind::I32 && dst == CastVmKind::F32) {
            (*st.out) << "  conv.i32.f32\n";
          } else if (src == CastVmKind::I32 && dst == CastVmKind::F64) {
            (*st.out) << "  conv.i32.f64\n";
          } else if (src == CastVmKind::F32 && dst == CastVmKind::I32) {
            (*st.out) << "  conv.f32.i32\n";
          } else if (src == CastVmKind::F64 && dst == CastVmKind::I32) {
            (*st.out) << "  conv.f64.i32\n";
          } else if (src == CastVmKind::F32 && dst == CastVmKind::F64) {
            (*st.out) << "  conv.f32.f64\n";
          } else if (src == CastVmKind::F64 && dst == CastVmKind::F32) {
            (*st.out) << "  conv.f64.f32\n";
          } else {
            if (error) *error = "unsupported cast in SIR emission: " + arg_type.name + " -> " + cast_target;
            return false;
          }
        } else if (arg_type.name != cast_target) {
          // Normalize same-lane casts (for example i8 -> i32) to produce verifier-visible dst kind.
          if (dst == CastVmKind::I32 && cast_target == "i32") {
            if (arg_type.name == "bool") {
              if (error) *error = "unsupported cast in SIR emission: " + arg_type.name + " -> " + cast_target;
              return false;
            }
            (*st.out) << "  const.i32 0\n";
            PushStack(st, 1);
            (*st.out) << "  add.i32\n";
            PopStack(st, 2);
            PushStack(st, 1);
          } else if (dst == CastVmKind::I64 && cast_target == "i64" && arg_type.name == "u64") {
            (*st.out) << "  const.i64 -1\n";
            PushStack(st, 1);
            (*st.out) << "  and.i64\n";
            PopStack(st, 2);
            PushStack(st, 1);
          }
        }
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
          TypeRef call_type;
          if (!CloneTypeRef(proc_type, &call_type)) return false;
          if (proc_type.proc_is_callback) {
            call_type.proc_is_callback = false;
            call_type.proc_params.clear();
            for (const auto& arg : expr.args) {
              TypeRef arg_type;
              if (!InferExprType(arg, st, &arg_type, error)) return false;
              if (!EmitExpr(st, arg, &arg_type, error)) return false;
              call_type.proc_params.push_back(std::move(arg_type));
            }
            call_type.proc_return = std::make_unique<TypeRef>();
            call_type.proc_return->name = "void";
          } else {
            if (expr.args.size() != proc_type.proc_params.size()) {
              if (error) *error = "call argument count mismatch for '" + name + "'";
              return false;
            }
            for (size_t i = 0; i < proc_type.proc_params.size(); ++i) {
              if (!EmitExpr(st, expr.args[i], &proc_type.proc_params[i], error)) return false;
            }
          }
          if (!EmitExpr(st, callee, &proc_type, error)) return false;
          std::string sig_name = GetProcSigName(st, call_type, error);
          if (sig_name.empty()) return false;
          (*st.out) << "  call.indirect " << sig_name << " " << call_type.proc_params.size() << "\n";
          PopStack(st, static_cast<uint32_t>(call_type.proc_params.size() + 1));
          if (call_type.proc_return && call_type.proc_return->name != "void") {
            PushStack(st, 1);
          }
          return true;
        }
        auto ext_it = st.extern_ids.find(name);
        if (ext_it != st.extern_ids.end()) {
          auto params_it = st.extern_params.find(name);
          auto ret_it = st.extern_returns.find(name);
          if (params_it == st.extern_params.end() || ret_it == st.extern_returns.end()) {
            if (error) *error = "missing signature for extern '" + name + "'";
            return false;
          }
          const auto& params = params_it->second;
          if (expr.args.size() != params.size()) {
            if (error) *error = "call argument count mismatch for '" + name + "'";
            return false;
          }
          uint32_t abi_arg_count = 0;
          for (size_t i = 0; i < params.size(); ++i) {
            if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
            ++abi_arg_count;
          }
          (*st.out) << "  call " << ext_it->second << " " << abi_arg_count << "\n";
          if (st.stack_cur >= abi_arg_count) {
            st.stack_cur -= abi_arg_count;
          } else {
            st.stack_cur = 0;
          }
          if (ret_it->second.name != "void") {
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
      TypeRef call_type;
      if (!CloneTypeRef(callee_type, &call_type)) return false;
      if (callee_type.proc_is_callback) {
        call_type.proc_is_callback = false;
        call_type.proc_params.clear();
        for (const auto& arg : expr.args) {
          TypeRef arg_type;
          if (!InferExprType(arg, st, &arg_type, error)) return false;
          if (!EmitExpr(st, arg, &arg_type, error)) return false;
          call_type.proc_params.push_back(std::move(arg_type));
        }
        call_type.proc_return = std::make_unique<TypeRef>();
        call_type.proc_return->name = "void";
      } else {
        if (expr.args.size() != callee_type.proc_params.size()) {
          if (error) *error = "call argument count mismatch for callee";
          return false;
        }
        for (size_t i = 0; i < callee_type.proc_params.size(); ++i) {
          if (!EmitExpr(st, expr.args[i], &callee_type.proc_params[i], error)) return false;
        }
      }
      if (!EmitExpr(st, callee, &callee_type, error)) return false;
      std::string sig_name = GetProcSigName(st, call_type, error);
      if (sig_name.empty()) return false;
      (*st.out) << "  call.indirect " << sig_name << " " << call_type.proc_params.size() << "\n";
      PopStack(st, static_cast<uint32_t>(call_type.proc_params.size() + 1));
      if (call_type.proc_return && call_type.proc_return->name != "void") {
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
      if (is_list) {
        return EmitListLiteral(st, expr, element_type, op_suffix, type_name, error);
      }
      return EmitArrayLiteral(st, expr, element_type, op_suffix, type_name, error);
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
        std::string resolved;
        if (ResolveReservedModuleName(st, base.text, &resolved) &&
            resolved == "Core.Math" && expr.text == "PI") {
          (*st.out) << "  const.f64 3.141592653589793\n";
          return PushStack(st, 1);
        }
        if (ResolveReservedModuleName(st, base.text, &resolved) &&
            resolved == "Core.DL" && expr.text == "supported") {
          (*st.out) << "  const.i32 " << (HostHasDl() ? 1 : 0) << "\n";
          return PushStack(st, 1);
        }
        if (ResolveReservedModuleName(st, base.text, &resolved) &&
            resolved == "Core.OS" &&
            (expr.text == "is_linux" || expr.text == "is_macos" ||
             expr.text == "is_windows" || expr.text == "has_dl")) {
          bool value = false;
          if (expr.text == "is_linux") value = HostIsLinux();
          else if (expr.text == "is_macos") value = HostIsMacOs();
          else if (expr.text == "is_windows") value = HostIsWindows();
          else if (expr.text == "has_dl") value = HostHasDl();
          (*st.out) << "  const.i32 " << (value ? 1 : 0) << "\n";
          return PushStack(st, 1);
        }
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
