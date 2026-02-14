bool EmitListIndexSetOp(EmitState& st, const char* op_suffix);
bool EmitListIndexGetOp(EmitState& st, const char* op_suffix);

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
        std::string manifest_module;
        if (GetDlOpenManifestModule(var.init_expr, st, &manifest_module)) {
          st.local_dl_modules[var.name] = manifest_module;
        }
      }
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
        if (type_it != st.local_types.end()) {
          return EmitLocalAssignment(st, stmt.target.text, type_it->second, stmt.expr, stmt.assign_op, false, error);
        }
        auto gtype_it = st.global_types.find(stmt.target.text);
        if (gtype_it != st.global_types.end()) {
          return EmitGlobalAssignment(st, stmt.target.text, gtype_it->second, stmt.expr, stmt.assign_op, false, error);
        }
        if (error) *error = "unknown type for local '" + stmt.target.text + "'";
        return false;
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
            if (!EmitListIndexGetOp(st, op_suffix)) return false;
          } else {
            (*st.out) << "  array.get." << op_suffix << "\n";
            PopStack(st, 2);
            PushStack(st, 1);
          }
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
            if (!EmitListIndexSetOp(st, op_suffix)) return false;
          } else {
            (*st.out) << "  array.set." << op_suffix << "\n";
            PopStack(st, 3);
          }
          return true;
        }
        if (!EmitExpr(st, stmt.expr, &element_type, error)) return false;
        if (container_type.dims.front().is_list) {
          if (!EmitListIndexSetOp(st, op_suffix)) return false;
        } else {
          (*st.out) << "  array.set." << op_suffix << "\n";
          PopStack(st, 3);
        }
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
      if (stmt.has_loop_var_decl) {
        Stmt var_stmt;
        var_stmt.kind = StmtKind::VarDecl;
        var_stmt.var_decl = stmt.loop_var_decl;
        if (!EmitStmt(st, var_stmt, error)) return false;
      }
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
                  bool is_entry,
                  const std::vector<Stmt>* script_body,
                  std::string* out,
                  std::string* error) {
  const std::vector<Stmt>& stmt_body = script_body ? *script_body : fn.body;
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
  st.local_dl_modules.clear();
  st.next_local = 0;
  st.stack_cur = 0;
  st.stack_max = 0;
  st.saw_return = false;
  st.label_counter = 0;
  st.loop_stack.clear();
  uint16_t locals_count = 0;
  for (const auto& stmt : stmt_body) {
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

  if (!st.global_init_func_name.empty() &&
      is_entry &&
      emit_name != st.global_init_func_name) {
    auto init_it = st.func_ids.find(st.global_init_func_name);
    if (init_it == st.func_ids.end()) {
      if (error) *error = "missing global init function id";
      return false;
    }
    (*st.out) << "  call " << init_it->second << " 0\n";
  }

  if (!st.global_init_func_name.empty() && emit_name == st.global_init_func_name) {
    for (const auto* glob : st.global_decls) {
      if (!glob->has_init_expr) continue;
      if (!EmitExpr(st, glob->init_expr, &glob->type, error)) return false;
      auto git = st.global_indices.find(glob->name);
      if (git == st.global_indices.end()) {
        if (error) *error = "unknown global in init function '" + glob->name + "'";
        return false;
      }
      (*st.out) << "  stglob " << git->second << "\n";
      PopStack(st, 1);
    }
  }

  for (const auto& stmt : stmt_body) {
    if (!EmitStmt(st, stmt, error)) {
      if (error && !error->empty()) {
        *error = "in function '" + display_name + "': " + *error;
      }
      return false;
    }
  }

  if (!st.saw_return) {
    if ((fn.name == "main" || is_entry) && fn.return_type.name == "i32") {
      (*st.out) << "  const.i32 0\n";
      PushStack(st, 1);
    }
    (*st.out) << "  ret\n";
  }

  std::string func_body = func_out.str();
  st.out = nullptr;

  size_t header_end = func_body.find('\n');
  std::string header = func_body.substr(0, header_end);
  std::string body_text = func_body.substr(header_end + 1);
  total_locals = st.next_local;
  size_t enter_end = body_text.find('\n');
  if (enter_end != std::string::npos &&
      body_text.rfind("  enter ", 0) == 0) {
    body_text = "  enter " + std::to_string(total_locals) + body_text.substr(enter_end);
  }

  header = "func " + emit_name +
           " locals=" + std::to_string(total_locals) +
           " stack=" + std::to_string(st.stack_max > 0 ? st.stack_max : 8) +
           " sig=" + emit_name;

  func_out.str(std::string());
  func_out.clear();
  func_out << header << "\n" << body_text << "end\n";
  st.out = nullptr;
  func_body = func_out.str();
  if (out) *out = func_body;
  return true;
}
