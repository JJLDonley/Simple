#include "test_utils.h"

#include <unordered_map>
#include <unordered_set>

#include "AST/lower_cast.h"
#include "CAST/parser.h"
#include "RAST/resolver.h"
#include "RAST/reserved_resolution.h"
#include "RAST/member_resolution.h"
#include "RAST/import_graph.h"
#include "RAST/symbol_table.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitRastResolvesFunctionSymbol() {
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : i32 () { return 1; }", &cast_program, &error)) {
    return false;
  }
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  const auto* symbol = Simple::Lang::RAST::LookupQualifiedSymbol(&resolved, "main");
  return symbol && symbol->kind == Simple::Lang::RAST::SymbolKind::Function;
}

bool LangRastResolverCollectsQualifiedSymbols() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 () { return self.v; }\n"
      "}\n"
      "Config :: namespace {\n"
      "  Max :: i32 = 42\n"
      "}\n"
      "Mode :: enum { Off = 0, On = 1 }\n"
      "main : i32 () { return Config.Max; }\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  return resolved.by_qualified_name.find("Box") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Box.v") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Box.score") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Config") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Config.Max") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Mode.On") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("main") != resolved.by_qualified_name.end();
}


bool LangRastResolverRejectsDuplicateQualifiedSymbols() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  v : i32\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  return error.find("duplicate symbol: Box.v") != std::string::npos;
}


bool LangRastResolverCollectsCallableScopes() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 (amount : i32) {\n"
      "    total : i32 = amount + self.v;\n"
      "    if (total > 0) { branch : i32 = total; return branch; }\n"
      "    return 0;\n"
      "  }\n"
      "}\n"
      "main : i32 () {\n"
      "  b : Box = { 1 };\n"
      "  return b.score(41);\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  return resolved.by_qualified_name.find("Box.score::self") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Box.score::param:amount") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Box.score::body.s0:total") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("Box.score::body.s1.if_then.s0:branch") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("main::body.s0:b") != resolved.by_qualified_name.end();
}


bool LangRastResolverCollectsSwitchBranchLocals() {
  const char* src =
      "main : i32 () {\n"
      "  mode : i32 = 1;\n"
      "  value : i32 = switch (mode) {\n"
      "    mode == 1 => { local : i32 = 42; return local }\n"
      "    default => return 0\n"
      "  };\n"
      "  return value;\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  return resolved.by_qualified_name.find("main::body.s0:mode") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("main::body.s1:value") != resolved.by_qualified_name.end() &&
         resolved.by_qualified_name.find("main::body.s1.init.switch0.s0:local") != resolved.by_qualified_name.end();
}


bool LangRastResolverDisambiguatesMemberRefs() {
  const char* src =
      "import Standard.IO\n"
      "import System.FFI\n"
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 () { return self.v; }\n"
      "}\n"
      "Config :: namespace {\n"
      "  Max :: i32 = 40\n"
      "}\n"
      "Mode :: enum { Off = 0, On = 1 }\n"
      "extern Ray.InitWindow : void (w : i32, h : i32)\n"
      "extern ffi.simple_add_i32 : i32 (a : i32, b : i32)\n"
      "glib :: i64 = System.FFI.Open(\"libffi.so\", ffi)\n"
      "UseGlobal : i32 () { return glib.simple_add_i32(3, 4); }\n"
      "main : i32 () {\n"
      "  Standard.IO.println(1);\n"
      "  Ray.InitWindow(1, 2);\n"
      "  lib : i64 = System.FFI.Open(\"libffi.so\", ffi);\n"
      "  dyn : i32 = lib.simple_add_i32(1, 2);\n"
      "  x : i32 = Config.Max;\n"
      "  m : Mode = Mode.On;\n"
      "  b : Box = { 2 };\n"
      "  return b.score() + x;\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  bool saw_self = false;
  bool saw_module = false;
  bool saw_enum = false;
  bool saw_artifact_method = false;
  bool saw_extern = false;
  bool saw_reserved = false;
  bool saw_dl_manifest = false;
  bool saw_global_dl_manifest = false;
  bool saw_artifact_receiver = false;
  bool saw_self_receiver = false;
  for (const auto& ref : resolved.member_refs) {
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::ArtifactField &&
        ref.qualified_name == "Box.v") {
      saw_self = true;
      saw_self_receiver = ref.receiver_type == "Box" &&
                          ref.receiver_symbol != Simple::Lang::RAST::kInvalidSymbolId &&
                          resolved.symbols[ref.receiver_symbol].kind == Simple::Lang::RAST::SymbolKind::Artifact;
    }
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::ModuleMember &&
        ref.qualified_name == "Config.Max") saw_module = true;
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::EnumMember &&
        ref.qualified_name == "Mode.On") saw_enum = true;
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::ArtifactMethod &&
        ref.qualified_name == "Box.score") {
      saw_artifact_method = true;
      saw_artifact_receiver = ref.receiver_type == "Box" &&
                              ref.receiver_symbol != Simple::Lang::RAST::kInvalidSymbolId &&
                              resolved.symbols[ref.receiver_symbol].kind == Simple::Lang::RAST::SymbolKind::Artifact;
    }
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::ExternSymbol &&
        ref.qualified_name == "Ray.InitWindow") saw_extern = true;
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::ReservedModuleFunction &&
        ref.qualified_name == "Standard.IO.println") saw_reserved = true;
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::DLManifestCall &&
        ref.qualified_name == "ffi.simple_add_i32" && ref.base == "lib") saw_dl_manifest = true;
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::DLManifestCall &&
        ref.qualified_name == "ffi.simple_add_i32" && ref.base == "glib") saw_global_dl_manifest = true;
  }
  return saw_self && saw_module && saw_enum && saw_artifact_method && saw_extern && saw_reserved &&
         saw_dl_manifest && saw_global_dl_manifest && saw_artifact_receiver && saw_self_receiver;
}


bool LangRastMemberLookupFindsDeclMembers() {
  Simple::Lang::AST::ModuleDecl module;
  module.name = "Math";
  Simple::Lang::AST::VarDecl module_var;
  module_var.name = "answer";
  module.variables.push_back(module_var);
  Simple::Lang::AST::FuncDecl module_fn;
  module_fn.name = "add";
  module.functions.push_back(module_fn);

  Simple::Lang::AST::ArtifactDecl artifact;
  artifact.name = "Point";
  Simple::Lang::AST::VarDecl field;
  field.name = "x";
  artifact.fields.push_back(field);
  Simple::Lang::AST::FuncDecl method;
  method.name = "move";
  artifact.methods.push_back(method);

  const auto module_members = Simple::Lang::RAST::ModuleMembers(&module);
  const bool saw_module_var = !module_members.empty() && module_members[0] == "answer";
  const bool saw_module_func = module_members.size() == 2 && module_members[1] == "add";
  const std::string suggestion = Simple::Lang::RAST::UnknownMemberErrorWithSuggestion(
      "Math", "answr", module_members);
  const std::string no_suggestion = Simple::Lang::RAST::UnknownMemberErrorWithSuggestion(
      "Math", "zzzzzz", module_members);

  return suggestion.find("did you mean 'answer'") != std::string::npos &&
         no_suggestion == "unknown module member: Standard.Math.zzzzzz" &&
         saw_module_var &&
         saw_module_func &&
         Simple::Lang::RAST::ModuleMembers(nullptr).empty() &&
         Simple::Lang::RAST::FindModuleVar(&module, "answer") == &module.variables[0] &&
         Simple::Lang::RAST::FindModuleFunc(&module, "add") == &module.functions[0] &&
         Simple::Lang::RAST::FindArtifactField(&artifact, "x") == &artifact.fields[0] &&
         Simple::Lang::RAST::FindArtifactMethod(&artifact, "move") == &artifact.methods[0] &&
         Simple::Lang::RAST::IsArtifactMemberName(&artifact, "x") &&
         Simple::Lang::RAST::IsArtifactMemberName(&artifact, "move") &&
         !Simple::Lang::RAST::IsArtifactMemberName(&artifact, "missing");
}

bool LangRastMemberResolutionRecordsMemberRefs() {
  Simple::Lang::RAST::ResolvedProgram program;
  Simple::Lang::RAST::AddResolvedMemberRef(&program,
                                           Simple::Lang::RAST::MemberRefKind::ModuleMember,
                                           "Math",
                                           "answer",
                                           "Standard.Math.answer",
                                           7);
  const auto* ref = Simple::Lang::RAST::ResolveMemberAccess(&program, "Math", "answer");
  return program.member_refs.size() == 1 && ref &&
         Simple::Lang::RAST::LookupResolvedMemberRef(&program, "Math", "answer") == ref &&
         ref->base == "Math" &&
         ref->member == "answer" &&
         ref->symbol == 7 &&
         !Simple::Lang::RAST::LookupResolvedMemberRef(&program, "Math", "missing") &&
         Simple::Lang::RAST::ClassifyMemberRefKind(Simple::Lang::RAST::MemberRefKind::Unknown,
                                                   Simple::Lang::RAST::SymbolKind::EnumMember) ==
             Simple::Lang::RAST::MemberRefKind::EnumMember;
}


bool LangRastReservedResolutionUsesNativeMetadata() {
  std::string native_module;
  const auto fs_members = Simple::Lang::RAST::ReservedModuleMembers("FS");
  const auto has_fs_member = [&fs_members](const std::string& name) {
    for (const auto& member : fs_members) {
      if (member == name) return true;
    }
    return false;
  };
  Simple::Lang::AST::TypeRef reserved_var_type;
  std::unordered_set<std::string> reserved_imports = {"FS", "DL"};
  std::unordered_map<std::string, std::string> reserved_aliases = {{"Files", "FS"}};
  std::string resolved_reserved;
  const bool resolves_reserved_alias = Simple::Lang::RAST::ResolveReservedModuleName(
      reserved_imports, reserved_aliases, "Files", &resolved_reserved) && resolved_reserved == "FS";
  const bool resolves_reserved_direct = Simple::Lang::RAST::ResolveReservedModuleName(
      reserved_imports, reserved_aliases, "DL", &resolved_reserved) && resolved_reserved == "DL";
  const bool enables_reserved_alias = Simple::Lang::RAST::IsReservedModuleEnabled(
      reserved_imports, reserved_aliases, "Files");
  Simple::Lang::AST::Expr io_base;
  io_base.kind = Simple::Lang::AST::ExprKind::Identifier;
  io_base.text = "Printer";
  Simple::Lang::AST::Expr io_print;
  io_print.kind = Simple::Lang::AST::ExprKind::Member;
  io_print.op = ".";
  io_print.text = "println";
  io_print.children.push_back(io_base);
  reserved_imports.insert("StandardIO");
  reserved_aliases["Printer"] = "StandardIO";
  const bool recognizes_io_print = Simple::Lang::RAST::IsIoPrintCallExpr(
      io_print, reserved_imports, reserved_aliases);
  io_print.text = "write";
  const bool rejects_non_print = !Simple::Lang::RAST::IsIoPrintCallExpr(
      io_print, reserved_imports, reserved_aliases);
  Simple::Lang::AST::Expr dl_base;
  dl_base.kind = Simple::Lang::AST::ExprKind::Identifier;
  dl_base.text = "Dyn";
  Simple::Lang::AST::Expr dl_member;
  dl_member.kind = Simple::Lang::AST::ExprKind::Member;
  dl_member.op = ".";
  dl_member.text = "Open";
  dl_member.children.push_back(dl_base);
  Simple::Lang::AST::Expr dl_call;
  dl_call.kind = Simple::Lang::AST::ExprKind::Call;
  dl_call.children.push_back(dl_member);
  reserved_aliases["Dyn"] = "DL";
  const bool recognizes_dl_open = Simple::Lang::RAST::IsCoreDlOpenCallExpr(
      dl_call, reserved_imports, reserved_aliases);
  Simple::Lang::AST::ExternDecl manifest_ext;
  manifest_ext.module = "ffi";
  manifest_ext.name = "add";
  std::unordered_map<std::string, std::unordered_map<std::string, const Simple::Lang::AST::ExternDecl*>> externs_by_module;
  externs_by_module["ffi"]["add"] = &manifest_ext;
  Simple::Lang::AST::Expr path_arg;
  path_arg.kind = Simple::Lang::AST::ExprKind::Literal;
  Simple::Lang::AST::Expr manifest_arg;
  manifest_arg.kind = Simple::Lang::AST::ExprKind::Identifier;
  manifest_arg.text = "ffi";
  dl_call.args.push_back(path_arg);
  dl_call.args.push_back(manifest_arg);
  std::string manifest_module;
  const bool extracts_manifest = Simple::Lang::RAST::GetDlOpenManifestModule(
      dl_call, reserved_imports, reserved_aliases, externs_by_module, &manifest_module) &&
      manifest_module == "ffi";
  dl_call.children[0].text = "Sym";
  const bool rejects_dl_sym = !Simple::Lang::RAST::IsCoreDlOpenCallExpr(
      dl_call, reserved_imports, reserved_aliases) &&
      !Simple::Lang::RAST::GetDlOpenManifestModule(
          dl_call, reserved_imports, reserved_aliases, externs_by_module, &manifest_module);
  const bool math_pi_type = Simple::Lang::RAST::GetReservedModuleVarType("Math", "PI", &reserved_var_type) &&
                            reserved_var_type.name == "f64";
  const bool os_flag_type = Simple::Lang::RAST::GetReservedModuleVarType("OS", "has_dl", &reserved_var_type) &&
                            reserved_var_type.name == "bool";
  return resolves_reserved_alias &&
         resolves_reserved_direct &&
         enables_reserved_alias &&
         recognizes_io_print &&
         rejects_non_print &&
         recognizes_dl_open &&
         extracts_manifest &&
         rejects_dl_sym &&
         math_pi_type &&
         os_flag_type &&
         !Simple::Lang::RAST::GetReservedModuleVarType("Math", "missing", &reserved_var_type) &&
         has_fs_member("readText") &&
         has_fs_member("open") &&
         Simple::Lang::RAST::ReservedModuleMembers("Missing").empty() &&
         Simple::Lang::RAST::NativeModuleNameForReserved("FS", &native_module) &&
         native_module == "System.fs" &&
         Simple::Lang::RAST::IsIoPrintName("print") &&
         Simple::Lang::RAST::IsIoPrintName("println") &&
         !Simple::Lang::RAST::IsIoPrintName("write") &&
         Simple::Lang::RAST::IsReservedModuleFunction("FS", "open") &&
         Simple::Lang::RAST::IsReservedModuleFunction("Json", "parse") &&
         Simple::Lang::RAST::NormalizeDlMemberName("Open") == "open";
}


bool LangRastSymbolTableAddsAndRejectsDuplicates() {
  Simple::Lang::RAST::ResolvedProgram program;
  std::string error;
  const bool added = Simple::Lang::RAST::AddSymbol(&program,
                                                   Simple::Lang::RAST::SymbolKind::Artifact,
                                                   "Thing",
                                                   "Thing",
                                                   Simple::Lang::RAST::kInvalidSymbolId,
                                                   &error);
  const auto found = Simple::Lang::RAST::FindQualifiedSymbol(&program,
                                                            "Thing",
                                                            Simple::Lang::RAST::SymbolKind::Artifact);
  const auto* lookup_by_id = Simple::Lang::RAST::LookupSymbol(&program, found);
  const auto* lookup_by_name = Simple::Lang::RAST::LookupQualifiedSymbol(&program, "Thing");
  const bool duplicate_rejected = !Simple::Lang::RAST::AddSymbol(&program,
                                                                 Simple::Lang::RAST::SymbolKind::Artifact,
                                                                 "Thing",
                                                                 "Thing",
                                                                 Simple::Lang::RAST::kInvalidSymbolId,
                                                                 &error);
  return added && found == 0 && lookup_by_id && lookup_by_id->name == "Thing" &&
         lookup_by_name == lookup_by_id && duplicate_rejected &&
         error.find("duplicate symbol: Thing") != std::string::npos;
}


bool LangRastAllowsTypeInvalidPrograms() {
  const char* src = "main : i32 () { return \"not an i32\" }";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  return Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error);
}


bool LangRastDeclarationResolutionFindsDeclSymbols() {
  const char* src =
      "Point :: artifact { x : i32; }\n"
      "main : i32 () { return 0 }";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  const auto* artifact_symbol = Simple::Lang::RAST::ResolveDeclarationSymbol(&resolved, ast_program.decls[0]);
  const auto* function_symbol = Simple::Lang::RAST::ResolveDeclarationSymbol(&resolved, ast_program.decls[1]);
  return artifact_symbol && artifact_symbol->kind == Simple::Lang::RAST::SymbolKind::Artifact &&
         artifact_symbol->qualified_name == "Point" &&
         function_symbol && function_symbol->kind == Simple::Lang::RAST::SymbolKind::Function &&
         function_symbol->qualified_name == "main";
}


bool LangRastImportGraphResolvesReservedAliases() {
  const char* src =
      "import Standard.FS as FileSystem\n"
      "main : void () {}";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  std::string canonical;
  const auto imports = Simple::Lang::RAST::ResolveReservedImports(&ast_program);
  return imports.size() == 1 && imports[0].alias == "FileSystem" &&
         imports[0].canonical_module == "FS" &&
         Simple::Lang::RAST::ResolveReservedImportAlias(&ast_program, "FileSystem", &canonical) &&
         canonical == "FS" &&
         !Simple::Lang::RAST::ResolveReservedImportAlias(&ast_program, "Missing", &canonical);
}

bool LangRastImportGraphChecksUsingPriorImport() {
  std::unordered_map<std::string, std::string> aliases;
  aliases["FS"] = "FileSystem";
  std::string error;
  if (!Simple::Lang::RAST::CheckUsingImportHasPriorAlias("FS", aliases, &error)) return false;
  if (Simple::Lang::RAST::CheckUsingImportHasPriorAlias("Time", aliases, &error)) return false;
  return error.find("using requires prior import: Time") != std::string::npos;
}



const TestCase kLangRastTests[] = {
  {"lang_split_rast_resolves_function_symbol", LangSplitRastResolvesFunctionSymbol},
  {"lang_rast_member_lookup_finds_decl_members", LangRastMemberLookupFindsDeclMembers},
  {"lang_rast_member_resolution_records_member_refs", LangRastMemberResolutionRecordsMemberRefs},
  {"lang_rast_reserved_resolution_uses_native_metadata", LangRastReservedResolutionUsesNativeMetadata},
  {"lang_rast_symbol_table_adds_and_rejects_duplicates", LangRastSymbolTableAddsAndRejectsDuplicates},
  {"lang_rast_allows_type_invalid_programs", LangRastAllowsTypeInvalidPrograms},
  {"lang_rast_declaration_resolution_finds_decl_symbols", LangRastDeclarationResolutionFindsDeclSymbols},
  {"lang_rast_import_graph_resolves_reserved_aliases", LangRastImportGraphResolvesReservedAliases},
  {"lang_rast_import_graph_checks_using_prior_import", LangRastImportGraphChecksUsingPriorImport},
  {"lang_rast_resolver_collects_qualified_symbols", LangRastResolverCollectsQualifiedSymbols},
  {"lang_rast_resolver_rejects_duplicate_qualified_symbols", LangRastResolverRejectsDuplicateQualifiedSymbols},
  {"lang_rast_resolver_collects_callable_scopes", LangRastResolverCollectsCallableScopes},
  {"lang_rast_resolver_collects_switch_branch_locals", LangRastResolverCollectsSwitchBranchLocals},
  {"lang_rast_resolver_disambiguates_member_refs", LangRastResolverDisambiguatesMemberRefs},
};

const TestSection kLangRastSections[] = {
  {"lang_rast", kLangRastTests, sizeof(kLangRastTests) / sizeof(kLangRastTests[0])},
};

} // namespace

const TestSection* GetLangRastSections(size_t* count) {
  if (count) *count = sizeof(kLangRastSections) / sizeof(kLangRastSections[0]);
  return kLangRastSections;
}

} // namespace Simple::VM::Tests
