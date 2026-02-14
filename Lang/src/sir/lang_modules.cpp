bool EmitProgramImpl(const Program& program, std::string* out, std::string* error) {
  EmitState st;
  st.error = error;

  std::vector<FuncItem> functions;
  std::vector<const ArtifactDecl*> artifacts;
  std::vector<const EnumDecl*> enums;
  std::vector<const ExternDecl*> externs;
  std::vector<const VarDecl*> globals;
  FuncDecl global_init_fn;
  FuncDecl script_entry_fn;
  const bool has_top_level_script = !program.top_level_stmts.empty();
  if (has_top_level_script) {
    script_entry_fn.name = "__script_entry";
    script_entry_fn.return_mutability = Mutability::Mutable;
    script_entry_fn.return_type.name = "i32";
  }
  for (const auto& decl : program.decls) {
    if (decl.kind == DeclKind::Import || decl.kind == DeclKind::Extern) {
      if (decl.kind == DeclKind::Import) {
        std::string canonical_import;
        if (!CanonicalizeReservedImportPath(decl.import_decl.path, &canonical_import)) {
          if (error) *error = "unsupported import path: " + decl.import_decl.path;
          return false;
        }
        st.reserved_imports.insert(canonical_import);
        if (decl.import_decl.has_alias && !decl.import_decl.alias.empty()) {
          st.reserved_import_aliases[decl.import_decl.alias] = canonical_import;
        } else {
          const std::string implicit_alias = DefaultImportAlias(decl.import_decl.path);
          if (!implicit_alias.empty()) {
            st.reserved_import_aliases[implicit_alias] = canonical_import;
          }
        }
      }
      if (decl.kind == DeclKind::Extern) {
        externs.push_back(&decl.ext);
      }
      continue;
    } else if (decl.kind == DeclKind::Function) {
      functions.push_back({&decl.func, decl.func.name, decl.func.name, false, {}});
    } else if (decl.kind == DeclKind::Artifact) {
      artifacts.push_back(&decl.artifact);
      st.artifacts.emplace(decl.artifact.name, &decl.artifact);
      for (const auto& method : decl.artifact.methods) {
        const std::string emit_name = decl.artifact.name + "__" + method.name;
        const std::string display = decl.artifact.name + "." + method.name;
        st.artifact_method_names.emplace(display, emit_name);
        FuncItem item;
        item.decl = &method;
        item.emit_name = emit_name;
        item.display_name = display;
        item.has_self = true;
        item.self_type.name = decl.artifact.name;
        functions.push_back(std::move(item));
      }
    } else if (decl.kind == DeclKind::Enum) {
      enums.push_back(&decl.enm);
      std::unordered_map<std::string, int64_t> values;
      for (const auto& member : decl.enm.members) {
        int64_t value = 0;
        if (member.has_value) {
          if (!ParseIntegerLiteralText(member.value_text, &value)) {
            if (error) *error = "invalid enum value for " + decl.enm.name + "." + member.name;
            return false;
          }
        }
        values.emplace(member.name, value);
      }
      st.enum_values.emplace(decl.enm.name, std::move(values));
    } else if (decl.kind == DeclKind::Module) {
      if (!decl.module.variables.empty()) {
        if (error) *error = "module variables are not supported in SIR emission";
        return false;
      }
      for (const auto& fn : decl.module.functions) {
        const std::string key = decl.module.name + "." + fn.name;
        const std::string emit_name = decl.module.name + "__" + fn.name;
        st.module_func_names.emplace(key, emit_name);
        functions.push_back({&fn, emit_name, key, false, {}});
      }
    } else if (decl.kind == DeclKind::Variable) {
      globals.push_back(&decl.var);
    } else {
      if (error) *error = "unsupported top-level declaration in SIR emission";
      return false;
    }
  }
  if (!globals.empty()) {
    st.global_decls = globals;
    bool has_global_init = false;
    for (const auto* g : globals) {
      if (g->has_init_expr) {
        has_global_init = true;
        break;
      }
    }
    if (has_global_init) {
      global_init_fn.name = "__global_init";
      global_init_fn.return_type.name = "void";
      global_init_fn.return_mutability = Mutability::Mutable;
      st.global_init_func_name = global_init_fn.name;
      functions.push_back({&global_init_fn, global_init_fn.name, global_init_fn.name, false, {}});
    }
  }
  if (has_top_level_script) {
    FuncItem item;
    item.decl = &script_entry_fn;
    item.emit_name = script_entry_fn.name;
    item.display_name = script_entry_fn.name;
    item.has_self = false;
    item.script_body = &program.top_level_stmts;
    functions.push_back(std::move(item));
  }
  if (functions.empty()) {
    if (error) *error = "program has no functions or top-level statements";
    return false;
  }

  for (const auto* glob : globals) {
    TypeRef gtype;
    if (!CloneTypeRef(glob->type, &gtype)) return false;
    uint32_t index = static_cast<uint32_t>(st.global_indices.size());
    st.global_indices[glob->name] = index;
    st.global_types[glob->name] = std::move(gtype);
    st.global_mutability[glob->name] = glob->mutability;
  }

  for (size_t i = 0; i < functions.size(); ++i) {
    st.func_ids[functions[i].emit_name] = static_cast<uint32_t>(i);
    TypeRef ret;
    if (!CloneTypeRef(functions[i].decl->return_type, &ret)) return false;
    st.func_returns.emplace(functions[i].emit_name, std::move(ret));
    std::vector<TypeRef> params;
    params.reserve(functions[i].decl->params.size() + (functions[i].has_self ? 1u : 0u));
    if (functions[i].has_self) {
      TypeRef cloned;
      if (!CloneTypeRef(functions[i].self_type, &cloned)) return false;
      params.push_back(std::move(cloned));
    }
    for (const auto& param : functions[i].decl->params) {
      TypeRef cloned;
      if (!CloneTypeRef(param.type, &cloned)) return false;
      params.push_back(std::move(cloned));
    }
    st.func_params.emplace(functions[i].emit_name, std::move(params));
  }
  st.base_func_count = static_cast<uint32_t>(functions.size());

  std::unordered_map<std::string, size_t> import_index_by_key;
  auto clone_params = [&](const std::vector<TypeRef>& src, std::vector<TypeRef>* out_params) -> bool {
    if (!out_params) return false;
    out_params->clear();
    out_params->reserve(src.size());
    for (const auto& param : src) {
      TypeRef cloned;
      if (!CloneTypeRef(param, &cloned)) return false;
      out_params->push_back(std::move(cloned));
    }
    return true;
  };
  uint32_t dynamic_dl_call_index = 0;
  for (const auto* ext : externs) {
    std::string module = ext->has_module ? ResolveImportModule(ext->module) : std::string("host");
    std::string symbol = ext->name;
    std::string key = module + '\0' + symbol;
    if (import_index_by_key.find(key) != import_index_by_key.end()) {
      if (error) *error = "duplicate extern import: " + (module.empty() ? symbol : (module + "." + symbol));
      return false;
    }
    EmitState::ImportItem item;
    item.name = "import_" + std::to_string(st.imports.size());
    item.module = module;
    item.symbol = symbol;
    item.sig_name = "sig_import_" + std::to_string(st.imports.size());
    item.flags = 0;
    std::vector<TypeRef> abi_params;
    abi_params.reserve(ext->params.size());
    for (const auto& param : ext->params) {
      if (!IsSupportedDlAbiType(param.type, st, false)) {
        if (error) {
          *error = "extern '" + (ext->has_module ? (ext->module + ".") : std::string()) + ext->name +
                   "' parameter '" + param.name + "' has unsupported ABI type";
        }
        return false;
      }
      TypeRef cloned_param;
      if (!CloneTypeRef(param.type, &cloned_param)) return false;
      abi_params.push_back(std::move(cloned_param));
    }
    if (!IsSupportedDlAbiType(ext->return_type, st, true)) {
      if (error) {
        *error = "extern '" + (ext->has_module ? (ext->module + ".") : std::string()) + ext->name +
                 "' return has unsupported ABI type";
      }
      return false;
    }
    TypeRef abi_ret;
    if (!CloneTypeRef(ext->return_type, &abi_ret)) return false;
    item.params = std::move(abi_params);
    item.ret = std::move(abi_ret);
    import_index_by_key.emplace(key, st.imports.size());
    st.imports.push_back(std::move(item));

    std::vector<TypeRef> param_copy;
    param_copy.reserve(ext->params.size());
    for (const auto& param : ext->params) {
      TypeRef cloned;
      if (!CloneTypeRef(param.type, &cloned)) return false;
      param_copy.push_back(std::move(cloned));
    }
    TypeRef ret_copy;
    if (!CloneTypeRef(ext->return_type, &ret_copy)) return false;
    if (ext->has_module) {
      st.extern_ids_by_module[ext->module][symbol] = st.imports.back().name;
      st.extern_params_by_module[ext->module][symbol] = std::move(param_copy);
      st.extern_returns_by_module[ext->module][symbol] = std::move(ret_copy);
    } else {
      st.extern_ids[symbol] = st.imports.back().name;
      st.extern_params[symbol] = std::move(param_copy);
      st.extern_returns[symbol] = std::move(ret_copy);
    }

    if (ext->has_module &&
        ResolveImportModule(ext->module) != "core.dl" &&
        IsSupportedDlAbiType(st.imports.back().ret, st, true)) {
      bool all_params_scalar = true;
      for (const auto& p : st.imports.back().params) {
        if (!IsSupportedDlAbiType(p, st, false)) {
          all_params_scalar = false;
          break;
        }
      }
      if (all_params_scalar) {
        EmitState::ImportItem dyn_item;
        dyn_item.name = "import_" + std::to_string(st.imports.size());
        dyn_item.module = "core.dl";
        dyn_item.symbol = "call$" + std::to_string(dynamic_dl_call_index++);
        dyn_item.sig_name = "sig_import_" + std::to_string(st.imports.size());
        dyn_item.flags = 0;
        TypeRef ptr_type;
        ptr_type.name = "i64";
        dyn_item.params.push_back(std::move(ptr_type));
        for (const auto& param : st.imports.back().params) {
          TypeRef cloned_param;
          if (!CloneTypeRef(param, &cloned_param)) return false;
          dyn_item.params.push_back(std::move(cloned_param));
        }
        if (!CloneTypeRef(st.imports.back().ret, &dyn_item.ret)) return false;
        st.dl_call_import_ids_by_module[ext->module][symbol] = dyn_item.name;
        st.imports.push_back(std::move(dyn_item));
      }
    }
  }

  for (const auto* glob : globals) {
    if (!glob->has_init_expr) continue;
    std::string manifest_module;
    if (GetDlOpenManifestModule(glob->init_expr, st, &manifest_module)) {
      st.global_dl_modules[glob->name] = manifest_module;
    }
  }

  auto make_type = [](const char* name) {
    TypeRef out;
    out.name = name;
    out.type_args.clear();
    out.dims.clear();
    out.is_proc = false;
    out.proc_is_callback = false;
    out.proc_params.clear();
    out.proc_return.reset();
    return out;
  };
  auto make_list_type = [&](const char* name) {
    TypeRef out = make_type(name);
    TypeDim dim;
    dim.is_list = true;
    dim.has_size = false;
    dim.size = 0;
    out.dims.push_back(dim);
    return out;
  };
  auto add_reserved_import = [&](const std::string& module_alias,
                                 const std::string& module,
                                 const std::string& symbol,
                                 std::vector<TypeRef>&& params,
                                 TypeRef&& ret) -> bool {
    std::string key = module + '\0' + symbol;
    auto existing_it = import_index_by_key.find(key);
    if (existing_it != import_index_by_key.end()) {
      const size_t existing_idx = existing_it->second;
      st.extern_ids_by_module[module_alias][symbol] = st.imports[existing_idx].name;
      std::vector<TypeRef> param_copy;
      if (!clone_params(st.imports[existing_idx].params, &param_copy)) return false;
      TypeRef ret_copy;
      if (!CloneTypeRef(st.imports[existing_idx].ret, &ret_copy)) return false;
      st.extern_params_by_module[module_alias][symbol] = std::move(param_copy);
      st.extern_returns_by_module[module_alias][symbol] = std::move(ret_copy);
      return true;
    }
    EmitState::ImportItem item;
    item.name = "import_" + std::to_string(st.imports.size());
    item.module = module;
    item.symbol = symbol;
    item.sig_name = "sig_import_" + std::to_string(st.imports.size());
    item.flags = 0;
    item.params = std::move(params);
    item.ret = std::move(ret);
    import_index_by_key.emplace(key, st.imports.size());
    st.imports.push_back(std::move(item));
    std::vector<TypeRef> param_copy;
    if (!clone_params(st.imports.back().params, &param_copy)) return false;
    TypeRef ret_copy;
    if (!CloneTypeRef(st.imports.back().ret, &ret_copy)) return false;
    st.extern_ids_by_module[module_alias][symbol] = st.imports.back().name;
    st.extern_params_by_module[module_alias][symbol] = std::move(param_copy);
    st.extern_returns_by_module[module_alias][symbol] = std::move(ret_copy);
    return true;
  };

  auto reserved_aliases_for = [&](const std::string& name) {
    std::vector<std::string> aliases;
    aliases.push_back(name);
    for (const auto& entry : st.reserved_import_aliases) {
      if (entry.second == name) aliases.push_back(entry.first);
    }
    return aliases;
  };

  if (st.reserved_imports.find("Core.FS") != st.reserved_imports.end()) {
    for (const auto& alias : reserved_aliases_for("Core.FS")) {
      std::vector<TypeRef> open_params;
      open_params.push_back(make_type("string"));
      open_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.fs", "open", std::move(open_params), make_type("i32"))) return false;

      std::vector<TypeRef> close_params;
      close_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.fs", "close", std::move(close_params), make_type("void"))) return false;

      auto make_rw_params = [&]() {
        std::vector<TypeRef> params;
        params.push_back(make_type("i32"));
        params.push_back(make_list_type("i32"));
        params.push_back(make_type("i32"));
        return params;
      };
      if (!add_reserved_import(alias, "core.fs", "read", make_rw_params(), make_type("i32"))) return false;
      if (!add_reserved_import(alias, "core.fs", "write", make_rw_params(), make_type("i32"))) return false;
    }
  }

  if (st.reserved_imports.find("Core.DL") != st.reserved_imports.end()) {
    for (const auto& alias : reserved_aliases_for("Core.DL")) {
      std::vector<TypeRef> open_params;
      open_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "core.dl", "open", std::move(open_params), make_type("i64"))) return false;

      std::vector<TypeRef> sym_params;
      sym_params.push_back(make_type("i64"));
      sym_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "core.dl", "sym", std::move(sym_params), make_type("i64"))) return false;

      std::vector<TypeRef> close_params;
      close_params.push_back(make_type("i64"));
      if (!add_reserved_import(alias, "core.dl", "close", std::move(close_params), make_type("i32"))) return false;

      if (!add_reserved_import(alias, "core.dl", "last_error", {}, make_type("string"))) return false;
    }
  }

  if (st.reserved_imports.find("Core.OS") != st.reserved_imports.end()) {
    for (const auto& alias : reserved_aliases_for("Core.OS")) {
      if (!add_reserved_import(alias, "core.os", "args_count", {}, make_type("i32"))) return false;

      std::vector<TypeRef> idx_params;
      idx_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.os", "args_get", std::move(idx_params), make_type("string"))) return false;

      std::vector<TypeRef> env_params;
      env_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "core.os", "env_get", std::move(env_params), make_type("string"))) return false;

      if (!add_reserved_import(alias, "core.os", "cwd_get", {}, make_type("string"))) return false;
      if (!add_reserved_import(alias, "core.os", "time_mono_ns", {}, make_type("i64"))) return false;
      if (!add_reserved_import(alias, "core.os", "time_wall_ns", {}, make_type("i64"))) return false;

      std::vector<TypeRef> sleep_params;
      sleep_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.os", "sleep_ms", std::move(sleep_params), make_type("void"))) return false;
    }
  }

  if (st.reserved_imports.find("Core.IO") != st.reserved_imports.end()) {
    for (const auto& alias : reserved_aliases_for("Core.IO")) {
      std::vector<TypeRef> new_params;
      new_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.io", "buffer_new", std::move(new_params), make_list_type("i32"))) {
        return false;
      }

      std::vector<TypeRef> len_params;
      len_params.push_back(make_list_type("i32"));
      if (!add_reserved_import(alias, "core.io", "buffer_len", std::move(len_params), make_type("i32"))) {
        return false;
      }

      std::vector<TypeRef> fill_params;
      fill_params.push_back(make_list_type("i32"));
      fill_params.push_back(make_type("i32"));
      fill_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.io", "buffer_fill", std::move(fill_params), make_type("i32"))) {
        return false;
      }

      std::vector<TypeRef> copy_params;
      copy_params.push_back(make_list_type("i32"));
      copy_params.push_back(make_list_type("i32"));
      copy_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.io", "buffer_copy", std::move(copy_params), make_type("i32"))) {
        return false;
      }
    }
  }

  if (st.reserved_imports.find("Core.FS") != st.reserved_imports.end()) {
    for (const auto& alias : reserved_aliases_for("Core.FS")) {
      std::vector<TypeRef> open_params;
      open_params.push_back(make_type("string"));
      open_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.fs", "open", std::move(open_params), make_type("i32"))) return false;

      std::vector<TypeRef> close_params;
      close_params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.fs", "close", std::move(close_params), make_type("void"))) return false;

      auto make_rw_params = [&]() {
        std::vector<TypeRef> params;
        params.push_back(make_type("i32"));
        params.push_back(make_list_type("i32"));
        params.push_back(make_type("i32"));
        return params;
      };
      if (!add_reserved_import(alias, "core.fs", "read", make_rw_params(), make_type("i32"))) return false;
      if (!add_reserved_import(alias, "core.fs", "write", make_rw_params(), make_type("i32"))) return false;
    }
  }

  if (st.reserved_imports.find("Core.Log") != st.reserved_imports.end()) {
    for (const auto& alias : reserved_aliases_for("Core.Log")) {
      std::vector<TypeRef> params;
      params.push_back(make_type("string"));
      params.push_back(make_type("i32"));
      if (!add_reserved_import(alias, "core.log", "log", std::move(params), make_type("void"))) return false;
    }
  }

  for (const auto* artifact : artifacts) {
    EmitState::ArtifactLayout layout;
    uint32_t offset = 0;
    uint32_t max_align = 1;
    layout.fields.reserve(artifact->fields.size());
    for (const auto& field : artifact->fields) {
      EmitState::FieldLayout field_layout;
      field_layout.name = field.name;
      if (!CloneTypeRef(field.type, &field_layout.type)) return false;
      field_layout.sir_type = FieldSirTypeName(field.type, st);
      uint32_t align = FieldAlignForType(field.type);
      uint32_t size = FieldSizeForType(field.type);
      offset = AlignTo(offset, align);
      field_layout.offset = offset;
      offset += size;
      if (align > max_align) max_align = align;
      layout.field_index[field.name] = layout.fields.size();
      layout.fields.push_back(std::move(field_layout));
    }
    layout.size = AlignTo(offset, max_align);
    st.artifact_layouts.emplace(artifact->name, std::move(layout));
  }

  std::string entry_name;
  if (has_top_level_script) {
    entry_name = script_entry_fn.name;
  } else {
    entry_name = functions[0].emit_name;
    for (const auto& fn : functions) {
      if (fn.decl->name == "main") {
        entry_name = fn.emit_name;
        break;
      }
    }
  }

  std::vector<std::string> function_text;
  function_text.reserve(functions.size());
  for (const auto& item : functions) {
    std::string func_body;
    if (!EmitFunction(st,
                      *item.decl,
                      item.emit_name,
                      item.display_name,
                      item.has_self ? &item.self_type : nullptr,
                      item.emit_name == entry_name,
                      item.script_body,
                      &func_body,
                      error)) {
      return false;
    }
    function_text.push_back(std::move(func_body));
  }

  for (size_t i = 0; i < st.lambda_funcs.size(); ++i) {
    std::string func_body;
    if (!EmitFunction(st,
                      st.lambda_funcs[i],
                      st.lambda_funcs[i].name,
                      st.lambda_funcs[i].name,
                      nullptr,
                      false,
                      nullptr,
                      &func_body,
                      error)) {
      return false;
    }
    function_text.push_back(std::move(func_body));
  }

  std::ostringstream result;
  if (!artifacts.empty() || !enums.empty()) {
    result << "types:\n";
    for (const auto* artifact : artifacts) {
      auto it = st.artifact_layouts.find(artifact->name);
      if (it == st.artifact_layouts.end()) return false;
      const auto& layout = it->second;
      result << "  type " << artifact->name << " size=" << layout.size << " kind=artifact\n";
      for (const auto& field : layout.fields) {
        result << "  field " << field.name << " " << field.sir_type << " offset=" << field.offset << "\n";
      }
    }
    for (const auto* enm : enums) {
      result << "  type " << enm->name << " size=4 kind=i32\n";
    }
  }

  result << "sigs:\n";
  struct SigItem {
    const FuncDecl* decl = nullptr;
    std::string name;
    bool has_self = false;
    TypeRef self_type;
  };
  std::vector<SigItem> all_functions;
  all_functions.reserve(functions.size() + st.lambda_funcs.size());
  for (const auto& item : functions) {
    SigItem sig;
    sig.decl = item.decl;
    sig.name = item.emit_name;
    sig.has_self = item.has_self;
    if (item.has_self) {
      if (!CloneTypeRef(item.self_type, &sig.self_type)) return false;
    }
    all_functions.push_back(std::move(sig));
  }
  for (const auto& fn : st.lambda_funcs) {
    all_functions.push_back({&fn, fn.name, false, {}});
  }
  for (const auto& fn : all_functions) {
    std::string ret = SigTypeNameFromType(fn.decl->return_type, st, error);
    if (ret.empty()) {
      if (error && error->empty()) *error = "unsupported return type in signature: " + fn.decl->return_type.name;
      return false;
    }
    result << "  sig " << fn.name << ": (";
    bool first = true;
    if (fn.has_self) {
      std::string param = SigTypeNameFromType(fn.self_type, st, error);
      if (param.empty()) {
        if (error && error->empty()) *error = "unsupported self type in signature";
        return false;
      }
      result << param;
      first = false;
    }
    for (size_t i = 0; i < fn.decl->params.size(); ++i) {
      if (!first) result << ", ";
      std::string param = SigTypeNameFromType(fn.decl->params[i].type, st, error);
      if (param.empty()) {
        if (error && error->empty()) {
          *error = "unsupported param type in signature: " + fn.decl->params[i].type.name;
        }
        return false;
      }
      result << param;
      first = false;
    }
    result << ") -> " << ret << "\n";
  }
  for (const auto& imp : st.imports) {
    std::string ret = SigTypeNameFromType(imp.ret, st, error);
    if (ret.empty()) {
      if (error && error->empty()) *error = "unsupported return type in import signature";
      return false;
    }
    result << "  sig " << imp.sig_name << ": (";
    bool first = true;
    for (size_t i = 0; i < imp.params.size(); ++i) {
      if (!first) result << ", ";
      std::string param = SigTypeNameFromType(imp.params[i], st, error);
      if (param.empty()) {
        if (error && error->empty()) *error = "unsupported param type in import signature";
        return false;
      }
      result << param;
      first = false;
    }
    result << ") -> " << ret << "\n";
  }
  for (const auto& line : st.proc_sig_lines) {
    result << line << "\n";
  }

  if (!globals.empty()) {
    for (const auto* glob : globals) {
      std::string init_const_name;
      if (!AddGlobalInitConst(st, glob->name, glob->type, &init_const_name)) {
        if (error) *error = "global '" + glob->name + "' type has no default const init support";
        return false;
      }
    }
  }

  if (!st.const_lines.empty()) {
    result << "consts:\n";
    for (const auto& line : st.const_lines) {
      result << line << "\n";
    }
  }

  if (!globals.empty()) {
    result << "globals:\n";
    for (const auto* glob : globals) {
      std::string type_name = SigTypeNameFromType(glob->type, st, error);
      if (type_name.empty()) {
        if (error && error->empty()) *error = "unsupported global type: " + glob->type.name;
        return false;
      }
      result << "  global " << glob->name << " " << type_name;
      result << " init=" << "__ginit_" + glob->name;
      result << "\n";
    }
  }

  if (!st.imports.empty()) {
    result << "imports:\n";
    for (const auto& imp : st.imports) {
      result << "  import " << imp.name << " " << imp.module << " " << imp.symbol
             << " sig=" << imp.sig_name;
      if (imp.flags != 0) {
        result << " flags=" << imp.flags;
      }
      result << "\n";
    }
  }

  for (const auto& text : function_text) {
    result << text;
  }

  result << "entry " << entry_name << "\n";

  if (out) *out = result.str();
  return true;
}

} // namespace

bool EmitSir(const Program& program, std::string* out, std::string* error) {
  std::string validate_error;
  if (!ValidateProgram(program, &validate_error)) {
    if (error) *error = validate_error;
    return false;
  }
  return EmitProgramImpl(program, out, error);
}

bool EmitSirFromString(const std::string& text, std::string* out, std::string* error) {
  Program program;
  std::string parse_error;
  if (!ParseProgramFromString(text, &program, &parse_error)) {
    if (error) *error = parse_error;
    return false;
  }
  return EmitSir(program, out, error);
}

} // namespace Simple::Lang
