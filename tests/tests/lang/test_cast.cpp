#include "test_utils.h"

#include "CAST/parser.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitCastParsesFunctionDecl() {
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : i32 () { return 7; }", &program, &error)) {
    return false;
  }
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  return decl.kind == Simple::Lang::DeclKind::Function &&
         decl.func.name == "main" &&
         decl.func.return_type.name == "i32" &&
         decl.func.body.size() == 1 &&
         decl.func.body[0].kind == Simple::Lang::StmtKind::Return;
}

bool LangCastParserModuleParsesArtifactSwitch() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 () {\n"
      "    return switch (self.v) {\n"
      "      self.v > 0 => { local : i32 = 1; return local }\n"
      "      default => return 0\n"
      "    };\n"
      "  }\n"
      "}\n";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  if (program.decls[0].kind != Simple::Lang::DeclKind::Artifact) return false;
  if (program.decls[0].artifact.methods.size() != 1) return false;
  const auto& body = program.decls[0].artifact.methods[0].body;
  return body.size() == 1 &&
         body[0].kind == Simple::Lang::StmtKind::Return &&
         body[0].expr.kind == Simple::Lang::ExprKind::Switch;
}


bool LangParseMissingSemicolonSameLine() {
  const char* src = "main : i32 () { x : i32 = 1 y : i32 = 2 }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  return true;
}


bool LangParseErrorIncludesLocation() {
  const char* src = "main : i32 () { $ }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  return error.find(':') != std::string::npos;
}


bool LangParseArtifactCommaDiagnosticHint() {
  const char* src = "Point :: artifact { x : i32, y : i32 }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  return error.find("use newline or ';' between members") != std::string::npos;
}


bool LangParseReservedKeywordParameterDiagnosticHint() {
  const char* src = "f : void (artifact: i32) { return; }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  return error.find("cannot be used as identifier") != std::string::npos;
}


bool LangParsesTypeLiterals() {
  Simple::Lang::TypeRef type;
  std::string error;
  if (!Simple::Lang::CAST::ParseTypeFromString("i32", &type, &error)) return false;
  if (type.name != "i32") return false;
  if (!type.dims.empty()) return false;

  if (!Simple::Lang::CAST::ParseTypeFromString("i8", &type, &error)) return false;
  if (type.name != "i8") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("i16", &type, &error)) return false;
  if (type.name != "i16") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("i64", &type, &error)) return false;
  if (type.name != "i64") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("u8", &type, &error)) return false;
  if (type.name != "u8") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("u16", &type, &error)) return false;
  if (type.name != "u16") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("u32", &type, &error)) return false;
  if (type.name != "u32") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("u64", &type, &error)) return false;
  if (type.name != "u64") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("f32", &type, &error)) return false;
  if (type.name != "f32") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("f64", &type, &error)) return false;
  if (type.name != "f64") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("bool", &type, &error)) return false;
  if (type.name != "bool") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("char", &type, &error)) return false;
  if (type.name != "char") return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("string", &type, &error)) return false;
  if (type.name != "string") return false;

  Simple::Lang::TypeRef arr;
  if (!Simple::Lang::CAST::ParseTypeFromString("i32{10}[]", &arr, &error)) return false;
  if (arr.dims.size() != 2) return false;
  if (!arr.dims[0].has_size || arr.dims[0].size != 10) return false;
  if (!arr.dims[1].is_list) return false;

  Simple::Lang::TypeRef list_type;
  if (!Simple::Lang::CAST::ParseTypeFromString("i32[]", &list_type, &error)) return false;
  if (list_type.dims.size() != 1) return false;
  if (!list_type.dims[0].is_list) return false;

  Simple::Lang::TypeRef list2_type;
  if (!Simple::Lang::CAST::ParseTypeFromString("i32[][]", &list2_type, &error)) return false;
  if (list2_type.dims.size() != 2) return false;
  if (!list2_type.dims[0].is_list) return false;
  if (!list2_type.dims[1].is_list) return false;

  Simple::Lang::TypeRef hex_arr;
  if (!Simple::Lang::CAST::ParseTypeFromString("i32{0x10}", &hex_arr, &error)) return false;
  if (hex_arr.dims.size() != 1) return false;
  if (!hex_arr.dims[0].has_size || hex_arr.dims[0].size != 16) return false;

  Simple::Lang::TypeRef bin_arr;
  if (!Simple::Lang::CAST::ParseTypeFromString("i32{0b1010}", &bin_arr, &error)) return false;
  if (bin_arr.dims.size() != 1) return false;
  if (!bin_arr.dims[0].has_size || bin_arr.dims[0].size != 10) return false;

  Simple::Lang::TypeRef generic;
  if (!Simple::Lang::CAST::ParseTypeFromString("Map<string, i32>", &generic, &error)) return false;
  if (generic.type_args.size() != 2) return false;
  if (generic.type_args[0].name != "string") return false;
  if (generic.type_args[1].name != "i32") return false;

  Simple::Lang::TypeRef nested_generic_list;
  if (!Simple::Lang::CAST::ParseTypeFromString(
          "Holder<Option<string>>[]", &nested_generic_list, &error)) {
    return false;
  }
  if (nested_generic_list.name != "Holder" || nested_generic_list.dims.size() != 1 ||
      !nested_generic_list.dims[0].is_list || nested_generic_list.type_args.size() != 1) {
    return false;
  }
  const auto& nested_option = nested_generic_list.type_args[0];
  if (nested_option.name != "Option" || !nested_option.dims.empty() ||
      nested_option.type_args.size() != 1 || nested_option.type_args[0].name != "string") {
    return false;
  }

  Simple::Lang::TypeRef proc;
  if (!Simple::Lang::CAST::ParseTypeFromString("fn bool (i32, string)", &proc, &error)) return false;
  if (!proc.is_proc) return false;
  if (proc.proc_params.size() != 2) return false;
  if (proc.proc_params[0].name != "i32") return false;
  if (proc.proc_params[1].name != "string") return false;
  if (!proc.proc_return) return false;
  if (proc.proc_return->name != "bool") return false;

  Simple::Lang::TypeRef fn_ret;
  if (!Simple::Lang::CAST::ParseTypeFromString("fn i32 ()", &fn_ret, &error)) return false;
  if (!fn_ret.is_proc) return false;
  if (!fn_ret.proc_return) return false;
  if (fn_ret.proc_return->name != "i32") return false;
  if (!fn_ret.proc_params.empty()) return false;

  Simple::Lang::TypeRef ptr;
  if (!Simple::Lang::CAST::ParseTypeFromString("i32*", &ptr, &error)) return false;
  if (ptr.name != "i32" || ptr.pointer_depth != 1) return false;
  if (!Simple::Lang::CAST::ParseTypeFromString("void**", &ptr, &error)) return false;
  if (ptr.name != "void" || ptr.pointer_depth != 2) return false;

  return true;
}


bool LangRejectsBadArraySize() {
  Simple::Lang::TypeRef type;
  std::string error;
  return !Simple::Lang::CAST::ParseTypeFromString("i32[foo]", &type, &error);
}


bool LangParsesFuncDecl() {
  const char* src = "add : i32 (a : i32, b :: i32) { return a + b; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Function) return false;
  if (decl.func.name != "add") return false;
  if (decl.func.return_type.name != "i32") return false;
  if (decl.func.params.size() != 2) return false;
  if (decl.func.params[0].name != "a") return false;
  if (decl.func.params[0].mutability != Simple::Lang::Mutability::Mutable) return false;
  if (decl.func.params[1].name != "b") return false;
  if (decl.func.params[1].mutability != Simple::Lang::Mutability::Immutable) return false;
  return true;
}


bool LangParsesFnKeywordDecl() {
  const char* src = "fn main :: void () { return; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Function) return false;
  if (decl.func.name != "main") return false;
  if (decl.func.return_type.name != "void") return false;
  if (decl.func.body.empty()) return false;
  if (decl.func.body[0].kind != Simple::Lang::StmtKind::Return) return false;
  if (decl.func.body[0].has_return_expr) return false;
  return true;
}


bool LangParserRecoversInBlock() {
  const char* src = "main : void () { +; x : i32 = 1; }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Function) return false;
  bool found_var = false;
  for (const auto& stmt : decl.func.body) {
    if (stmt.kind == Simple::Lang::StmtKind::VarDecl && stmt.var_decl.name == "x") {
      found_var = true;
      break;
    }
  }
  return found_var;
}


bool LangParsesVarDecl() {
  const char* src = "count :: i32 = 42;";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Variable) return false;
  if (decl.var.name != "count") return false;
  if (decl.var.mutability != Simple::Lang::Mutability::Immutable) return false;
  if (decl.var.type.name != "i32") return false;
  return true;
}


bool LangParsesVarDeclNoInit() {
  const char* src = "count :: i32;";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Variable) return false;
  if (decl.var.name != "count") return false;
  return true;
}


bool LangParsesLocalVarDeclNoInit() {
  const char* src = "main : void () { x : i32; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::VarDecl) return false;
  if (stmt.var_decl.name != "x") return false;
  if (stmt.var_decl.has_init_expr) return false;
  return true;
}


bool LangParsesArtifactDecl() {
  const char* src = "Point :: artifact { x : f32; y :: f32; len : i32 () { return 1; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Artifact) return false;
  if (decl.artifact.name != "Point") return false;
  if (decl.artifact.fields.size() != 2) return false;
  if (decl.artifact.methods.size() != 1) return false;
  return true;
}


bool LangParsesDataDecl() {
  const char* src = "Point :: data { x : f32; y :: f32 }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Artifact) return false;
  if (decl.artifact.name != "Point") return false;
  if (!decl.artifact.is_data) return false;
  if (decl.artifact.fields.size() != 2) return false;
  if (!decl.artifact.methods.empty()) return false;
  return true;
}


bool LangParsesArtifactDeclCapitalized() {
  const char* src = "Point :: artifact { x : f32; y :: f32; len : i32 () { return 1; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Artifact) return false;
  if (decl.artifact.name != "Point") return false;
  if (decl.artifact.fields.size() != 2) return false;
  if (decl.artifact.methods.size() != 1) return false;
  return true;
}


bool LangParsesModuleDecl() {
  const char* src = "Math :: namespace { scale : i32 = 2; add : i32 (a : i32, b : i32) { return a + b; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Module) return false;
  if (decl.module.name != "Math") return false;
  if (decl.module.variables.size() != 1) return false;
  if (decl.module.functions.size() != 1) return false;
  return true;
}


bool LangParsesModuleDeclCapitalized() {
  const char* src = "Math :: namespace { scale : i32 = 2; add : i32 (a : i32, b : i32) { return a + b; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Module) return false;
  if (decl.module.name != "Math") return false;
  if (decl.module.variables.size() != 1) return false;
  if (decl.module.functions.size() != 1) return false;
  return true;
}


bool LangParsesImportDecl() {
  const char* src = "import \"raylib\"";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Import) return false;
  if (decl.import_decl.path != "raylib") return false;
  if (decl.import_decl.has_alias) return false;
  return true;
}


bool LangParsesImportDeclAlias() {
  const char* src = "import \"raylib\" as Ray";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Import) return false;
  if (decl.import_decl.path != "raylib") return false;
  if (!decl.import_decl.has_alias) return false;
  if (decl.import_decl.alias != "Ray") return false;
  return true;
}


bool LangParsesImportDeclUnquotedPath() {
  const char* src = "import Standard.IO";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Import) return false;
  if (decl.import_decl.path != "Standard.IO") return false;
  if (decl.import_decl.has_alias) return false;
  return true;
}


bool LangParsesExternDecl() {
  const char* src = "extern Ray.InitWindow : void (w : i32, h : i32)";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Extern) return false;
  if (!decl.ext.has_module) return false;
  if (decl.ext.module != "Ray") return false;
  if (decl.ext.name != "InitWindow") return false;
  if (decl.ext.params.size() != 2) return false;
  if (decl.ext.params[0].name != "w") return false;
  if (decl.ext.params[1].name != "h") return false;
  return true;
}


bool LangParsesEnumDecl() {
  const char* src =
    "Status :: enum { Pending = 1, Active = 2 }"
    "Color :: enum { Red, Green, Blue }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 2) return false;
  const auto& status = program.decls[0];
  if (status.kind != Simple::Lang::DeclKind::Enum) return false;
  if (status.enm.name != "Status") return false;
  if (status.enm.members.size() != 2) return false;
  if (!status.enm.members[0].has_value) return false;
  if (status.enm.members[0].value_text != "1") return false;
  if (!status.enm.members[1].has_value) return false;
  const auto& color = program.decls[1];
  if (color.kind != Simple::Lang::DeclKind::Enum) return false;
  if (color.enm.name != "Color") return false;
  if (color.enm.members.size() != 3) return false;
  if (color.enm.members[0].has_value) return false;
  return true;
}


bool LangParsesEnumDeclCapitalized() {
  const char* src =
    "Status :: enum { Pending = 1, Active = 2 }"
    "Color :: enum { Red, Green, Blue }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 2) return false;
  const auto& status = program.decls[0];
  if (status.kind != Simple::Lang::DeclKind::Enum) return false;
  if (status.enm.name != "Status") return false;
  if (status.enm.members.size() != 2) return false;
  const auto& color = program.decls[1];
  if (color.kind != Simple::Lang::DeclKind::Enum) return false;
  if (color.enm.name != "Color") return false;
  if (color.enm.members.size() != 3) return false;
  if (color.enm.members[0].has_value) return false;
  return true;
}

bool LangParsesReturnExpr() {
  const char* src = "main : i32 () { return 1 + 2 * 3; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Function) return false;
  if (decl.func.body.size() != 1) return false;
  if (decl.func.body[0].kind != Simple::Lang::StmtKind::Return) return false;
  const auto& expr = decl.func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  if (expr.op != "+") return false;
  return true;
}


bool LangParsesCallAndMember() {
  const char* src = "main : i32 () { return foo(1, 2).bar + 3; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& decl = program.decls[0];
  const auto& expr = decl.func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  const auto& left = expr.children[0];
  if (left.kind != Simple::Lang::ExprKind::Member) return false;
  return true;
}


bool LangParsesSelf() {
  const char* src = "Point :: artifact { x : i32; get : i32 () { return self.x; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Artifact) return false;
  if (decl.artifact.methods.empty()) return false;
  const auto& stmt = decl.artifact.methods[0].body[0];
  if (stmt.kind != Simple::Lang::StmtKind::Return) return false;
  const auto& expr = stmt.expr;
  if (expr.kind != Simple::Lang::ExprKind::Member) return false;
  if (expr.children.empty()) return false;
  if (expr.children[0].kind != Simple::Lang::ExprKind::Identifier) return false;
  if (expr.children[0].text != "self") return false;
  return true;
}


bool LangParsesQualifiedMember() {
  const char* src = "main : i32 () { return Standard.Math.PI; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& expr = program.decls[0].func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Member) return false;
  if (expr.op != ".") return false;
  if (expr.text != "PI") return false;
  return true;
}


bool LangRejectsDoubleColonMember() {
  const char* src = "main : i32 () { return Math::PI; }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  return true;
}


bool LangParsesComparisons() {
  const char* src = "main : bool () { return 1 + 2 * 3 == 7 && 4 < 5; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& expr = program.decls[0].func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  if (expr.op != "&&") return false;
  return true;
}


bool LangParsesBitwisePrecedence() {
  const char* src = "main : i32 () { return 1 | 2 ^ 3 & 4 << 1; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& expr = program.decls[0].func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  if (expr.op != "|") return false;
  const auto& rhs = expr.children[1];
  if (rhs.kind != Simple::Lang::ExprKind::Binary || rhs.op != "^") return false;
  const auto& rhs_rhs = rhs.children[1];
  if (rhs_rhs.kind != Simple::Lang::ExprKind::Binary || rhs_rhs.op != "&") return false;
  const auto& shift = rhs_rhs.children[1];
  if (shift.kind != Simple::Lang::ExprKind::Binary || shift.op != "<<") return false;
  return true;
}


bool LangParsesArrayListAndIndex() {
  const char* src = "main : i32 () { return [1,2,3][0] + [][0]; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& expr = program.decls[0].func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  const auto& left = expr.children[0];
  if (left.kind != Simple::Lang::ExprKind::Index) return false;
  const auto& list_index = expr.children[1];
  if (list_index.kind != Simple::Lang::ExprKind::Index) return false;
  return true;
}


bool LangParsesDelimitedStringLiterals() {
  const char* src =
      "Pair :: artifact { left : string; right : string }\n"
      "main : void () { values : string[] = [\"alpha\", \"beta\"]; "
      "pair : Pair = { \"gamma\", \"delta\" }; "
      "named : Pair = { .left = \"epsilon\", .right = \"zeta\" }; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 2 || program.decls[1].func.body.size() != 3) return false;
  const auto& list = program.decls[1].func.body[0].var_decl.init_expr;
  const auto& artifact = program.decls[1].func.body[1].var_decl.init_expr;
  const auto& named = program.decls[1].func.body[2].var_decl.init_expr;
  if (list.kind != Simple::Lang::ExprKind::ListLiteral || list.children.size() != 2) return false;
  if (artifact.kind != Simple::Lang::ExprKind::ArtifactLiteral || artifact.children.size() != 2) {
    return false;
  }
  return list.children[0].kind == Simple::Lang::ExprKind::Literal &&
         list.children[1].kind == Simple::Lang::ExprKind::Literal &&
         artifact.children[0].kind == Simple::Lang::ExprKind::Literal &&
         artifact.children[1].kind == Simple::Lang::ExprKind::Literal &&
         named.field_values.size() == 2 &&
         named.field_values[0].kind == Simple::Lang::ExprKind::Literal &&
         named.field_values[1].kind == Simple::Lang::ExprKind::Literal;
}


bool LangParsesArtifactLiteral() {
  const char* src = "main : void () { foo({ .x = 1, .y = 2 }); }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::Expr) return false;
  if (stmt.expr.kind != Simple::Lang::ExprKind::Call) return false;
  if (stmt.expr.args.size() != 1) return false;
  const auto& arg = stmt.expr.args[0];
  if (arg.kind != Simple::Lang::ExprKind::ArtifactLiteral) return false;
  if (!arg.children.empty()) return false;
  if (arg.field_names.size() != 2) return false;
  if (arg.field_values.size() != 2) return false;
  if (arg.field_names[0] != "x") return false;
  if (arg.field_names[1] != "y") return false;
  return true;
}


bool LangParsesFnLiteral() {
  const char* src = "main : void () { f : fn i32 (x : i32) = (x) { return x; }; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& body = program.decls[0].func.body;
  if (body.empty()) return false;
  if (body[0].kind != Simple::Lang::StmtKind::VarDecl) return false;
  if (!body[0].var_decl.has_init_expr) return false;
  const auto& init = body[0].var_decl.init_expr;
  if (init.kind != Simple::Lang::ExprKind::FnLiteral) return false;
  if (init.fn_params.size() != 1) return false;
  if (init.fn_body_tokens.empty()) return false;
  return true;
}


bool LangParsesFnShorthandLiteralBinding() {
  const char* src = "main : void () { f : fn i32 (a : i32, b : i32) = (a, b) { return a + b; }; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& body = program.decls[0].func.body;
  if (body.empty()) return false;
  if (body[0].kind != Simple::Lang::StmtKind::VarDecl) return false;
  if (!body[0].var_decl.type.is_proc) return false;
  if (!body[0].var_decl.type.proc_return) return false;
  if (body[0].var_decl.type.proc_return->name != "i32") return false;
  if (body[0].var_decl.type.proc_params.size() != 2) return false;
  if (body[0].var_decl.type.proc_params[0].name != "i32") return false;
  if (body[0].var_decl.type.proc_params[1].name != "i32") return false;
  if (!body[0].var_decl.has_init_expr) return false;
  if (body[0].var_decl.init_expr.kind != Simple::Lang::ExprKind::FnLiteral) return false;
  if (body[0].var_decl.init_expr.fn_params.size() != 2) return false;
  return true;
}


bool LangParsesAssignments() {
  const char* src = "main : i32 () { x : i32 = 1; x += 2; x = x * 3; return x; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& body = program.decls[0].func.body;
  if (body.size() < 3) return false;
  if (body[1].kind != Simple::Lang::StmtKind::Assign) return false;
  if (body[1].assign_op != "+=") return false;
  if (body[2].kind != Simple::Lang::StmtKind::Assign) return false;
  if (body[2].assign_op != "=") return false;
  return true;
}


bool LangParsesIncDec() {
  const char* src = "main : void () { x++; ++x; x--; --x; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& body = program.decls[0].func.body;
  if (body.size() != 4) return false;
  for (const auto& stmt : body) {
    if (stmt.kind != Simple::Lang::StmtKind::Expr) return false;
    if (stmt.expr.kind != Simple::Lang::ExprKind::Unary) return false;
  }
  return true;
}


bool LangParsesIfChain() {
  const char* src = "main : i32 () { |> (true) { return 1; } |> default { return 2; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::IfChain) return false;
  if (stmt.if_branches.size() != 1) return false;
  if (stmt.else_branch.empty()) return false;
  return true;
}


bool LangParsesIfElse() {
  const char* src = "main : i32 () { if (x < 1) { return 1; } else { return 2; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::IfStmt) return false;
  if (stmt.if_then.size() != 1) return false;
  if (stmt.if_else.size() != 1) return false;
  return true;
}


bool LangParsesWhileLoop() {
  const char* src = "main : void () { while (x < 10) { x = x + 1; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::WhileLoop) return false;
  return true;
}


bool LangParsesBreakSkip() {
  const char* src = "main : void () { while (true) { break; skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& loop = program.decls[0].func.body[0];
  if (loop.kind != Simple::Lang::StmtKind::WhileLoop) return false;
  if (loop.loop_body.size() != 2) return false;
  if (loop.loop_body[0].kind != Simple::Lang::StmtKind::Break) return false;
  if (loop.loop_body[1].kind != Simple::Lang::StmtKind::Skip) return false;
  return true;
}


bool LangParsesForLoop() {
  const char* src = "main : void () { for (i : i32 = 0; i < 10; i = i + 1) { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  return true;
}


bool LangParsesForLoopPostInc() {
  const char* src = "main : void () { for (i : i32 = 0; i < 10; i++) { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (stmt.loop_step.kind != Simple::Lang::ExprKind::Unary) return false;
  return true;
}


bool LangParsesForLoopRange() {
  const char* src = "main : void () { for (i : i32 = 0; i <= 10; i++) { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (!stmt.has_loop_var_decl) return false;
  if (stmt.loop_cond.kind != Simple::Lang::ExprKind::Binary) return false;
  if (stmt.loop_cond.op != "<=") return false;
  if (stmt.loop_step.kind != Simple::Lang::ExprKind::Unary) return false;
  return true;
}


bool LangParsesForLoopRangeDefaultType() {
  const char* src = "main : void () { for (i; i < 10; i += 1) { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (!stmt.has_loop_var_decl) return false;
  if (stmt.loop_var_decl.type.name != "i32") return false;
  return true;
}


const TestCase kLangCastTests[] = {
  {"lang_split_cast_parses_function_decl", LangSplitCastParsesFunctionDecl},
  {"lang_parse_type_literals", LangParsesTypeLiterals},
  {"lang_parse_bad_array_size", LangRejectsBadArraySize},
  {"lang_parse_func_decl", LangParsesFuncDecl},
  {"lang_parse_fn_keyword", LangParsesFnKeywordDecl},
  {"lang_parse_var_decl", LangParsesVarDecl},
  {"lang_parse_var_decl_no_init", LangParsesVarDeclNoInit},
  {"lang_parse_local_var_decl_no_init", LangParsesLocalVarDeclNoInit},
  {"lang_parse_artifact_decl", LangParsesArtifactDecl},
  {"lang_parse_data_decl", LangParsesDataDecl},
  {"lang_parse_artifact_decl_capitalized", LangParsesArtifactDeclCapitalized},
  {"lang_parse_module_decl", LangParsesModuleDecl},
  {"lang_parse_module_decl_capitalized", LangParsesModuleDeclCapitalized},
  {"lang_parse_import_decl", LangParsesImportDecl},
  {"lang_parse_import_decl_alias", LangParsesImportDeclAlias},
  {"lang_parse_import_decl_unquoted_path", LangParsesImportDeclUnquotedPath},
  {"lang_parse_extern_decl", LangParsesExternDecl},
  {"lang_parse_enum_decl", LangParsesEnumDecl},
  {"lang_parse_enum_decl_capitalized", LangParsesEnumDeclCapitalized},
  {"lang_parse_return_expr", LangParsesReturnExpr},
  {"lang_parse_call_member", LangParsesCallAndMember},
  {"lang_parse_self", LangParsesSelf},
  {"lang_parse_qualified_member", LangParsesQualifiedMember},
  {"lang_parse_reject_double_colon_member", LangRejectsDoubleColonMember},
  {"lang_cast_parser_module_parses_artifact_switch", LangCastParserModuleParsesArtifactSwitch},
  {"lang_parse_missing_semicolon_same_line", LangParseMissingSemicolonSameLine},
  {"lang_parse_error_includes_location", LangParseErrorIncludesLocation},
  {"lang_parse_artifact_comma_diagnostic_hint", LangParseArtifactCommaDiagnosticHint},
  {"lang_parse_reserved_keyword_parameter_hint", LangParseReservedKeywordParameterDiagnosticHint},
  {"lang_parse_comparisons", LangParsesComparisons},
  {"lang_parse_bitwise_precedence", LangParsesBitwisePrecedence},
  {"lang_parse_array_list_index", LangParsesArrayListAndIndex},
  {"lang_parse_artifact_literal", LangParsesArtifactLiteral},
  {"lang_parse_delimited_string_literals", LangParsesDelimitedStringLiterals},
  {"lang_parse_fn_literal", LangParsesFnLiteral},
  {"lang_parse_fn_shorthand_literal_binding", LangParsesFnShorthandLiteralBinding},
  {"lang_parse_assignments", LangParsesAssignments},
  {"lang_parse_recover_in_block", LangParserRecoversInBlock},
  {"lang_parse_inc_dec", LangParsesIncDec},
  {"lang_parse_if_chain", LangParsesIfChain},
  {"lang_parse_if_else", LangParsesIfElse},
  {"lang_parse_while_loop", LangParsesWhileLoop},
  {"lang_parse_break_skip", LangParsesBreakSkip},
  {"lang_parse_for_loop", LangParsesForLoop},
  {"lang_parse_for_loop_post_inc", LangParsesForLoopPostInc},
  {"lang_parse_for_loop_range", LangParsesForLoopRange},
  {"lang_parse_for_loop_range_default_type", LangParsesForLoopRangeDefaultType},
};

const TestSection kLangCastSections[] = {
  {"lang_cast", kLangCastTests, sizeof(kLangCastTests) / sizeof(kLangCastTests[0])},
};

} // namespace

const TestSection* GetLangCastSections(size_t* count) {
  if (count) *count = sizeof(kLangCastSections) / sizeof(kLangCastSections[0]);
  return kLangCastSections;
}

} // namespace Simple::VM::Tests
