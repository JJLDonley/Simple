bool EmitArrayIndexSetOp(EmitState& st, const char* op_suffix) {
  (*st.out) << "  array.set." << op_suffix << "\n";
  PopStack(st, 3);
  return true;
}

bool EmitArrayIndexGetOp(EmitState& st, const char* op_suffix) {
  (*st.out) << "  array.get." << op_suffix << "\n";
  PopStack(st, 2);
  return PushStack(st, 1);
}

bool EmitArrayLiteral(EmitState& st,
                      const Expr& expr,
                      const TypeRef& element_type,
                      const char* op_suffix,
                      const char* type_name,
                      std::string* error) {
  uint32_t length = static_cast<uint32_t>(expr.children.size());
  (*st.out) << "  newarray " << type_name << " " << length << "\n";
  PushStack(st, 1);
  for (uint32_t i = 0; i < length; ++i) {
    (*st.out) << "  dup\n";
    PushStack(st, 1);
    if (!EmitExpr(st, expr.children[i], &element_type, error)) return false;
    (*st.out) << "  const.i32 " << i << "\n";
    PushStack(st, 1);
    (*st.out) << "  swap\n";
    (*st.out) << "  array.set." << op_suffix << "\n";
    PopStack(st, 3);
  }
  return true;
}

bool EmitArrayLenOp(EmitState& st) {
  (*st.out) << "  array.len\n";
  return true;
}
