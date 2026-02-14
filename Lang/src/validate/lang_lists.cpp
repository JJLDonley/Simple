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
