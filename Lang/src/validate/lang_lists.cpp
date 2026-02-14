bool CheckListLiteralElementTypes(const Expr& expr,
                                  const ValidateContext& ctx,
                                  const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                  const ArtifactDecl* current_artifact,
                                  const TypeRef& list_type,
                                  std::string* error) {
  if (expr.kind != ExprKind::ListLiteral) return true;
  if (list_type.dims.empty()) return true;
  if (!list_type.dims.front().is_list) return true;

  TypeRef element_type;
  if (!CloneTypeRef(list_type, &element_type)) return false;
  if (!element_type.dims.empty()) {
    element_type.dims.erase(element_type.dims.begin());
  }

  for (const auto& child : expr.children) {
    TypeRef child_type;
    if (!InferExprType(child, ctx, scopes, current_artifact, &child_type)) {
      if (error && error->empty()) *error = "list literal element type mismatch";
      return false;
    }
    if (!TypesCompatibleForExpr(element_type, child_type, child)) {
      if (error) *error = "list literal element type mismatch";
      return false;
    }
  }
  return true;
}

bool GetListCallTargetInfo(const Expr& base,
                           const Expr& callee,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           CallTargetInfo* out) {
  if (!out) return false;
  TypeRef base_type;
  if (!InferExprType(base, ctx, scopes, current_artifact, &base_type) ||
      base_type.dims.empty() || !base_type.dims.front().is_list) {
    return false;
  }

  TypeRef element_type;
  if (!CloneElementType(base_type, &element_type)) return false;
  out->params.clear();
  out->type_params.clear();
  out->is_proc = false;
  out->return_mutability = Mutability::Mutable;
  if (callee.text == "len") {
    out->return_type = MakeSimpleType("i32");
    return true;
  }
  if (callee.text == "push") {
    out->params.push_back(element_type);
    out->return_type = MakeSimpleType("void");
    return true;
  }
  if (callee.text == "pop") {
    out->return_type = element_type;
    return true;
  }
  if (callee.text == "insert") {
    out->params.push_back(MakeSimpleType("i32"));
    out->params.push_back(element_type);
    out->return_type = MakeSimpleType("void");
    return true;
  }
  if (callee.text == "remove") {
    out->params.push_back(MakeSimpleType("i32"));
    out->return_type = element_type;
    return true;
  }
  if (callee.text == "clear") {
    out->return_type = MakeSimpleType("void");
    return true;
  }
  return false;
}
