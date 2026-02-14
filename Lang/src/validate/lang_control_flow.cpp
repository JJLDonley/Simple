bool CheckBoolCondition(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error) {
  TypeRef cond_type;
  if (InferExprType(expr, ctx, scopes, current_artifact, &cond_type)) {
    if (cond_type.pointer_depth != 0 || !IsBoolTypeName(cond_type.name)) {
      if (error) *error = "condition must be bool";
      return false;
    }
  }
  return true;
}

bool StmtReturns(const Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::Return:
      return true;
    case StmtKind::IfChain:
      if (stmt.if_branches.empty() || stmt.else_branch.empty()) return false;
      for (const auto& branch : stmt.if_branches) {
        if (!StmtsReturn(branch.second)) return false;
      }
      return StmtsReturn(stmt.else_branch);
    case StmtKind::IfStmt:
      if (stmt.if_then.empty() || stmt.if_else.empty()) return false;
      return StmtsReturn(stmt.if_then) && StmtsReturn(stmt.if_else);
    default:
      return false;
  }
}

bool StmtsReturn(const std::vector<Stmt>& stmts) {
  for (const auto& stmt : stmts) {
    if (StmtReturns(stmt)) return true;
  }
  return false;
}
