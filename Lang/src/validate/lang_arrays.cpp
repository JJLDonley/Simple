bool CheckArrayLiteralShape(const Expr& expr,
                            const std::vector<TypeDim>& dims,
                            size_t dim_index,
                            std::string* error) {
  if (dim_index >= dims.size()) return true;
  const TypeDim& dim = dims[dim_index];
  if (!dim.has_size) return true;

  if (expr.kind == ExprKind::ListLiteral) {
    if (dim.size != 0) {
      if (error) *error = "array literal size does not match fixed dimensions";
      return false;
    }
    return true;
  }

  if (expr.kind != ExprKind::ArrayLiteral) {
    if (error) *error = "array literal size does not match fixed dimensions";
    return false;
  }
  if (expr.children.size() != dim.size) {
    if (error) *error = "array literal size does not match fixed dimensions";
    return false;
  }
  if (dim_index + 1 < dims.size()) {
    for (const auto& child : expr.children) {
      if (!CheckArrayLiteralShape(child, dims, dim_index + 1, error)) return false;
    }
  }
  return true;
}

bool CheckArrayLiteralElementTypes(const Expr& expr,
                                   const ValidateContext& ctx,
                                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                   const ArtifactDecl* current_artifact,
                                   const std::vector<TypeDim>& dims,
                                   size_t dim_index,
                                   const TypeRef& element_type,
                                   std::string* error) {
  if (expr.kind == ExprKind::ListLiteral) return true;
  if (expr.kind != ExprKind::ArrayLiteral) return true;
  if (dims.empty()) return true;

  if (dim_index + 1 >= dims.size()) {
    for (const auto& child : expr.children) {
      TypeRef child_type;
      if (!InferExprType(child, ctx, scopes, current_artifact, &child_type)) {
        if (error && error->empty()) *error = "array literal element type mismatch";
        return false;
      }
      if (!TypesCompatibleForExpr(element_type, child_type, child)) {
        if (error) *error = "array literal element type mismatch";
        return false;
      }
    }
    return true;
  }

  for (const auto& child : expr.children) {
    if (!CheckArrayLiteralElementTypes(child,
                                       ctx,
                                       scopes,
                                       current_artifact,
                                       dims,
                                       dim_index + 1,
                                       element_type,
                                       error)) {
      return false;
    }
  }
  return true;
}
