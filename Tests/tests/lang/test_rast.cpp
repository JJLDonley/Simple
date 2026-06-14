#include "test_utils.h"

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
      "Box :: Artifact {\n"
      "  v : i32\n"
      "  score : i32 () { return self.v; }\n"
      "}\n"
      "Config :: Namespace {\n"
      "  Max :: i32 = 42\n"
      "}\n"
      "Mode :: Enum { Off = 0, On = 1 }\n"
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
      "Box :: Artifact {\n"
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
      "Box :: Artifact {\n"
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
      "import IO\n"
      "import DL\n"
      "Box :: Artifact {\n"
      "  v : i32\n"
      "  score : i32 () { return self.v; }\n"
      "}\n"
      "Config :: Namespace {\n"
      "  Max :: i32 = 40\n"
      "}\n"
      "Mode :: Enum { Off = 0, On = 1 }\n"
      "extern Ray.InitWindow : void (w : i32, h : i32)\n"
      "extern ffi.simple_add_i32 : i32 (a : i32, b : i32)\n"
      "glib :: i64 = DL.Open(\"libffi.so\", ffi)\n"
      "UseGlobal : i32 () { return glib.simple_add_i32(3, 4); }\n"
      "main : i32 () {\n"
      "  IO.println(1);\n"
      "  Ray.InitWindow(1, 2);\n"
      "  lib : i64 = DL.Open(\"libffi.so\", ffi);\n"
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
        ref.qualified_name == "IO.println") saw_reserved = true;
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::DLManifestCall &&
        ref.qualified_name == "ffi.simple_add_i32" && ref.base == "lib") saw_dl_manifest = true;
    if (ref.kind == Simple::Lang::RAST::MemberRefKind::DLManifestCall &&
        ref.qualified_name == "ffi.simple_add_i32" && ref.base == "glib") saw_global_dl_manifest = true;
  }
  return saw_self && saw_module && saw_enum && saw_artifact_method && saw_extern && saw_reserved &&
         saw_dl_manifest && saw_global_dl_manifest && saw_artifact_receiver && saw_self_receiver;
}


bool LangRastMemberResolutionRecordsMemberRefs() {
  Simple::Lang::RAST::ResolvedProgram program;
  Simple::Lang::RAST::AddResolvedMemberRef(&program,
                                           Simple::Lang::RAST::MemberRefKind::ModuleMember,
                                           "Math",
                                           "answer",
                                           "Math.answer",
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
  return Simple::Lang::RAST::NativeModuleNameForReserved("FS", &native_module) &&
         native_module == "System.fs" &&
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
      "import FS as FileSystem\n"
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



const TestCase kLangRastTests[] = {
  {"lang_split_rast_resolves_function_symbol", LangSplitRastResolvesFunctionSymbol},
  {"lang_rast_member_resolution_records_member_refs", LangRastMemberResolutionRecordsMemberRefs},
  {"lang_rast_reserved_resolution_uses_native_metadata", LangRastReservedResolutionUsesNativeMetadata},
  {"lang_rast_symbol_table_adds_and_rejects_duplicates", LangRastSymbolTableAddsAndRejectsDuplicates},
  {"lang_rast_allows_type_invalid_programs", LangRastAllowsTypeInvalidPrograms},
  {"lang_rast_declaration_resolution_finds_decl_symbols", LangRastDeclarationResolutionFindsDeclSymbols},
  {"lang_rast_import_graph_resolves_reserved_aliases", LangRastImportGraphResolvesReservedAliases},
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
