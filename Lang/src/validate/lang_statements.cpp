bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               const std::unordered_set<std::string>& type_params,
               const TypeRef* expected_return,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error) {
  switch (stmt.kind) {
    case StmtKind::Return:
      if (return_is_void && stmt.has_return_expr) {
        if (error) *error = "void function cannot return a value";
        return false;
      }
      if (!return_is_void && !stmt.has_return_expr) {
        if (error) *error = "non-void function must return a value";
        return false;
      }
      if (stmt.has_return_expr) {
        if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
        if (expected_return) {
          TypeRef actual;
          if (InferExprType(stmt.expr, ctx, scopes, current_artifact, &actual)) {
            if (!TypesCompatibleForExpr(*expected_return, actual, stmt.expr)) {
              if (error) *error = "return type mismatch";
              return false;
            }
          }
        }
        return true;
      }
      return true;
    case StmtKind::Expr:
      return CheckExpr(stmt.expr, ctx, scopes, current_artifact, error);
    case StmtKind::Assign:
      if (!CheckExpr(stmt.target, ctx, scopes, current_artifact, error)) return false;
      if (!CheckAssignmentTarget(stmt.target, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
      {
        TypeRef target_type;
        TypeRef value_type;
        bool have_target = InferExprType(stmt.target, ctx, scopes, current_artifact, &target_type);
        bool have_value = InferExprType(stmt.expr, ctx, scopes, current_artifact, &value_type);
        if (have_target && stmt.expr.kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(stmt.expr, target_type, error)) return false;
        }
        if (have_target && have_value && !TypesCompatibleForExpr(target_type, value_type, stmt.expr)) {
          if (error) *error = "assignment type mismatch";
          return false;
        }
        if (have_target && have_value && stmt.assign_op != "=") {
          std::string op = stmt.assign_op;
          if (!op.empty() && op.back() == '=') op.pop_back();
          TypeRef rhs_for_op;
          if (!CloneTypeRef(value_type, &rhs_for_op)) return false;
          if (!TypeEquals(target_type, rhs_for_op) &&
              IsLiteralCompatibleWithScalarType(stmt.expr, target_type)) {
            if (!CloneTypeRef(target_type, &rhs_for_op)) return false;
          }
          if (!CheckCompoundAssignOp(op, target_type, rhs_for_op, error)) return false;
        }
        if (have_target &&
            (stmt.expr.kind == ExprKind::ArrayLiteral || stmt.expr.kind == ExprKind::ListLiteral) &&
            !target_type.dims.empty()) {
          if (!CheckArrayLiteralShape(stmt.expr, target_type.dims, 0, error)) return false;
          TypeRef base_type;
          if (!CloneTypeRef(target_type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(stmt.expr,
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             target_type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(stmt.expr,
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            target_type,
                                            error)) {
            return false;
          }
        } else if (have_target &&
                   (stmt.expr.kind == ExprKind::ArrayLiteral || stmt.expr.kind == ExprKind::ListLiteral)) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
      }
      return true;
    case StmtKind::VarDecl:
      if (!CheckTypeRef(stmt.var_decl.type, ctx, type_params, TypeUse::Value, error)) return false;
      if (IsCallbackType(stmt.var_decl.type)) {
        if (error) *error = "callback is only valid as a parameter type";
        return false;
      }
      {
        LocalInfo info;
        info.mutability = stmt.var_decl.mutability;
        info.type = &stmt.var_decl.type;
        if (!AddLocal(scopes, stmt.var_decl.name, info, error)) return false;
      }
      if (stmt.var_decl.has_init_expr) {
        if (!CheckExpr(stmt.var_decl.init_expr, ctx, scopes, current_artifact, error)) return false;
        if (stmt.var_decl.init_expr.kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(stmt.var_decl.init_expr, stmt.var_decl.type, error)) {
            return false;
          }
        }
        if ((stmt.var_decl.init_expr.kind == ExprKind::ArrayLiteral ||
             stmt.var_decl.init_expr.kind == ExprKind::ListLiteral) &&
            !stmt.var_decl.type.dims.empty()) {
          if (!CheckArrayLiteralShape(stmt.var_decl.init_expr, stmt.var_decl.type.dims, 0, error)) {
            return false;
          }
          TypeRef base_type;
          if (!CloneTypeRef(stmt.var_decl.type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(stmt.var_decl.init_expr,
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             stmt.var_decl.type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(stmt.var_decl.init_expr,
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            stmt.var_decl.type,
                                            error)) {
            return false;
          }
        } else if (stmt.var_decl.init_expr.kind == ExprKind::ArrayLiteral ||
                   stmt.var_decl.init_expr.kind == ExprKind::ListLiteral) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
        TypeRef init_type;
        if (InferExprType(stmt.var_decl.init_expr, ctx, scopes, current_artifact, &init_type)) {
          if (!TypesCompatibleForExpr(stmt.var_decl.type, init_type, stmt.var_decl.init_expr)) {
            if (error) *error = "initializer type mismatch";
            return false;
          }
        }
        if (stmt.var_decl.init_expr.kind == ExprKind::ArtifactLiteral) {
          auto artifact_it = ctx.artifacts.find(stmt.var_decl.type.name);
          if (artifact_it != ctx.artifacts.end()) {
            std::unordered_map<std::string, TypeRef> mapping;
            if (!BuildArtifactTypeParamMap(stmt.var_decl.type,
                                           artifact_it->second,
                                           &mapping,
                                           error)) {
              return false;
            }
            if (!ValidateArtifactLiteral(stmt.var_decl.init_expr,
                                         artifact_it->second,
                                         mapping,
                                         ctx,
                                         scopes,
                                         current_artifact,
                                         error)) {
              return false;
            }
          }
        }
        std::string manifest_module;
        if (GetDlOpenManifestModule(stmt.var_decl.init_expr, ctx, &manifest_module)) {
          auto local_it = scopes.back().find(stmt.var_decl.name);
          if (local_it != scopes.back().end()) {
            local_it->second.dl_module = manifest_module;
          }
        }
        return true;
      }
      return true;
    case StmtKind::IfChain:
      for (const auto& branch : stmt.if_branches) {
        if (!CheckExpr(branch.first, ctx, scopes, current_artifact, error)) return false;
        if (!CheckBoolCondition(branch.first, ctx, scopes, current_artifact, error)) return false;
        scopes.emplace_back();
        for (const auto& child : branch.second) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      if (!stmt.else_branch.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.else_branch) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::IfStmt:
      if (!CheckExpr(stmt.if_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.if_cond, ctx, scopes, current_artifact, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.if_then) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      if (!stmt.if_else.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.if_else) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::WhileLoop:
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth + 1,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      return true;
    case StmtKind::ForLoop: {
      scopes.emplace_back();
      if (stmt.has_loop_var_decl) {
        Stmt var_stmt;
        var_stmt.kind = StmtKind::VarDecl;
        var_stmt.var_decl = stmt.loop_var_decl;
        if (!CheckStmt(var_stmt,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      if (!CheckExpr(stmt.loop_iter, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_step, ctx, scopes, current_artifact, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth + 1,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      scopes.pop_back();
      return true;
    }
    case StmtKind::Break:
      if (loop_depth == 0) {
        if (error) *error = "break used outside of loop";
        return false;
      }
      return true;
    case StmtKind::Skip:
      if (loop_depth == 0) {
        if (error) *error = "skip used outside of loop";
        return false;
      }
      return true;
  }
  return true;
}

bool IsIntegerTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "i128" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "u128" ||
         name == "char";
}

bool IsFloatTypeName(const std::string& name) {
  return name == "f32" || name == "f64";
}

bool IsBoolTypeName(const std::string& name) {
  return name == "bool";
}

bool IsStringTypeName(const std::string& name) {
  return name == "string";
}

bool IsNumericTypeName(const std::string& name) {
  return IsIntegerTypeName(name) || IsFloatTypeName(name);
}

bool IsScalarType(const TypeRef& type) {
  return type.pointer_depth == 0 &&
         !type.is_proc &&
         type.dims.empty() &&
         type.type_args.empty();
}

bool RequireScalar(const TypeRef& type, const std::string& op, std::string* error) {
  if (!IsScalarType(type)) {
    if (error) *error = "operator '" + op + "' requires scalar operands";
    return false;
  }
  return true;
}

bool CheckUnaryOpTypes(const Expr& expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  TypeRef operand;
  if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &operand)) return true;
  if (!RequireScalar(operand, expr.op, error)) return false;

  const std::string op = expr.op.rfind("post", 0) == 0 ? expr.op.substr(4) : expr.op;
  if (op == "!") {
    if (!IsBoolTypeName(operand.name)) {
      if (error) *error = "operator '!' requires bool operand";
      return false;
    }
    return true;
  }
  if (op == "++" || op == "--" || op == "-") {
    if (!IsNumericTypeName(operand.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operand";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckBinaryOpTypes(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error) {
  TypeRef lhs;
  TypeRef rhs;
  if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &lhs)) return true;
  if (!InferExprType(expr.children[1], ctx, scopes, current_artifact, &rhs)) return true;

  if (!RequireScalar(lhs, expr.op, error)) return false;
  if (!RequireScalar(rhs, expr.op, error)) return false;
  if (!TypeEquals(lhs, rhs)) {
    if (!IsLiteralCompatibleWithScalarType(expr.children[0], rhs) &&
        !IsLiteralCompatibleWithScalarType(expr.children[1], lhs)) {
      if (error) *error = "operator '" + expr.op + "' requires matching operand types";
      return false;
    }
  }

  const std::string& op = expr.op;
  if (op == "&&" || op == "||") {
    if (!IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires bool operands";
      return false;
    }
    return true;
  }

  if (op == "==" || op == "!=") {
    if (IsStringTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' does not support string operands";
      return false;
    }
    if (!IsNumericTypeName(lhs.name) && !IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric or bool operands";
      return false;
    }
    return true;
  }

  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }

  if (op == "+" || op == "-" || op == "*" || op == "/") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }

  if (op == "%") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '%' requires integer operands";
      return false;
    }
    return true;
  }

  if (op == "<<" || op == ">>" || op == "&" || op == "|" || op == "^") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires integer operands";
      return false;
    }
    return true;
  }

  return true;
}

bool CheckCompoundAssignOp(const std::string& op,
                           const TypeRef& lhs,
                           const TypeRef& rhs,
                           std::string* error) {
  if (!RequireScalar(lhs, op, error)) return false;
  if (!RequireScalar(rhs, op, error)) return false;
  if (!TypeEquals(lhs, rhs)) {
    if (error) *error = "assignment type mismatch";
    return false;
  }
  if (op == "&&" || op == "||") {
    if (!IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires bool operands";
      return false;
    }
    return true;
  }
  if (op == "==" || op == "!=") {
    if (IsStringTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' does not support string operands";
      return false;
    }
    if (!IsNumericTypeName(lhs.name) && !IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric or bool operands";
      return false;
    }
    return true;
  }
  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }
  if (op == "+" || op == "-" || op == "*" || op == "/") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }
  if (op == "%") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '%' requires integer operands";
      return false;
    }
    return true;
  }
  if (op == "<<" || op == ">>" || op == "&" || op == "|" || op == "^") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires integer operands";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckFnLiteralAgainstType(const Expr& fn_expr,
                               const TypeRef& target_type,
                               std::string* error) {
  if (!target_type.is_proc) {
    if (error) *error = "fn literal requires procedure type";
    return false;
  }
  if (target_type.proc_is_callback) return true;
  if (fn_expr.fn_params.size() != target_type.proc_params.size()) {
    if (error) {
      *error = "fn literal parameter count mismatch: expected " +
               std::to_string(target_type.proc_params.size()) + ", got " +
               std::to_string(fn_expr.fn_params.size());
    }
    return false;
  }
  for (size_t i = 0; i < fn_expr.fn_params.size(); ++i) {
    if (!TypeEquals(fn_expr.fn_params[i].type, target_type.proc_params[i])) {
      if (error) *error = "fn literal parameter type mismatch";
      return false;
    }
  }
  return true;
}

bool IsCallbackType(const TypeRef& type) {
  return type.is_proc && type.proc_is_callback;
}

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error) {
  switch (expr.kind) {
    case ExprKind::Identifier:
      if (expr.text == "self") {
        if (!current_artifact) {
          if (error) *error = "self used outside of artifact method";
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        return true;
      }
      if (expr.text == "Core") {
        if (IsReservedModuleEnabled(ctx, "Core.Math") || IsReservedModuleEnabled(ctx, "Core.IO") ||
            IsReservedModuleEnabled(ctx, "Core.Time") || IsReservedModuleEnabled(ctx, "Core.DL") ||
            IsReservedModuleEnabled(ctx, "Core.OS") || IsReservedModuleEnabled(ctx, "Core.FS") ||
            IsReservedModuleEnabled(ctx, "Core.Log")) {
          return true;
        }
      }
      if (current_artifact && IsArtifactMemberName(current_artifact, expr.text)) {
        if (error) *error = "artifact members must be accessed via self: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (expr.text == "len" || expr.text == "str" ||
          IsPrimitiveCastName(expr.text) || GetAtCastTargetName(expr.text, nullptr)) {
        return true;
      }
      if (FindLocal(scopes, expr.text)) return true;
      if (ctx.top_level.find(expr.text) != ctx.top_level.end()) {
        if (ctx.modules.find(expr.text) != ctx.modules.end()) {
          if (error) *error = "module is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        if (ctx.artifacts.find(expr.text) != ctx.artifacts.end()) {
          if (error) *error = "type is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        if (ctx.enum_types.find(expr.text) != ctx.enum_types.end()) {
          if (error) *error = "enum type is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        return true;
      }
      if (IsReservedModuleEnabled(ctx, expr.text)) {
        if (error) *error = "module is not a value: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (ctx.externs_by_module.find(expr.text) != ctx.externs_by_module.end()) {
        return true;
      }
      if (ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
        if (error) *error = "unqualified enum value: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (error) *error = "undeclared identifier: " + expr.text;
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    case ExprKind::Literal:
      return true;
    case ExprKind::Unary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (expr.op == "++" || expr.op == "--" || expr.op == "post++" || expr.op == "post--") {
        if (!CheckAssignmentTarget(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      }
      return CheckUnaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Binary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) {
        if (!CheckAssignmentTarget(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckExpr(expr.children[1], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) {
        TypeRef target_type;
        TypeRef value_type;
        bool have_target = InferExprType(expr.children[0], ctx, scopes, current_artifact, &target_type);
        bool have_value = InferExprType(expr.children[1], ctx, scopes, current_artifact, &value_type);
        if (expr.op != "=" && have_target && have_value) {
          TypeRef rhs_for_op;
          if (!CloneTypeRef(value_type, &rhs_for_op)) return false;
          if (!TypeEquals(target_type, rhs_for_op) &&
              IsLiteralCompatibleWithScalarType(expr.children[1], target_type)) {
            if (!CloneTypeRef(target_type, &rhs_for_op)) return false;
          }
          if (!CheckCompoundAssignOp(expr.op, target_type, rhs_for_op, error)) return false;
          return true;
        }
        if (have_target && expr.children[1].kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(expr.children[1], target_type, error)) return false;
        }
        if (have_target &&
            (expr.children[1].kind == ExprKind::ArrayLiteral ||
             expr.children[1].kind == ExprKind::ListLiteral) &&
            !target_type.dims.empty()) {
          if (!CheckArrayLiteralShape(expr.children[1], target_type.dims, 0, error)) {
            return false;
          }
          TypeRef base_type;
          if (!CloneTypeRef(target_type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(expr.children[1],
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             target_type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(expr.children[1],
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            target_type,
                                            error)) {
            return false;
          }
        } else if (have_target &&
                   (expr.children[1].kind == ExprKind::ArrayLiteral ||
                    expr.children[1].kind == ExprKind::ListLiteral)) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
        if (have_target && have_value &&
            !TypesCompatibleForExpr(target_type, value_type, expr.children[1])) {
          if (error) *error = "assignment type mismatch";
          return false;
        }
        return true;
      }
      return CheckBinaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Call:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      for (const auto& arg : expr.args) {
        if (!CheckExpr(arg, ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckCallTarget(expr.children[0], expr.args.size(), ctx, scopes, current_artifact, error)) return false;
      if (IsIoPrintCallExpr(expr.children[0], ctx)) {
        if (expr.args.empty()) {
          if (error) *error = "call argument count mismatch for IO." + expr.children[0].text;
          return false;
        }
        if (expr.args.size() == 1) {
          TypeRef arg_type;
          if (!InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
            if (error && error->empty()) *error = "IO.print expects scalar argument";
            return false;
          }
          if (arg_type.pointer_depth != 0 ||
              arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
            if (error) *error = "IO.print expects scalar argument";
            return false;
          }
          if (!(IsNumericTypeName(arg_type.name) ||
                IsBoolTypeName(arg_type.name) ||
                arg_type.name == "char" ||
                arg_type.name == "string")) {
            if (error) *error = "IO.print supports numeric, bool, char, or string";
            return false;
          }
        } else {
          if (!(expr.args[0].kind == ExprKind::Literal &&
                expr.args[0].literal_kind == LiteralKind::String)) {
            if (error) *error = "IO.print format call expects string literal as first argument";
            return false;
          }
          size_t placeholder_count = 0;
          if (!CountFormatPlaceholders(expr.args[0].text, &placeholder_count, error)) {
            return false;
          }
          const size_t value_count = expr.args.size() - 1;
          if (placeholder_count != value_count) {
            if (error) {
              *error = "IO.print format placeholder count mismatch: expected " +
                       std::to_string(placeholder_count) + ", got " +
                       std::to_string(value_count);
            }
            return false;
          }
          for (size_t i = 1; i < expr.args.size(); ++i) {
            TypeRef arg_type;
            if (!InferExprType(expr.args[i], ctx, scopes, current_artifact, &arg_type)) {
              if (error && error->empty()) *error = "IO.print format expects scalar arguments";
              return false;
            }
            if (arg_type.pointer_depth != 0 ||
                arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
              if (error) *error = "IO.print format expects scalar arguments";
              return false;
            }
            if (!(IsNumericTypeName(arg_type.name) ||
                  IsBoolTypeName(arg_type.name) ||
                  arg_type.name == "char" ||
                  arg_type.name == "string")) {
              if (error) *error = "IO.print supports numeric, bool, char, or string";
              return false;
            }
          }
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier && expr.children[0].text == "len") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for len: expected 1, got " +
                              std::to_string(expr.args.size());
          return false;
        }
        TypeRef arg_type;
        if (InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (arg_type.dims.empty() && arg_type.name != "string") {
            if (error) *error = "len expects array, list, or string argument";
            return false;
          }
        } else {
          if (error && error->empty()) *error = "len expects array, list, or string argument";
          return false;
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier && expr.children[0].text == "str") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for str: expected 1, got " +
                              std::to_string(expr.args.size());
          return false;
        }
        TypeRef arg_type;
        if (InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (arg_type.pointer_depth != 0 ||
              (!IsNumericTypeName(arg_type.name) && !IsBoolTypeName(arg_type.name))) {
            if (error) *error = "str expects numeric or bool argument";
            return false;
          }
        } else {
          if (error && error->empty()) *error = "str expects numeric or bool argument";
          return false;
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier) {
        std::string cast_target;
        const bool is_at_cast = GetAtCastTargetName(expr.children[0].text, &cast_target);
        if (!is_at_cast && IsPrimitiveCastName(expr.children[0].text)) {
          if (error) {
            *error = "primitive cast syntax requires '@': use @" +
                     expr.children[0].text + "(value)";
          }
          return false;
        }
        if (is_at_cast) {
          if (expr.args.size() != 1) {
            if (error) *error = "call argument count mismatch for " + cast_target + ": expected 1, got " +
                                std::to_string(expr.args.size());
            return false;
          }
          TypeRef arg_type;
          if (!InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
            if (error && error->empty()) *error = cast_target + " cast expects scalar argument";
            return false;
          }
          if (arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
            if (error) *error = cast_target + " cast expects scalar argument";
            return false;
          }
          if (IsStringTypeName(arg_type.name) && !(cast_target == "i32" || cast_target == "f64")) {
            if (error) *error = cast_target + " cast from string is unsupported";
            return false;
          }
        }
      }
      if (!IsIoPrintCallExpr(expr.children[0], ctx) &&
          !(expr.children[0].kind == ExprKind::Identifier &&
            (expr.children[0].text == "len" || expr.children[0].text == "str" ||
             GetAtCastTargetName(expr.children[0].text, nullptr)))) {
        if (!CheckCallArgTypes(expr, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::Member:
      if (expr.op == "." && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            IsIoPrintCallExpr(expr, ctx)) {
          return true;
        }
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end()) {
          auto members_it = ctx.enum_members_by_type.find(base.text);
          if (members_it != ctx.enum_members_by_type.end()) {
            if (members_it->second.find(expr.text) == members_it->second.end()) {
              if (error) *error = "unknown enum member: " + base.text + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
          }
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
            auto mod_it = ctx.externs_by_module.find(dl_module);
            if (mod_it != ctx.externs_by_module.end() &&
                mod_it->second.find(expr.text) != mod_it->second.end()) {
              return true;
            }
          }
          auto module_it = ctx.modules.find(base.text);
          if (module_it != ctx.modules.end()) {
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text)) {
              if (error) {
                *error = UnknownMemberErrorWithSuggestion(
                    base.text, expr.text, ModuleMembers(module_it->second));
              }
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
            return true;
          }
          std::string module_name;
          if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
            TypeRef var_type;
            CallTargetInfo info;
            if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
                GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
              return true;
            }
            if (error) {
              std::string resolved;
              ResolveReservedModuleName(ctx, module_name, &resolved);
              *error = UnknownMemberErrorWithSuggestion(
                  module_name, expr.text, ReservedModuleMembers(resolved.empty() ? module_name : resolved));
            }
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
        }
      }
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (expr.op == "." && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
            auto mod_it = ctx.externs_by_module.find(dl_module);
            if (mod_it != ctx.externs_by_module.end() &&
                mod_it->second.find(expr.text) != mod_it->second.end()) {
              return true;
            }
          }
          auto module_it = ctx.modules.find(base.text);
          if (module_it != ctx.modules.end()) {
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text)) {
              if (error) {
                *error = UnknownMemberErrorWithSuggestion(
                    base.text, expr.text, ModuleMembers(module_it->second));
              }
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
            return true;
          }
          std::string module_name;
          if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
            TypeRef var_type;
            CallTargetInfo info;
            if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
                GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
              return true;
            }
            if (error) {
              std::string resolved;
              ResolveReservedModuleName(ctx, module_name, &resolved);
              *error = UnknownMemberErrorWithSuggestion(
                  module_name, expr.text, ReservedModuleMembers(resolved.empty() ? module_name : resolved));
            }
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
        }
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
          TypeRef var_type;
          CallTargetInfo info;
          if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
              GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
            return true;
          }
          if (error) {
            std::string resolved;
            ResolveReservedModuleName(ctx, module_name, &resolved);
            *error = UnknownMemberErrorWithSuggestion(
                module_name, expr.text, ReservedModuleMembers(resolved.empty() ? module_name : resolved));
          }
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        TypeRef base_type;
        if (InferExprType(base, ctx, scopes, current_artifact, &base_type)) {
          auto artifact_it = ctx.artifacts.find(base_type.name);
          if (artifact_it != ctx.artifacts.end()) {
            const ArtifactDecl* artifact = artifact_it->second;
            if (!FindArtifactField(artifact, expr.text) &&
                !FindArtifactMethod(artifact, expr.text)) {
              if (error) *error = "unknown artifact member: " + base_type.name + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
          }
        }
      }
      if (expr.op == "::" && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end() &&
            ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
          if (error) *error = "enum members must be qualified with '.': " + base.text + "." + expr.text;
          return false;
        }
      }
      return true;
    case ExprKind::Index:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(expr.children[1], ctx, scopes, current_artifact, error)) return false;
      {
        TypeRef base_type;
        if (InferExprType(expr.children[0], ctx, scopes, current_artifact, &base_type)) {
          if (base_type.dims.empty()) {
            if (error) *error = "indexing is only valid on arrays and lists";
            return false;
          }
        } else if (expr.children[0].kind == ExprKind::Literal) {
          if (error) *error = "indexing is only valid on arrays and lists";
          return false;
        }
      }
      if (expr.children[1].kind == ExprKind::Literal) {
        switch (expr.children[1].literal_kind) {
          case LiteralKind::Integer:
          case LiteralKind::Char:
            break;
          default:
            if (error) *error = "index must be an integer";
            return false;
        }
      } else {
        TypeRef index_type;
        if (InferExprType(expr.children[1], ctx, scopes, current_artifact, &index_type)) {
          if (!IsIntegerTypeName(index_type.name) && index_type.name != "char") {
            if (error) *error = "index must be an integer";
            return false;
          }
        }
      }
      return true;
    case ExprKind::ArrayLiteral:
    case ExprKind::ListLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::ArtifactLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, current_artifact, error)) return false;
      }
      for (const auto& field_value : expr.field_values) {
        if (!CheckExpr(field_value, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::FnLiteral:
      return true;
  }
  return true;
}

bool CheckFunctionBody(const FuncDecl& fn,
                       const ValidateContext& ctx,
                       const std::unordered_set<std::string>& type_params,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  std::vector<std::unordered_map<std::string, LocalInfo>> scopes;
  scopes.emplace_back();
  std::unordered_set<std::string> param_names;
  const bool return_is_void = fn.return_type.name == "void";
  const bool is_main = (fn.name == "main" && fn.return_type.name == "i32");
  if (IsCallbackType(fn.return_type)) {
    if (error) *error = "callback is only valid as a parameter type";
    return false;
  }
  if (!CheckTypeRef(fn.return_type, ctx, type_params, TypeUse::Return, error)) return false;
  for (const auto& param : fn.params) {
    if (!param_names.insert(param.name).second) {
      if (error) *error = "duplicate parameter name: " + param.name;
      return false;
    }
    if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
    LocalInfo info;
    info.mutability = param.mutability;
    info.type = &param.type;
    if (!AddLocal(scopes, param.name, info, error)) return false;
  }
  for (const auto& stmt : fn.body) {
    if (!CheckStmt(stmt,
                   ctx,
                   type_params,
                   &fn.return_type,
                   return_is_void,
                   0,
                   scopes,
                   current_artifact,
                   error)) {
      return false;
    }
  }
  if (!return_is_void && !StmtsReturn(fn.body) && !is_main) {
    if (error) *error = "non-void function does not return on all paths";
    return false;
  }
  return true;
}

} // namespace
