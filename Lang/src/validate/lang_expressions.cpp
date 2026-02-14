bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out) {
  if (!out) return false;
  switch (expr.kind) {
    case ExprKind::Literal:
      out->is_proc = false;
      out->proc_is_callback = false;
      out->type_args.clear();
      out->dims.clear();
      switch (expr.literal_kind) {
        case LiteralKind::Integer: out->name = "i32"; break;
        case LiteralKind::Float: out->name = "f64"; break;
        case LiteralKind::String: out->name = "string"; break;
        case LiteralKind::Char: out->name = "char"; break;
        case LiteralKind::Bool: out->name = "bool"; break;
      }
      return true;
    case ExprKind::Identifier: {
      if (expr.text == "self") return false;
      if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
        if (!local->type) return false;
        return CloneTypeRef(*local->type, out);
      }
      auto global_it = ctx.globals.find(expr.text);
      if (global_it != ctx.globals.end()) {
        return CloneTypeRef(global_it->second->type, out);
      }
      return false;
    }
    case ExprKind::Member: {
      if (expr.op != "." || expr.children.empty()) return false;
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier && base.text == "Core") {
        return true;
      }
      if (base.kind == ExprKind::Identifier) {
        if (base.text == "self") {
          const VarDecl* field = FindArtifactField(current_artifact, expr.text);
          if (field) return CloneTypeRef(field->type, out);
          const FuncDecl* method = FindArtifactMethod(current_artifact, expr.text);
          if (method) return CloneTypeRef(method->return_type, out);
          return false;
        }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        if (const VarDecl* var = FindModuleVar(module_it->second, expr.text)) {
          return CloneTypeRef(var->type, out);
        }
        if (const FuncDecl* fn = FindModuleFunc(module_it->second, expr.text)) {
          return CloneTypeRef(fn->return_type, out);
        }
        return false;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          if (GetReservedModuleVarType(ctx, module_name, expr.text, out)) {
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(expr.text);
          if (ext_it != ext_mod_it->second.end()) {
            return CloneTypeRef(ext_it->second->return_type, out);
          }
          return false;
        }
      }
        if (const LocalInfo* local = FindLocal(scopes, base.text)) {
          if (!local->type) return false;
          auto artifact_it = ctx.artifacts.find(local->type->name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, nullptr)) return false;
          }
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(field->type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(method->return_type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
        }
        auto global_it = ctx.globals.find(base.text);
        if (global_it != ctx.globals.end()) {
          auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, nullptr)) return false;
          }
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(field->type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(method->return_type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
        }
      }
      return false;
    }
    case ExprKind::Call: {
      if (expr.children.empty()) return false;
      const Expr& callee = expr.children[0];
      if (callee.kind == ExprKind::Identifier) {
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
        if (callee.text == "str") {
          out->name = "string";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_is_callback = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        std::string cast_target;
        if (GetAtCastTargetName(callee.text, &cast_target)) {
          out->name = cast_target;
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_is_callback = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
      }
      CallTargetInfo info;
      if (!GetCallTargetInfo(callee, ctx, scopes, current_artifact, &info, nullptr)) return false;
      if (info.type_params.empty()) {
        return CloneTypeRef(info.return_type, out);
      }
      std::unordered_map<std::string, TypeRef> mapping;
      if (!expr.type_args.empty()) {
        if (expr.type_args.size() != info.type_params.size()) return false;
        for (size_t i = 0; i < info.type_params.size(); ++i) {
          TypeRef copy;
          if (!CloneTypeRef(expr.type_args[i], &copy)) return false;
          mapping[info.type_params[i]] = std::move(copy);
        }
      } else {
        std::unordered_set<std::string> type_param_set(info.type_params.begin(),
                                                       info.type_params.end());
        if (!InferTypeArgsFromCall(info.params, expr.args, type_param_set,
                                   ctx, scopes, current_artifact, &mapping)) {
          return false;
        }
      }
      TypeRef resolved;
      if (!SubstituteTypeParams(info.return_type, mapping, &resolved)) return false;
      return CloneTypeRef(resolved, out);
    }
    case ExprKind::Index: {
      TypeRef base_type;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &base_type)) return false;
      if (base_type.dims.empty()) return false;
      TypeRef result;
      if (!CloneTypeRef(base_type, &result)) return false;
      result.dims.erase(result.dims.begin());
      result.is_proc = false;
      result.proc_is_callback = false;
      result.proc_params.clear();
      result.proc_return.reset();
      return CloneTypeRef(result, out);
    }
    case ExprKind::Unary: {
      if (expr.children.empty()) return false;
      TypeRef operand;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &operand)) return false;
      if (!IsScalarType(operand)) return false;
      const std::string op = expr.op.rfind("post", 0) == 0 ? expr.op.substr(4) : expr.op;
      if (op == "!") {
        if (!IsBoolTypeName(operand.name)) return false;
        out->name = "bool";
        out->pointer_depth = 0;
        out->type_args.clear();
        out->dims.clear();
        out->is_proc = false;
        out->proc_is_callback = false;
        out->proc_params.clear();
        out->proc_return.reset();
        return true;
      }
      if (op == "++" || op == "--" || op == "-") {
        if (!IsNumericTypeName(operand.name)) return false;
        return CloneTypeRef(operand, out);
      }
      return false;
    }
    case ExprKind::Binary: {
      if (expr.children.size() < 2) return false;
      TypeRef lhs;
      TypeRef rhs;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &lhs)) return false;
      if (!InferExprType(expr.children[1], ctx, scopes, current_artifact, &rhs)) return false;
      if (!IsScalarType(lhs) || !IsScalarType(rhs)) return false;

      TypeRef common;
      if (TypeEquals(lhs, rhs)) {
        if (!CloneTypeRef(lhs, &common)) return false;
      } else {
        if (IsLiteralCompatibleWithScalarType(expr.children[0], rhs)) {
          if (!CloneTypeRef(rhs, &common)) return false;
        } else if (IsLiteralCompatibleWithScalarType(expr.children[1], lhs)) {
          if (!CloneTypeRef(lhs, &common)) return false;
        } else {
          return false;
        }
      }

      const std::string& op = expr.op;
      if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
          op == "&&" || op == "||") {
        out->name = "bool";
        out->pointer_depth = 0;
        out->type_args.clear();
        out->dims.clear();
        out->is_proc = false;
        out->proc_is_callback = false;
        out->proc_params.clear();
        out->proc_return.reset();
        return true;
      }

      if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
          op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" ||
          op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
          op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        return CloneTypeRef(common, out);
      }
      return false;
    }
    default:
      return false;
  }
}

bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               const std::unordered_set<std::string>& type_params,
               const TypeRef* expected_return,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error);

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error);

bool CheckArrayLiteralShape(const Expr& expr,
                            const std::vector<TypeDim>& dims,
                            size_t dim_index,
                            std::string* error);

bool CheckArrayLiteralElementTypes(const Expr& expr,
                                   const ValidateContext& ctx,
                                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                   const ArtifactDecl* current_artifact,
                                   const std::vector<TypeDim>& dims,
                                   size_t dim_index,
                                   const TypeRef& element_type,
                                   std::string* error);

bool CheckListLiteralElementTypes(const Expr& expr,
                                  const ValidateContext& ctx,
                                  const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                  const ArtifactDecl* current_artifact,
                                  const TypeRef& list_type,
                                  std::string* error);

bool GetListCallTargetInfo(const Expr& base,
                           const Expr& callee,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           CallTargetInfo* out);

bool CheckBoolCondition(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error);

bool StmtReturns(const Stmt& stmt);
bool StmtsReturn(const std::vector<Stmt>& stmts);

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

const LocalInfo* FindLocal(const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const std::string& name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
}

bool AddLocal(std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
              const std::string& name,
              const LocalInfo& info,
              std::string* error) {
  if (scopes.empty()) scopes.emplace_back();
  auto& current = scopes.back();
  if (!current.emplace(name, info).second) {
    if (error) *error = "duplicate local declaration: " + name;
    return false;
  }
  return true;
}

bool IsAssignOp(const std::string& op) {
  return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
         op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=";
}

const VarDecl* FindModuleVar(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& var : module->variables) {
    if (var.name == name) return &var;
  }
  return nullptr;
}

const VarDecl* FindArtifactField(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return nullptr;
  for (const auto& field : artifact->fields) {
    if (field.name == name) return &field;
  }
  return nullptr;
}

const FuncDecl* FindArtifactMethod(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return nullptr;
  for (const auto& method : artifact->methods) {
    if (method.name == name) return &method;
  }
  return nullptr;
}

bool IsArtifactMemberName(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return false;
  if (FindArtifactField(artifact, name)) return true;
  if (FindArtifactMethod(artifact, name)) return true;
  return false;
}

const FuncDecl* FindModuleFunc(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& fn : module->functions) {
    if (fn.name == name) return &fn;
  }
  return nullptr;
}

bool CheckCallArgs(const FuncDecl* fn, size_t arg_count, std::string* error) {
  if (!fn) return false;
  if (fn->params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch for " + fn->name + ": expected " +
               std::to_string(fn->params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckProcTypeArgs(const TypeRef* type, size_t arg_count, std::string* error) {
  if (!type || !type->is_proc) return false;
  if (type->proc_is_callback) return true;
  if (type->proc_params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch: expected " +
               std::to_string(type->proc_params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckCallTarget(const Expr& callee,
                     size_t arg_count,
                     const ValidateContext& ctx,
                     const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                     const ArtifactDecl* current_artifact,
                     std::string* error) {
  if (callee.kind == ExprKind::FnLiteral) {
    if (callee.fn_params.size() != arg_count) {
      if (error) {
        *error = "call argument count mismatch for fn literal: expected " +
                 std::to_string(callee.fn_params.size()) + ", got " + std::to_string(arg_count);
      }
      return false;
    }
    return true;
  }
  if (callee.kind == ExprKind::Identifier) {
    if (IsPrimitiveCastName(callee.text)) {
      if (error) *error = "primitive cast syntax requires '@': use @" + callee.text + "(value)";
      return false;
    }
    auto fn_it = ctx.functions.find(callee.text);
    if (fn_it != ctx.functions.end()) {
      return CheckCallArgs(fn_it->second, arg_count, error);
    }
    auto ext_it = ctx.externs.find(callee.text);
    if (ext_it != ctx.externs.end()) {
      if (ext_it->second->params.size() != arg_count) {
        if (error) {
          *error = "call argument count mismatch for extern " + callee.text +
                   ": expected " + std::to_string(ext_it->second->params.size()) +
                   ", got " + std::to_string(arg_count);
        }
        return false;
      }
      return true;
    }
    if (const LocalInfo* local = FindLocal(scopes, callee.text)) {
      if (local->type && local->type->is_proc) {
        return CheckProcTypeArgs(local->type, arg_count, error);
      }
      if (error) *error = "attempt to call non-function: " + callee.text;
      return false;
    }
    auto global_it = ctx.globals.find(callee.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->type.is_proc) {
        return CheckProcTypeArgs(&global_it->second->type, arg_count, error);
      }
      if (error) *error = "attempt to call non-function: " + callee.text;
      return false;
    }
    return true;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (IsIoPrintCallExpr(callee, ctx)) {
        if (arg_count == 0) {
          if (error) *error = "call argument count mismatch for IO." + callee.text;
          return false;
        }
        return true;
      }
      if (base.text == "self") {
        const FuncDecl* method = FindArtifactMethod(current_artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (FindArtifactField(current_artifact, callee.text)) {
          if (error) *error = "attempt to call non-function: self." + callee.text;
          return false;
        }
        return true;
      }
      std::string dl_module;
      if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
        auto mod_it = ctx.externs_by_module.find(dl_module);
        if (mod_it != ctx.externs_by_module.end()) {
          auto ext_it = mod_it->second.find(callee.text);
          if (ext_it != mod_it->second.end()) {
            if (!IsSupportedDlDynamicSignature(*ext_it->second, ctx, error)) return false;
            if (ext_it->second->params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for dynamic symbol " +
                         base.text + "." + callee.text + ": expected " +
                         std::to_string(ext_it->second->params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
          if (error) {
            *error = "unknown dynamic symbol: " + base.text + "." + callee.text;
          }
          return false;
        }
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const FuncDecl* fn = FindModuleFunc(module_it->second, callee.text);
        if (fn) return CheckCallArgs(fn, arg_count, error);
        if (FindModuleVar(module_it->second, callee.text)) {
          const VarDecl* var = FindModuleVar(module_it->second, callee.text);
          if (var && var->type.is_proc) {
            return CheckProcTypeArgs(&var->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
        return true;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          CallTargetInfo info;
          if (GetReservedModuleCallTarget(ctx, module_name, callee.text, &info)) {
            std::string resolved_module;
            const bool is_core_dl_open =
                ResolveReservedModuleName(ctx, module_name, &resolved_module) &&
                resolved_module == "Core.DL" &&
                NormalizeCoreDlMember(callee.text) == "open";
            if (!is_core_dl_open && info.params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for " + module_name + "." + callee.text +
                         ": expected " + std::to_string(info.params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            if (is_core_dl_open && arg_count != 1 && arg_count != 2) {
              if (error) {
                *error = "call argument count mismatch for " + module_name + "." + callee.text +
                         ": expected 1 or 2, got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(callee.text);
          if (ext_it != ext_mod_it->second.end()) {
            if (ext_it->second->params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for extern " + module_name + "." + callee.text +
                         ": expected " + std::to_string(ext_it->second->params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
          if (error) *error = "unknown extern member: " + module_name + "." + callee.text;
          return false;
        }
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (const VarDecl* field = FindArtifactField(artifact, callee.text)) {
          if (field->type.is_proc) {
            return CheckProcTypeArgs(&field->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
        return true;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (const VarDecl* field = FindArtifactField(artifact, callee.text)) {
          if (field->type.is_proc) {
            return CheckProcTypeArgs(&field->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
      }
    }
  }
  return true;
}

bool GetCallTargetInfo(const Expr& callee,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       CallTargetInfo* out,
                       std::string* error) {
  if (!out) return false;
  if (callee.kind == ExprKind::FnLiteral) {
    out->params.clear();
    out->return_type = TypeRef{};
    out->return_mutability = Mutability::Mutable;
    out->type_params.clear();
    out->is_proc = true;
    for (const auto& param : callee.fn_params) {
      TypeRef copy;
      if (!CloneTypeRef(param.type, &copy)) return false;
      out->params.push_back(std::move(copy));
    }
    return true;
  }
  if (callee.kind == ExprKind::Identifier) {
    auto fn_it = ctx.functions.find(callee.text);
    if (fn_it != ctx.functions.end()) {
      out->params.clear();
      if (!CloneTypeRef(fn_it->second->return_type, &out->return_type)) return false;
      out->return_mutability = fn_it->second->return_mutability;
      out->type_params = fn_it->second->generics;
      out->is_proc = false;
      for (const auto& param : fn_it->second->params) {
        TypeRef copy;
        if (!CloneTypeRef(param.type, &copy)) return false;
        out->params.push_back(std::move(copy));
      }
      return true;
    }
    auto ext_it = ctx.externs.find(callee.text);
    if (ext_it != ctx.externs.end()) {
      out->params.clear();
      if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
      out->return_mutability = ext_it->second->return_mutability;
      out->type_params.clear();
      out->is_proc = false;
      for (const auto& param : ext_it->second->params) {
        TypeRef copy;
        if (!CloneTypeRef(param.type, &copy)) return false;
        out->params.push_back(std::move(copy));
      }
      return true;
    }
    if (const LocalInfo* local = FindLocal(scopes, callee.text)) {
      if (local->type && local->type->is_proc) {
        if (!CloneTypeVector(local->type->proc_params, &out->params)) return false;
        if (local->type->proc_return) {
          if (!CloneTypeRef(*local->type->proc_return, &out->return_type)) return false;
        } else if (local->type->proc_is_callback) {
          out->return_type = MakeSimpleType("void");
        }
        out->return_mutability = local->type->proc_return_mutability;
        out->type_params.clear();
        out->is_proc = true;
        return true;
      }
      return false;
    }
    auto global_it = ctx.globals.find(callee.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->type.is_proc) {
        if (!CloneTypeVector(global_it->second->type.proc_params, &out->params)) return false;
        if (global_it->second->type.proc_return) {
          if (!CloneTypeRef(*global_it->second->type.proc_return, &out->return_type)) return false;
        } else if (global_it->second->type.proc_is_callback) {
          out->return_type = MakeSimpleType("void");
        }
        out->return_mutability = global_it->second->type.proc_return_mutability;
        out->type_params.clear();
        out->is_proc = true;
        return true;
      }
      return false;
    }
    return false;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (IsIoPrintCallExpr(callee, ctx)) {
        out->params.clear();
        TypeRef param;
        param.name = "T";
        param.type_args.clear();
        param.dims.clear();
        param.is_proc = false;
        param.proc_is_callback = false;
        param.proc_params.clear();
        param.proc_return.reset();
        out->params.push_back(std::move(param));
        out->return_type = TypeRef{};
        out->return_type.name = "void";
        out->return_type.type_args.clear();
        out->return_type.dims.clear();
        out->return_type.is_proc = false;
        out->return_type.proc_is_callback = false;
        out->return_type.proc_params.clear();
        out->return_type.proc_return.reset();
        out->return_mutability = Mutability::Mutable;
        out->type_params = {"T"};
        out->is_proc = false;
        return true;
      }
      if (base.text == "self") {
        const FuncDecl* method = FindArtifactMethod(current_artifact, callee.text);
        if (!method) return false;
        out->params.clear();
        if (!CloneTypeRef(method->return_type, &out->return_type)) return false;
        out->return_mutability = method->return_mutability;
        out->type_params = method->generics;
        out->is_proc = false;
        for (const auto& param : method->params) {
          TypeRef copy;
          if (!CloneTypeRef(param.type, &copy)) return false;
          out->params.push_back(std::move(copy));
        }
        return true;
      }
      std::string dl_module;
      if (ResolveDlModuleForIdentifier(base.text, ctx, scopes, &dl_module)) {
        auto mod_it = ctx.externs_by_module.find(dl_module);
        if (mod_it != ctx.externs_by_module.end()) {
          auto ext_it = mod_it->second.find(callee.text);
          if (ext_it != mod_it->second.end()) {
            if (!IsSupportedDlDynamicSignature(*ext_it->second, ctx, error)) return false;
            out->params.clear();
            if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
            out->return_mutability = ext_it->second->return_mutability;
            out->type_params.clear();
            out->is_proc = false;
            for (const auto& param : ext_it->second->params) {
              TypeRef copy;
              if (!CloneTypeRef(param.type, &copy)) return false;
              out->params.push_back(std::move(copy));
            }
            return true;
          }
        }
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const FuncDecl* fn = FindModuleFunc(module_it->second, callee.text);
        if (fn) {
          out->params.clear();
          if (!CloneTypeRef(fn->return_type, &out->return_type)) return false;
          out->return_mutability = fn->return_mutability;
          out->type_params = fn->generics;
          out->is_proc = false;
          for (const auto& param : fn->params) {
            TypeRef copy;
            if (!CloneTypeRef(param.type, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* var = FindModuleVar(module_it->second, callee.text);
        if (var && var->type.is_proc) {
          if (!CloneTypeVector(var->type.proc_params, &out->params)) return false;
          if (var->type.proc_return) {
            if (!CloneTypeRef(*var->type.proc_return, &out->return_type)) return false;
          } else if (var->type.proc_is_callback) {
            out->return_type = MakeSimpleType("void");
          }
          out->return_mutability = var->type.proc_return_mutability;
          out->type_params.clear();
          out->is_proc = true;
          return true;
        }
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          if (GetReservedModuleCallTarget(ctx, module_name, callee.text, out)) {
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(callee.text);
          if (ext_it != ext_mod_it->second.end()) {
            out->params.clear();
            if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
            out->return_mutability = ext_it->second->return_mutability;
            out->type_params.clear();
            out->is_proc = false;
            for (const auto& param : ext_it->second->params) {
              TypeRef copy;
              if (!CloneTypeRef(param.type, &copy)) return false;
              out->params.push_back(std::move(copy));
            }
            return true;
          }
        }
      }
      if (GetListCallTargetInfo(base, callee, ctx, scopes, current_artifact, out)) {
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return false;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, error)) return false;
          }
          out->params.clear();
          TypeRef resolved_return;
          if (!SubstituteTypeParams(method->return_type, mapping, &resolved_return)) return false;
          if (!CloneTypeRef(resolved_return, &out->return_type)) return false;
          out->return_mutability = method->return_mutability;
          out->type_params = method->generics;
          out->is_proc = false;
          for (const auto& param : method->params) {
            TypeRef copy;
            if (!SubstituteTypeParams(param.type, mapping, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* field = FindArtifactField(artifact, callee.text);
        if (field && field->type.is_proc) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, error)) return false;
          }
          TypeRef resolved_field;
          if (!SubstituteTypeParams(field->type, mapping, &resolved_field)) return false;
          out->params.clear();
          out->type_params.clear();
          out->is_proc = true;
          out->return_mutability = resolved_field.proc_return_mutability;
          if (!CloneTypeVector(resolved_field.proc_params, &out->params)) return false;
          if (resolved_field.proc_return) {
            if (!CloneTypeRef(*resolved_field.proc_return, &out->return_type)) return false;
          } else if (resolved_field.proc_is_callback) {
            out->return_type = MakeSimpleType("void");
          }
          return true;
        }
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, error)) return false;
          }
          out->params.clear();
          TypeRef resolved_return;
          if (!SubstituteTypeParams(method->return_type, mapping, &resolved_return)) return false;
          if (!CloneTypeRef(resolved_return, &out->return_type)) return false;
          out->return_mutability = method->return_mutability;
          out->type_params = method->generics;
          out->is_proc = false;
          for (const auto& param : method->params) {
            TypeRef copy;
            if (!SubstituteTypeParams(param.type, mapping, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* field = FindArtifactField(artifact, callee.text);
        if (field && field->type.is_proc) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, error)) return false;
          }
          TypeRef resolved_field;
          if (!SubstituteTypeParams(field->type, mapping, &resolved_field)) return false;
          out->params.clear();
          out->type_params.clear();
          out->is_proc = true;
          out->return_mutability = resolved_field.proc_return_mutability;
          if (!CloneTypeVector(resolved_field.proc_params, &out->params)) return false;
          if (resolved_field.proc_return) {
            if (!CloneTypeRef(*resolved_field.proc_return, &out->return_type)) return false;
          } else if (resolved_field.proc_is_callback) {
            out->return_type = MakeSimpleType("void");
          }
          return true;
        }
      }
    }
  }
  if (error) *error = "attempt to call non-function";
  return false;
}

bool CheckCallArgTypes(const Expr& call_expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  if (call_expr.kind != ExprKind::Call || call_expr.children.empty()) return true;
  const Expr& callee = call_expr.children[0];
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    std::string module_name;
    if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
      std::string mod = module_name;
      std::string resolved_mod;
      if (ResolveReservedModuleName(ctx, module_name, &resolved_mod)) mod = resolved_mod;
      const std::string& name = callee.text;
      auto infer_arg = [&](size_t index, TypeRef* out_type) -> bool {
        if (!out_type) return false;
        if (index >= call_expr.args.size()) return false;
        return InferExprType(call_expr.args[index], ctx, scopes, current_artifact, out_type);
      };
      auto is_i32_buffer = [&](const TypeRef& t) -> bool {
        return t.name == "i32" && !t.is_proc && t.type_args.empty() &&
               t.dims.size() == 1;
      };
      if (mod == "Core.Math") {
        if (name == "abs") {
          if (call_expr.args.size() != 1) return true;
          TypeRef arg;
          if (!infer_arg(0, &arg)) return true;
          if ((arg.name != "i32" && arg.name != "i64") || !arg.dims.empty() || arg.is_proc) {
            if (error) *error = "Math.abs expects i32 or i64 argument";
            return false;
          }
          return true;
        }
        if (name == "min" || name == "max") {
          if (call_expr.args.size() != 2) return true;
          TypeRef a;
          TypeRef b;
          if (!infer_arg(0, &a) || !infer_arg(1, &b)) return true;
          auto allowed = [&](const TypeRef& t) {
            return t.name == "i32" || t.name == "i64" || t.name == "f32" || t.name == "f64";
          };
          if (!allowed(a) || !allowed(b) || !TypeEquals(a, b) || !a.dims.empty() || !b.dims.empty()) {
            if (error) *error = "Math." + name + " expects two numeric arguments of the same type";
            return false;
          }
          return true;
        }
      }
      if (mod == "Core.IO") {
        if (name == "buffer_new") {
          if (call_expr.args.size() != 1) return true;
          TypeRef len;
          if (!infer_arg(0, &len)) return true;
          if (len.name != "i32" || !len.dims.empty()) {
            if (error) *error = "IO.buffer_new expects (i32)";
            return false;
          }
          return true;
        }
        if (name == "buffer_len") {
          if (call_expr.args.size() != 1) return true;
          TypeRef buf;
          if (!infer_arg(0, &buf)) return true;
          if (!is_i32_buffer(buf)) {
            if (error) *error = "IO.buffer_len expects (i32[])";
            return false;
          }
          return true;
        }
        if (name == "buffer_fill") {
          if (call_expr.args.size() != 3) return true;
          TypeRef buf;
          TypeRef value;
          TypeRef count;
          if (!infer_arg(0, &buf) || !infer_arg(1, &value) || !infer_arg(2, &count)) return true;
          if (!is_i32_buffer(buf) || value.name != "i32" || !value.dims.empty() ||
              count.name != "i32" || !count.dims.empty()) {
            if (error) *error = "IO.buffer_fill expects (i32[], i32, i32)";
            return false;
          }
          return true;
        }
        if (name == "buffer_copy") {
          if (call_expr.args.size() != 3) return true;
          TypeRef dst;
          TypeRef src;
          TypeRef count;
          if (!infer_arg(0, &dst) || !infer_arg(1, &src) || !infer_arg(2, &count)) return true;
          if (!is_i32_buffer(dst) || !is_i32_buffer(src) || count.name != "i32" || !count.dims.empty()) {
            if (error) *error = "IO.buffer_copy expects (i32[], i32[], i32)";
            return false;
          }
          return true;
        }
      }
      if (mod == "Core.Time") {
        if (name == "mono_ns" || name == "wall_ns") {
          if (!call_expr.args.empty()) {
            if (error) *error = "Time." + name + " expects no arguments";
            return false;
          }
          return true;
        }
      }
      if (mod == "Core.DL" && NormalizeCoreDlMember(name) == "open") {
        if (call_expr.args.size() != 1 && call_expr.args.size() != 2) {
          if (error) *error = "Core.DL.open expects (string) or (string, manifest)";
          return false;
        }
        TypeRef path;
        if (!infer_arg(0, &path)) return true;
        if (path.name != "string" || !path.dims.empty()) {
          if (error) *error = "Core.DL.open expects first argument string path";
          return false;
        }
        if (call_expr.args.size() == 2) {
          if (call_expr.args[1].kind != ExprKind::Identifier) {
            if (error) *error = "Core.DL.open manifest must be an extern module identifier";
            return false;
          }
          const std::string manifest = call_expr.args[1].text;
          auto mod_it = ctx.externs_by_module.find(manifest);
          if (mod_it == ctx.externs_by_module.end() || mod_it->second.empty()) {
            if (error) *error = "Core.DL.open manifest has no extern symbols: " + manifest;
            return false;
          }
          for (const auto& entry : mod_it->second) {
            if (!IsSupportedDlDynamicSignature(*entry.second, ctx, error)) return false;
          }
        }
        return true;
      }
      if (mod == "File") {
        if (name == "open") {
          if (call_expr.args.size() != 2) return true;
          TypeRef path;
          TypeRef flags;
          if (!infer_arg(0, &path) || !infer_arg(1, &flags)) return true;
          if (path.name != "string" || !path.dims.empty() || flags.name != "i32" || !flags.dims.empty()) {
            if (error) *error = "File.open expects (string, i32)";
            return false;
          }
          return true;
        }
        if (name == "close") {
          if (call_expr.args.size() != 1) return true;
          TypeRef fd;
          if (!infer_arg(0, &fd)) return true;
          if (fd.name != "i32" || !fd.dims.empty()) {
            if (error) *error = "File.close expects (i32)";
            return false;
          }
          return true;
        }
        if (name == "read" || name == "write") {
          if (call_expr.args.size() != 3) return true;
          TypeRef fd;
          TypeRef buf;
          TypeRef len;
          if (!infer_arg(0, &fd) || !infer_arg(1, &buf) || !infer_arg(2, &len)) return true;
          if (fd.name != "i32" || !fd.dims.empty() || len.name != "i32" || !len.dims.empty() ||
              !is_i32_buffer(buf)) {
            if (error) *error = "File." + name + " expects (i32, i32[], i32)";
            return false;
          }
          return true;
        }
      }
    }
  }
  CallTargetInfo info;
  if (!GetCallTargetInfo(callee, ctx, scopes, current_artifact, &info, error)) return true;
  if (!info.type_params.empty() && !call_expr.type_args.empty()) {
    if (call_expr.type_args.size() != info.type_params.size()) {
      if (error) {
        *error = "generic type argument count mismatch: expected " +
                 std::to_string(info.type_params.size()) + ", got " +
                 std::to_string(call_expr.type_args.size());
      }
      return false;
    }
  } else if (info.type_params.empty() && !call_expr.type_args.empty()) {
    if (error) *error = "non-generic call cannot take type arguments";
    return false;
  }

  std::unordered_map<std::string, TypeRef> mapping;
  if (!info.type_params.empty()) {
    std::unordered_set<std::string> type_param_set(info.type_params.begin(), info.type_params.end());
    if (!call_expr.type_args.empty()) {
      for (size_t i = 0; i < info.type_params.size(); ++i) {
        TypeRef copy;
        if (!CloneTypeRef(call_expr.type_args[i], &copy)) return false;
        mapping[info.type_params[i]] = std::move(copy);
      }
    } else {
      if (!InferTypeArgsFromCall(info.params, call_expr.args, type_param_set,
                                 ctx, scopes, current_artifact, &mapping)) {
        if (error) *error = "cannot infer type arguments for call";
        return false;
      }
    }
  }

  for (size_t i = 0; i < info.params.size() && i < call_expr.args.size(); ++i) {
    TypeRef expected;
    if (!SubstituteTypeParams(info.params[i], mapping, &expected)) return false;
    TypeRef actual;
    if (!InferExprType(call_expr.args[i], ctx, scopes, current_artifact, &actual)) continue;
    if (!TypesCompatibleForExpr(expected, actual, call_expr.args[i])) {
      if (error) *error = "call argument type mismatch";
      return false;
    }
  }
  return true;
}

bool CheckAssignmentTarget(const Expr& target,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           std::string* error) {
  std::function<bool(const Expr&)> is_mutable_expr = [&](const Expr& expr) -> bool {
    if (expr.kind == ExprKind::Identifier) {
      if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
        return local->mutability == Mutability::Mutable;
      }
      auto global_it = ctx.globals.find(expr.text);
      if (global_it != ctx.globals.end()) {
        return global_it->second->mutability == Mutability::Mutable;
      }
      return true;
    }
    if (expr.kind == ExprKind::Member && expr.op == "." && !expr.children.empty()) {
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier) {
        if (base.text == "self") {
          const VarDecl* field = FindArtifactField(current_artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
          return true;
        }
        auto module_it = ctx.modules.find(base.text);
        if (module_it != ctx.modules.end()) {
          const VarDecl* var = FindModuleVar(module_it->second, expr.text);
          if (var) return var->mutability == Mutability::Mutable;
          return true;
        }
        if (const LocalInfo* local = FindLocal(scopes, base.text)) {
          auto artifact_it = ctx.artifacts.find(local->type ? local->type->name : "");
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          const VarDecl* field = FindArtifactField(artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
          return true;
        }
        auto global_it = ctx.globals.find(base.text);
        if (global_it != ctx.globals.end()) {
          auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          const VarDecl* field = FindArtifactField(artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
        }
      }
      return true;
    }
    if (expr.kind == ExprKind::Call) {
      CallTargetInfo info;
      if (!GetCallTargetInfo(expr.children[0], ctx, scopes, current_artifact, &info, nullptr)) return true;
      return info.return_mutability == Mutability::Mutable;
    }
    if (expr.kind == ExprKind::Index) {
      if (expr.children.empty()) return true;
      return is_mutable_expr(expr.children[0]);
    }
    return true;
  };
  if (target.kind == ExprKind::Identifier) {
    if (target.text == "self") {
      if (error) *error = "cannot assign to self";
      return false;
    }
    if (const LocalInfo* local = FindLocal(scopes, target.text)) {
      if (local->mutability == Mutability::Immutable) {
        if (error) *error = "cannot assign to immutable local: " + target.text;
        return false;
      }
      return true;
    }
    auto global_it = ctx.globals.find(target.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->mutability == Mutability::Immutable) {
        if (error) *error = "cannot assign to immutable variable: " + target.text;
        return false;
      }
      return true;
    }
    if (ctx.functions.find(target.text) != ctx.functions.end()) {
      if (error) *error = "cannot assign to function: " + target.text;
      return false;
    }
    return true;
  }
  if (target.kind == ExprKind::Member && target.op == "." && !target.children.empty()) {
    const Expr& base = target.children[0];
    if (!is_mutable_expr(base)) {
      if (error) *error = "cannot assign through immutable value";
      return false;
    }
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "self") {
        const VarDecl* field = FindArtifactField(current_artifact, target.text);
        if (!field && FindArtifactMethod(current_artifact, target.text)) {
          if (error) *error = "cannot assign to method: self." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: self." + target.text;
          return false;
        }
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, target.text);
        if (!field && FindArtifactMethod(artifact, target.text)) {
          if (error) *error = "cannot assign to method: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const VarDecl* field = FindModuleVar(module_it->second, target.text);
        if (!field && FindModuleFunc(module_it->second, target.text)) {
          if (error) *error = "cannot assign to function: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable module member: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
        if (error) *error = "cannot assign to immutable module member: " + module_name + "." + target.text;
        return false;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, target.text);
        if (!field && FindArtifactMethod(artifact, target.text)) {
          if (error) *error = "cannot assign to method: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
      }
    }
    return true;
  }
  if (target.kind == ExprKind::Index) {
    if (!target.children.empty() && !is_mutable_expr(target.children[0])) {
      if (error) *error = "cannot assign through immutable value";
      return false;
    }
    return true;
  }
  if (error) *error = "invalid assignment target";
  return false;
}

bool ValidateArtifactLiteral(const Expr& expr,
                             const ArtifactDecl* artifact,
                             const std::unordered_map<std::string, TypeRef>& type_mapping,
                             const ValidateContext& ctx,
                             const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                             const ArtifactDecl* current_artifact,
                             std::string* error) {
  if (!artifact) return true;
  const size_t field_count = artifact->fields.size();
  if (expr.children.size() > field_count) {
    if (error) *error = "too many positional values in artifact literal";
    return false;
  }
  std::unordered_set<std::string> seen;
  for (const auto& name : expr.field_names) {
    if (!seen.insert(name).second) {
      if (error) *error = "duplicate named field in artifact literal: " + name;
      return false;
    }
  }
  for (size_t i = 0; i < expr.children.size(); ++i) {
    if (i >= field_count) break;
    const auto& field = artifact->fields[i];
    if (seen.find(field.name) != seen.end()) {
      if (error) *error = "field specified twice in artifact literal: " + field.name;
      return false;
    }
    seen.insert(field.name);
    TypeRef value_type;
    if (InferExprType(expr.children[i], ctx, scopes, current_artifact, &value_type)) {
      TypeRef expected;
      if (!SubstituteTypeParams(field.type, type_mapping, &expected)) return false;
      if (!TypesCompatibleForExpr(expected, value_type, expr.children[i])) {
        if (error) *error = "artifact field type mismatch: " + field.name;
        return false;
      }
    }
  }
  if (!expr.field_names.empty()) {
    std::unordered_set<std::string> valid;
    std::unordered_map<std::string, const VarDecl*> field_map;
    for (const auto& field : artifact->fields) {
      valid.insert(field.name);
      field_map[field.name] = &field;
    }
    for (const auto& name : expr.field_names) {
      if (valid.find(name) == valid.end()) {
        if (error) *error = "unknown artifact field: " + name;
        return false;
      }
    }
    for (size_t i = 0; i < expr.field_names.size(); ++i) {
      const auto& name = expr.field_names[i];
      auto it = field_map.find(name);
      if (it == field_map.end()) continue;
      TypeRef value_type;
      if (InferExprType(expr.field_values[i], ctx, scopes, current_artifact, &value_type)) {
        TypeRef expected;
        if (!SubstituteTypeParams(it->second->type, type_mapping, &expected)) return false;
        if (!TypesCompatibleForExpr(expected, value_type, expr.field_values[i])) {
          if (error) *error = "artifact field type mismatch: " + name;
          return false;
        }
      }
    }
  }
  return true;
}
