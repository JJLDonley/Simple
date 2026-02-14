bool CollectTypeParams(const std::vector<std::string>& generics,
                        std::unordered_set<std::string>* out,
                        std::string* error);
bool CollectTypeParamsMerged(const std::vector<std::string>& a,
                             const std::vector<std::string>& b,
                             std::unordered_set<std::string>* out,
                             std::string* error);

bool ValidateProgram(const Program& program, std::string* error) {
  ValidateContext ctx;
  if (program.decls.empty() && program.top_level_stmts.empty()) {
    if (error) *error = "program has no declarations or top-level statements";
    return false;
  }
  for (const auto& decl : program.decls) {
    const std::string* name_ptr = nullptr;
    switch (decl.kind) {
      case DeclKind::Import:
      {
        std::string canonical_import;
        if (!CanonicalizeReservedImportPath(decl.import_decl.path, &canonical_import)) {
          if (error) *error = "unsupported import path: " + decl.import_decl.path;
          return false;
        }
        ctx.reserved_imports.insert(canonical_import);
        if (decl.import_decl.has_alias && !decl.import_decl.alias.empty()) {
          ctx.reserved_import_aliases[decl.import_decl.alias] = canonical_import;
        } else {
          const std::string implicit_alias = DefaultImportAlias(decl.import_decl.path);
          if (!implicit_alias.empty()) {
            ctx.reserved_import_aliases[implicit_alias] = canonical_import;
          }
        }
        break;
      }
      case DeclKind::Extern:
        if (decl.ext.has_module) {
          ctx.externs_by_module[decl.ext.module][decl.ext.name] = &decl.ext;
        } else {
          name_ptr = &decl.ext.name;
          ctx.externs[decl.ext.name] = &decl.ext;
        }
        break;
      case DeclKind::Enum:
        name_ptr = &decl.enm.name;
        {
          std::unordered_set<std::string> local_members;
          for (const auto& member : decl.enm.members) {
            if (!member.has_value) {
              if (error) *error = "enum member requires explicit value: " + member.name;
              return false;
            }
            if (!local_members.insert(member.name).second) {
              if (error) *error = "duplicate enum member: " + member.name;
              return false;
            }
            ctx.enum_members.insert(member.name);
          }
          ctx.enum_members_by_type[decl.enm.name] = std::move(local_members);
        }
        ctx.enum_types.insert(decl.enm.name);
        break;
      case DeclKind::Artifact:
        name_ptr = &decl.artifact.name;
        ctx.artifacts[decl.artifact.name] = &decl.artifact;
        ctx.artifact_generics[decl.artifact.name] = decl.artifact.generics.size();
        break;
      case DeclKind::Module:
        name_ptr = &decl.module.name;
        ctx.modules[decl.module.name] = &decl.module;
        break;
      case DeclKind::Function:
        name_ptr = &decl.func.name;
        ctx.functions[decl.func.name] = &decl.func;
        break;
      case DeclKind::Variable:
        name_ptr = &decl.var.name;
        ctx.globals[decl.var.name] = &decl.var;
        break;
    }
    if (name_ptr && !ctx.top_level.insert(*name_ptr).second) {
      if (error) *error = "duplicate top-level declaration: " + *name_ptr;
      return false;
    }
  }

  if (!program.top_level_stmts.empty()) {
    std::vector<std::unordered_map<std::string, LocalInfo>> scopes;
    scopes.emplace_back();
    std::unordered_set<std::string> type_params;
    TypeRef script_return;
    script_return.name = "i32";
    for (const auto& stmt : program.top_level_stmts) {
      if (stmt.kind == StmtKind::Return) {
        if (error) *error = "top-level return is not allowed";
        return false;
      }
      if (!CheckStmt(stmt,
                     ctx,
                     type_params,
                     &script_return,
                     false,
                     0,
                     scopes,
                     nullptr,
                     error)) {
        if (error && !error->empty()) {
          *error = "in top-level script: " + *error;
        }
        return false;
      }
    }
  }

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case DeclKind::Import:
        break;
      case DeclKind::Extern:
        {
          std::unordered_set<std::string> param_names;
          std::unordered_set<std::string> type_params;
          if (IsCallbackType(decl.ext.return_type)) {
            if (error) *error = "callback is only valid as a parameter type";
            return false;
          }
          if (!CheckTypeRef(decl.ext.return_type, ctx, type_params, TypeUse::Return, error)) return false;
          for (const auto& param : decl.ext.params) {
            if (!param_names.insert(param.name).second) {
              if (error) *error = "duplicate extern parameter name: " + param.name;
              return false;
            }
            if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
        }
        break;
      case DeclKind::Function:
        {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(decl.func.generics, &type_params, error)) return false;
          if (!CheckFunctionBody(decl.func, ctx, type_params, nullptr, error)) {
            if (error && !error->empty()) {
              *error = "in function '" + decl.func.name + "': " + *error;
            }
            return false;
          }
        }
        break;
      case DeclKind::Artifact:
        {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(decl.artifact.generics, &type_params, error)) return false;
          std::unordered_set<std::string> names;
          for (const auto& field : decl.artifact.fields) {
            if (!names.insert(field.name).second) {
              if (error) *error = "duplicate artifact member: " + field.name;
              return false;
            }
            if (IsCallbackType(field.type)) {
              if (error) *error = "callback is only valid as a parameter type";
              return false;
            }
            if (!CheckTypeRef(field.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
          for (const auto& method : decl.artifact.methods) {
            if (!names.insert(method.name).second) {
              if (error) *error = "duplicate artifact member: " + method.name;
              return false;
            }
          }
          for (const auto& method : decl.artifact.methods) {
            std::unordered_set<std::string> method_params;
            if (!CollectTypeParamsMerged(decl.artifact.generics,
                                         method.generics,
                                         &method_params,
                                         error)) {
              return false;
            }
            if (!CheckFunctionBody(method, ctx, method_params, &decl.artifact, error)) {
              if (error && !error->empty()) {
                *error = "in function '" + decl.artifact.name + "." + method.name + "': " + *error;
              }
              return false;
            }
          }
        }
        break;
      case DeclKind::Module:
        {
          std::unordered_set<std::string> names;
          for (const auto& var : decl.module.variables) {
            if (!names.insert(var.name).second) {
              if (error) *error = "duplicate module member: " + var.name;
              return false;
            }
            std::unordered_set<std::string> type_params;
            if (IsCallbackType(var.type)) {
              if (error) *error = "callback is only valid as a parameter type";
              return false;
            }
            if (!CheckTypeRef(var.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
          for (const auto& fn : decl.module.functions) {
            if (!names.insert(fn.name).second) {
              if (error) *error = "duplicate module member: " + fn.name;
              return false;
            }
          }
        }
        for (const auto& fn : decl.module.functions) {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(fn.generics, &type_params, error)) return false;
          if (!CheckFunctionBody(fn, ctx, type_params, nullptr, error)) {
            if (error && !error->empty()) {
              *error = "in function '" + decl.module.name + "." + fn.name + "': " + *error;
            }
            return false;
          }
        }
        break;
      case DeclKind::Enum:
      case DeclKind::Variable:
        if (decl.kind == DeclKind::Variable) {
          std::unordered_set<std::string> type_params;
          if (IsCallbackType(decl.var.type)) {
            if (error) *error = "callback is only valid as a parameter type";
            return false;
          }
          if (!CheckTypeRef(decl.var.type, ctx, type_params, TypeUse::Value, error)) return false;
        }
        break;
    }
  }

  return true;
}

bool ValidateProgramFromString(const std::string& text, std::string* error) {
  Program program;
  if (!ParseProgramFromString(text, &program, error)) return false;
  return ValidateProgram(program, error);
}

} // namespace Simple::Lang
