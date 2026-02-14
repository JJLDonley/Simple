bool EmitListMethodCall(EmitState& st,
                        const Expr& expr,
                        const std::string& member_name,
                        const Expr& base,
                        const TypeRef& list_type,
                        std::string* error) {
  TypeRef element_type;
  if (!CloneElementType(list_type, &element_type)) return false;
  auto emit_list_value = [&](const Expr& expr_value,
                             const TypeRef& type) -> bool {
    return EmitExpr(st, expr_value, &type, error);
  };

  if (member_name == "len") {
    if (expr.args.size() != 0) {
      if (error) *error = "call argument count mismatch for 'list.len'";
      return false;
    }
    if (!emit_list_value(base, list_type)) return false;
    (*st.out) << "  list.len\n";
    PopStack(st, 1);
    PushStack(st, 1);
    return true;
  }
  if (member_name == "push") {
    if (expr.args.size() != 1) {
      if (error) *error = "call argument count mismatch for 'list.push'";
      return false;
    }
    const char* op_suffix = VmOpSuffixForType(element_type);
    if (!op_suffix) {
      if (error) *error = "unsupported list element type for list.push";
      return false;
    }
    if (!emit_list_value(base, list_type)) return false;
    if (!emit_list_value(expr.args[0], element_type)) return false;
    (*st.out) << "  list.push." << op_suffix << "\n";
    PopStack(st, 2);
    return true;
  }
  if (member_name == "pop") {
    if (expr.args.size() != 0) {
      if (error) *error = "call argument count mismatch for 'list.pop'";
      return false;
    }
    const char* op_suffix = VmOpSuffixForType(element_type);
    if (!op_suffix) {
      if (error) *error = "unsupported list element type for list.pop";
      return false;
    }
    if (!emit_list_value(base, list_type)) return false;
    (*st.out) << "  list.pop." << op_suffix << "\n";
    PopStack(st, 1);
    PushStack(st, 1);
    return true;
  }
  if (member_name == "insert") {
    if (expr.args.size() != 2) {
      if (error) *error = "call argument count mismatch for 'list.insert'";
      return false;
    }
    const char* op_suffix = VmOpSuffixForType(element_type);
    if (!op_suffix) {
      if (error) *error = "unsupported list element type for list.insert";
      return false;
    }
    TypeRef index_type = MakeTypeRef("i32");
    if (!emit_list_value(base, list_type)) return false;
    if (!emit_list_value(expr.args[0], index_type)) return false;
    if (!emit_list_value(expr.args[1], element_type)) return false;
    (*st.out) << "  list.insert." << op_suffix << "\n";
    PopStack(st, 3);
    return true;
  }
  if (member_name == "remove") {
    if (expr.args.size() != 1) {
      if (error) *error = "call argument count mismatch for 'list.remove'";
      return false;
    }
    const char* op_suffix = VmOpSuffixForType(element_type);
    if (!op_suffix) {
      if (error) *error = "unsupported list element type for list.remove";
      return false;
    }
    TypeRef index_type = MakeTypeRef("i32");
    if (!emit_list_value(base, list_type)) return false;
    if (!emit_list_value(expr.args[0], index_type)) return false;
    (*st.out) << "  list.remove." << op_suffix << "\n";
    PopStack(st, 2);
    PushStack(st, 1);
    return true;
  }
  if (member_name == "clear") {
    if (expr.args.size() != 0) {
      if (error) *error = "call argument count mismatch for 'list.clear'";
      return false;
    }
    if (!emit_list_value(base, list_type)) return false;
    (*st.out) << "  list.clear\n";
    PopStack(st, 1);
    return true;
  }
  return false;
}
