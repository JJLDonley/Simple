#include "lang_lexer.h"
#include "lang_parser.h"
#include "lang_sir.h"
#include "lang_validate.h"
#include "ir_lang.h"
#include "ir_compiler.h"
#include "simple_runner.h"
#include "test_utils.h"

#include <unordered_map>
#include <string>
#include <vector>

namespace Simple::VM::Tests {
namespace {

bool ExpectTokenKinds(const std::vector<Simple::Lang::Token>& tokens,
                      const std::vector<Simple::Lang::TokenKind>& kinds) {
  if (tokens.size() < kinds.size()) return false;
  for (size_t i = 0; i < kinds.size(); ++i) {
    if (tokens[i].kind != kinds[i]) return false;
  }
  return true;
}

bool RunSirTextExpectExit(const std::string& sir, int32_t expected) {
  Simple::IR::Text::IrTextModule text;
  std::string error;
  if (!Simple::IR::Text::ParseIrTextModule(sir, &text, &error)) return false;
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(text, &module, &error)) return false;
  std::vector<uint8_t> sbc;
  if (!Simple::IR::CompileToSbc(module, &sbc, &error)) return false;
  return RunExpectExit(sbc, expected);
}

bool RunSimpleFileExpectExit(const std::string& path, int32_t expected) {
  int exit_code = Simple::VM::Tests::RunSimpleFile(path, true);
  return exit_code == expected;
}

bool LangSirEmitsReturnI32() {
  const char* src = "main : i32 () { return 40 + 2; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangSimpleFixtureHello() {
  return RunSimpleFileExpectExit("Simple/Tests/simple/hello.simple", 0);
}

bool LangSimpleFixtureMath() {
  return RunSimpleFileExpectExit("Simple/Tests/simple/math.simple", 0);
}

bool LangSimpleFixtureSumLoop() {
  return RunSimpleFileExpectExit("Simple/Tests/simple/sum_loop.simple", 4950);
}

bool LangSimpleFixtureSumArray() {
  return RunSimpleFileExpectExit("Simple/Tests/simple/sum_array.simple", 6);
}

bool LangSimpleFixturePointSum() {
  return RunSimpleFileExpectExit("Simple/Tests/simple/point_sum.simple", 7);
}

bool LangSimpleFixtureListLen() {
  return RunSimpleFileExpectExit("Simple/Tests/simple/list_len.simple", 4);
}

bool LangSirEmitsLocalAssign() {
  const char* src = "main : i32 () { x : i32 = 1; x = x + 2; return x; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsIfElse() {
  const char* src = "main : i32 () { x : i32 = 1; if x == 1 { return 7; } else { return 9; } }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsWhileLoop() {
  const char* src =
      "main : i32 () { i : i32 = 0; sum : i32 = 0; while i < 5 { sum = sum + i; i = i + 1; } return sum; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 10);
}

bool LangSirEmitsFunctionCall() {
  const char* src =
      "add : i32 (a : i32, b : i32) { return a + b; }"
      "main : i32 () { return add(20, 22); }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangSirEmitsIoPrintString() {
  const char* src =
      "main : i32 () { IO.print(\"hi\"); return 1; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 1);
}

bool LangSirEmitsIoPrintI32() {
  const char* src =
      "main : i32 () { IO.print(42); return 2; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 2);
}

bool LangSirImplicitMainReturn() {
  const char* src =
      "main : i32 () { IO.print(\"hi\") }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 0);
}

bool LangParseMissingSemicolonSameLine() {
  const char* src = "main : i32 () { x : i32 = 1 y : i32 = 2 }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  return true;
}

bool LangSirEmitsIncDec() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 1;"
      "  y : i32 = x++;"
      "  z : i32 = ++x;"
      "  return y + z + x;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsCompoundAssignLocal() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 5;"
      "  x += 3;"
      "  x *= 2;"
      "  return x;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 16);
}

bool LangSirEmitsBitwiseShift() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 5;"
      "  y : i32 = 3;"
      "  return (x & y) | (1 << 3);"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 9);
}

bool LangSirEmitsIndexCompoundAssign() {
  const char* src =
      "main : i32 () {"
      "  values : i32[2] = [1, 2];"
      "  values[1] += 5;"
      "  return values[1];"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsMemberCompoundAssign() {
  const char* src =
      "Point :: artifact { x : i32 y : i32 }"
      "main : i32 () {"
      "  p : Point = { 1, 2 };"
      "  p.x *= 3;"
      "  return p.x;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsIndexIncDec() {
  const char* src =
      "main : i32 () {"
      "  values : i32[1] = [1];"
      "  x : i32 = values[0]++;"
      "  y : i32 = ++values[0];"
      "  return x + y + values[0];"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsMemberIncDec() {
  const char* src =
      "Point :: artifact { x : i32 }"
      "main : i32 () {"
      "  p : Point = { 1 };"
      "  a : i32 = p.x++;"
      "  b : i32 = ++p.x;"
      "  return a + b + p.x;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsArrayLiteralIndex() {
  const char* src = "main : i32 () { values : i32[3] = [1, 2, 3]; return values[1]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 2);
}

bool LangSirEmitsArrayAssign() {
  const char* src = "main : i32 () { values : i32[2] = [1, 2]; values[1] = 7; return values[1]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsListLiteralIndex() {
  const char* src = "main : i32 () { values : i32[] = [1, 2, 3]; return values[2]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsListAssign() {
  const char* src = "main : i32 () { values : i32[] = [1, 2, 3]; values[0] = 9; return values[0]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 9);
}

bool LangSirEmitsLen() {
  const char* src = "main : i32 () { values : i32[] = [1, 2, 3, 4]; return len(values); }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 4);
}

bool LangSirEmitsArtifactLiteralAndMember() {
  const char* src =
      "Point :: artifact { x : i32 y : i32 }"
      "main : i32 () { p : Point = { 1, 2 }; return p.x + p.y; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsArtifactMemberAssign() {
  const char* src =
      "Point :: artifact { x : i32 y : i32 }"
      "main : i32 () { p : Point = { 1, 2 }; p.y = 7; return p.y; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsEnumValue() {
  const char* src =
      "Color :: enum { Red = 1, Green = 2, Blue = 3 }"
      "main : i32 () { return Color.Green; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 2);
}

bool LangSirEmitsFnLiteralCall() {
  const char* src =
      "main : i32 () {"
      "  f : (i32, i32) : i32 = (a : i32, b : i32) { return a + b; };"
      "  return f(20, 22);"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangLexesKeywordsAndOps() {
  const char* src = "fn main :: void() { return; }";
  Simple::Lang::Lexer lex(src);
  if (!lex.Lex()) return false;
  const auto& toks = lex.Tokens();
  std::vector<Simple::Lang::TokenKind> kinds = {
    Simple::Lang::TokenKind::KwFn,
    Simple::Lang::TokenKind::Identifier,
    Simple::Lang::TokenKind::DoubleColon,
    Simple::Lang::TokenKind::Identifier,
    Simple::Lang::TokenKind::LParen,
    Simple::Lang::TokenKind::RParen,
    Simple::Lang::TokenKind::LBrace,
    Simple::Lang::TokenKind::KwReturn,
    Simple::Lang::TokenKind::Semicolon,
    Simple::Lang::TokenKind::RBrace,
  };
  return ExpectTokenKinds(toks, kinds);
}

bool LangLexesLiterals() {
  const char* src = "x : i32 = 42; h : i32 = 0x2A; b : i32 = 0b1010; y : f32 = 3.5; s : string = \"hi\\n\"; c : char = '\\n';";
  Simple::Lang::Lexer lex(src);
  if (!lex.Lex()) return false;
  const auto& toks = lex.Tokens();
  bool saw_int = false;
  bool saw_hex = false;
  bool saw_bin = false;
  bool saw_float = false;
  bool saw_string = false;
  bool saw_char = false;
  for (const auto& tok : toks) {
    if (tok.kind == Simple::Lang::TokenKind::Integer) saw_int = true;
    if (tok.kind == Simple::Lang::TokenKind::Integer && tok.text == "0x2A") saw_hex = true;
    if (tok.kind == Simple::Lang::TokenKind::Integer && tok.text == "0b1010") saw_bin = true;
    if (tok.kind == Simple::Lang::TokenKind::Float) saw_float = true;
    if (tok.kind == Simple::Lang::TokenKind::String) saw_string = true;
    if (tok.kind == Simple::Lang::TokenKind::Char) saw_char = true;
  }
  return saw_int && saw_hex && saw_bin && saw_float && saw_string && saw_char;
}

bool LangLexRejectsInvalidHex() {
  const char* src = "x : i32 = 0xZZ;";
  Simple::Lang::Lexer lex(src);
  return !lex.Lex();
}

bool LangLexRejectsInvalidBinary() {
  const char* src = "x : i32 = 0b2;";
  Simple::Lang::Lexer lex(src);
  return !lex.Lex();
}

bool LangLexRejectsInvalidStringEscape() {
  const char* src = "x : string = \"hi\\q\";";
  Simple::Lang::Lexer lex(src);
  return !lex.Lex();
}

bool LangLexRejectsInvalidCharEscape() {
  const char* src = "x : char = '\\q';";
  Simple::Lang::Lexer lex(src);
  return !lex.Lex();
}

bool LangParsesTypeLiterals() {
  Simple::Lang::TypeRef type;
  std::string error;
  if (!Simple::Lang::ParseTypeFromString("i32", &type, &error)) return false;
  if (type.name != "i32") return false;
  if (!type.dims.empty()) return false;

  if (!Simple::Lang::ParseTypeFromString("i8", &type, &error)) return false;
  if (type.name != "i8") return false;
  if (!Simple::Lang::ParseTypeFromString("i16", &type, &error)) return false;
  if (type.name != "i16") return false;
  if (!Simple::Lang::ParseTypeFromString("i64", &type, &error)) return false;
  if (type.name != "i64") return false;
  if (!Simple::Lang::ParseTypeFromString("i128", &type, &error)) return false;
  if (type.name != "i128") return false;
  if (!Simple::Lang::ParseTypeFromString("u8", &type, &error)) return false;
  if (type.name != "u8") return false;
  if (!Simple::Lang::ParseTypeFromString("u16", &type, &error)) return false;
  if (type.name != "u16") return false;
  if (!Simple::Lang::ParseTypeFromString("u32", &type, &error)) return false;
  if (type.name != "u32") return false;
  if (!Simple::Lang::ParseTypeFromString("u64", &type, &error)) return false;
  if (type.name != "u64") return false;
  if (!Simple::Lang::ParseTypeFromString("u128", &type, &error)) return false;
  if (type.name != "u128") return false;
  if (!Simple::Lang::ParseTypeFromString("f32", &type, &error)) return false;
  if (type.name != "f32") return false;
  if (!Simple::Lang::ParseTypeFromString("f64", &type, &error)) return false;
  if (type.name != "f64") return false;
  if (!Simple::Lang::ParseTypeFromString("bool", &type, &error)) return false;
  if (type.name != "bool") return false;
  if (!Simple::Lang::ParseTypeFromString("char", &type, &error)) return false;
  if (type.name != "char") return false;
  if (!Simple::Lang::ParseTypeFromString("string", &type, &error)) return false;
  if (type.name != "string") return false;

  Simple::Lang::TypeRef arr;
  if (!Simple::Lang::ParseTypeFromString("i32[10][]", &arr, &error)) return false;
  if (arr.dims.size() != 2) return false;
  if (!arr.dims[0].has_size || arr.dims[0].size != 10) return false;
  if (!arr.dims[1].is_list) return false;

  Simple::Lang::TypeRef list_type;
  if (!Simple::Lang::ParseTypeFromString("i32[]", &list_type, &error)) return false;
  if (list_type.dims.size() != 1) return false;
  if (!list_type.dims[0].is_list) return false;

  Simple::Lang::TypeRef list2_type;
  if (!Simple::Lang::ParseTypeFromString("i32[][]", &list2_type, &error)) return false;
  if (list2_type.dims.size() != 2) return false;
  if (!list2_type.dims[0].is_list) return false;
  if (!list2_type.dims[1].is_list) return false;

  Simple::Lang::TypeRef hex_arr;
  if (!Simple::Lang::ParseTypeFromString("i32[0x10]", &hex_arr, &error)) return false;
  if (hex_arr.dims.size() != 1) return false;
  if (!hex_arr.dims[0].has_size || hex_arr.dims[0].size != 16) return false;

  Simple::Lang::TypeRef bin_arr;
  if (!Simple::Lang::ParseTypeFromString("i32[0b1010]", &bin_arr, &error)) return false;
  if (bin_arr.dims.size() != 1) return false;
  if (!bin_arr.dims[0].has_size || bin_arr.dims[0].size != 10) return false;

  Simple::Lang::TypeRef generic;
  if (!Simple::Lang::ParseTypeFromString("Map<string, i32>", &generic, &error)) return false;
  if (generic.type_args.size() != 2) return false;
  if (generic.type_args[0].name != "string") return false;
  if (generic.type_args[1].name != "i32") return false;

  Simple::Lang::TypeRef proc;
  if (!Simple::Lang::ParseTypeFromString("(i32, string) :: bool", &proc, &error)) return false;
  if (!proc.is_proc) return false;
  if (proc.proc_params.size() != 2) return false;
  if (proc.proc_params[0].name != "i32") return false;
  if (proc.proc_params[1].name != "string") return false;
  if (!proc.proc_return) return false;
  if (proc.proc_return->name != "bool") return false;

  Simple::Lang::TypeRef fn_ret;
  if (!Simple::Lang::ParseTypeFromString("fn : i32", &fn_ret, &error)) return false;
  if (!fn_ret.is_proc) return false;
  if (!fn_ret.proc_return) return false;
  if (fn_ret.proc_return->name != "i32") return false;
  if (!fn_ret.proc_params.empty()) return false;

  return true;
}

bool LangRejectsBadArraySize() {
  Simple::Lang::TypeRef type;
  std::string error;
  return !Simple::Lang::ParseTypeFromString("i32[foo]", &type, &error);
}

bool LangParsesFuncDecl() {
  const char* src = "add : i32 (a : i32, b :: i32) { return a + b; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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

bool LangAstTypeCoverage() {
  const char* src =
      "a : i8; b : u8; c : i16; d : u16; e : i32; f : u32; g : i64; h : u64; "
      "i : i128; j : u128; k : f32; l : f64; m : bool; n : char; o : string; "
      "arr : i32[2]; list : i32[]; grid : i32[][]; "
      "proc : fn : i32; proc2 : (i32, f64) :: bool;";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  std::unordered_map<std::string, const Simple::Lang::VarDecl*> vars;
  for (const auto& decl : program.decls) {
    if (decl.kind != Simple::Lang::DeclKind::Variable) continue;
    vars.emplace(decl.var.name, &decl.var);
  }
  const char* primitives[] = {
    "i8","u8","i16","u16","i32","u32","i64","u64","i128","u128","f32","f64","bool","char","string"
  };
  const char* names[] = {
    "a","b","c","d","e","f","g","h","i","j","k","l","m","n","o"
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    auto it = vars.find(names[i]);
    if (it == vars.end()) return false;
    if (it->second->type.name != primitives[i]) return false;
  }
  {
    auto it = vars.find("arr");
    if (it == vars.end()) return false;
    if (it->second->type.name != "i32") return false;
    if (it->second->type.dims.size() != 1) return false;
    if (!it->second->type.dims[0].has_size || it->second->type.dims[0].size != 2) return false;
  }
  {
    auto it = vars.find("list");
    if (it == vars.end()) return false;
    if (it->second->type.name != "i32") return false;
    if (it->second->type.dims.size() != 1) return false;
    if (!it->second->type.dims[0].is_list) return false;
  }
  {
    auto it = vars.find("grid");
    if (it == vars.end()) return false;
    if (it->second->type.name != "i32") return false;
    if (it->second->type.dims.size() != 2) return false;
    if (!it->second->type.dims[0].is_list || !it->second->type.dims[1].is_list) return false;
  }
  {
    auto it = vars.find("proc");
    if (it == vars.end()) return false;
    if (!it->second->type.is_proc) return false;
    if (!it->second->type.proc_return) return false;
    if (it->second->type.proc_return->name != "i32") return false;
    if (!it->second->type.proc_params.empty()) return false;
  }
  {
    auto it = vars.find("proc2");
    if (it == vars.end()) return false;
    if (!it->second->type.is_proc) return false;
    if (it->second->type.proc_params.size() != 2) return false;
    if (it->second->type.proc_params[0].name != "i32") return false;
    if (it->second->type.proc_params[1].name != "f64") return false;
    if (!it->second->type.proc_return) return false;
    if (it->second->type.proc_return->name != "bool") return false;
  }
  return true;
}

bool LangParserRecoversInBlock() {
  const char* src = "main : void () { +; x : i32 = 1; }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::VarDecl) return false;
  if (stmt.var_decl.name != "x") return false;
  if (stmt.var_decl.has_init_expr) return false;
  return true;
}

bool LangParsesArtifactDecl() {
  const char* src = "Point :: artifact { x : f32 y :: f32 len : i32 () { return 1; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Artifact) return false;
  if (decl.artifact.name != "Point") return false;
  if (decl.artifact.fields.size() != 2) return false;
  if (decl.artifact.methods.size() != 1) return false;
  return true;
}

bool LangParsesModuleDecl() {
  const char* src = "Math :: module { scale : i32 = 2; add : i32 (a : i32, b : i32) { return a + b; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Module) return false;
  if (decl.module.name != "Math") return false;
  if (decl.module.variables.size() != 1) return false;
  if (decl.module.functions.size() != 1) return false;
  return true;
}

bool LangParsesEnumDecl() {
  const char* src =
    "Status :: enum { Pending = 1, Active = 2 }"
    "Color :: enum { Red, Green, Blue }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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

bool LangParsesReturnExpr() {
  const char* src = "main : i32 () { return 1 + 2 * 3; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& decl = program.decls[0];
  const auto& expr = decl.func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  const auto& left = expr.children[0];
  if (left.kind != Simple::Lang::ExprKind::Member) return false;
  return true;
}

bool LangParsesSelf() {
  const char* src = "Point :: artifact { x : i32 get : i32 () { return self.x; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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

bool LangValidateEnumQualified() {
  const char* src = "Color :: enum { Red = 1 } main : i32 () { return Color.Red; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumQualifiedDot() {
  const char* src = "Color :: enum { Red = 1 } main : i32 () { return Color::Red; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumUnqualified() {
  const char* src = "Color :: enum { Red = 1 } main : i32 () { return Red; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumDuplicateMember() {
  const char* src = "Color :: enum { Red = 1, Red = 2 }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumMissingValue() {
  const char* src = "Color :: enum { Red }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumTypeNotValue() {
  const char* src = "Color :: enum { Red = 1 } main : i32 () { x : i32 = Color; return x; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumUnknownMember() {
  const char* src = "Color :: enum { Red = 1 } main : i32 () { return Color.Blue; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateModuleNotValue() {
  const char* src = "Math :: module { } main : void () { x : i32 = Math; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactTypeNotValue() {
  const char* src = "Point :: artifact { x : i32 } main : void () { p : Point = Point; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateTopLevelDuplicate() {
  const char* src = "A :: enum { Red } A :: artifact { x : i32 }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLocalDuplicateSameScope() {
  const char* src = "main : void () { x : i32 = 1; x : i32 = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLocalDuplicateShadowAllowed() {
  const char* src = "main : void () { x : i32 = 1; if true { x : i32 = 2; } }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateForLoopScope() {
  const char* src =
    "main : void () {"
    "  x : i32 = 0;"
    "  for x = x; x < 1; x = x + 1 { x : i32 = 2; }"
    "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactDuplicateMember() {
  const char* src = "Thing :: artifact { x : i32 x : i32 }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateModuleDuplicateMember() {
  const char* src = "Math :: module { x : i32 = 1; x : i32 = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateModuleVarNoInit() {
  const char* src =
    "Math :: module { x : i32; }"
    "main : i32 () { return 0; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGlobalVarNoInit() {
  const char* src =
    "g : i32;"
    "main : i32 () { return g; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateDuplicateParams() {
  const char* src = "add : i32 (a : i32, a : i32) { return a; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateVoidReturnValue() {
  const char* src = "main : void () { return 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNonVoidMissingReturn() {
  const char* src = "main : i32 () { return; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNonVoidNoReturn() {
  const char* src = "foo : i32 () { x : i32 = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNonVoidAllPathsReturn() {
  const char* src =
    "main : i32 () {"
    "  if true { return 1; } else { return 2; }"
    "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNonVoidMissingPath() {
  const char* src =
    "foo : i32 () {"
    "  if true { return 1; }"
    "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateBreakOutsideLoop() {
  const char* src = "main : void () { break; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateSkipOutsideLoop() {
  const char* src = "main : void () { skip; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateUndeclaredIdentifier() {
  const char* src = "main : i32 () { return foo; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateUnknownType() {
  const char* src = "main : i32 () { x : NotAType = 1; return 0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateVoidValueType() {
  const char* src = "main : i32 () { x : void = 1; return 0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateVoidParamType() {
  const char* src = "main : i32 (x : void) { return 0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidatePrimitiveTypeArgs() {
  const char* src = "main : i32 () { x : i32<i32> = 1; return 0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateTypeParamOk() {
  const char* src = "id<T> : T (v : T) { return v; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateTypeParamWithArgs() {
  const char* src = "id<T> : i32 (v : T<i32>) { return 0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableVarAssign() {
  const char* src = "main : void () { x :: i32 = 1; x = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableParamAssign() {
  const char* src = "main : void (x :: i32) { x = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableFieldAssign() {
  const char* src =
    "Point :: artifact { x :: i32 }"
    "main : void () { p : Point = { 1 }; p.x = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableSelfFieldAssign() {
  const char* src =
    "Point :: artifact { x :: i32 set : void () { self.x = 1; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableModuleAssign() {
  const char* src =
    "Math :: module { PI :: f64 = 3.14; }"
    "main : void () { Math.PI = 0.0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateAssignToFunctionFail() {
  const char* src =
    "add : i32 (a : i32, b : i32) { return a + b; }"
    "main : void () { add = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateAssignToModuleFunctionFail() {
  const char* src =
    "Math :: module { add : i32 (a : i32, b : i32) { return a + b; } }"
    "main : void () { Math.add = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateAssignToArtifactMethodFail() {
  const char* src =
    "Point :: artifact { x : i32 get : i32 () { return x; } }"
    "main : void () { p : Point = { 1 }; p.get = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateAssignToSelfMethodFail() {
  const char* src =
    "Point :: artifact { x : i32 get : i32 () { return x; } set : void () { self.get = 1; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIncDecImmutableLocal() {
  const char* src = "main : void () { x :: i32 = 1; x++; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIncDecInvalidTarget() {
  const char* src = "main : void () { (1 + 2)++; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateUnknownModuleMember() {
  const char* src =
    "Math :: module { x : i32 = 1; }"
    "main : i32 () { return Math.y; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateMutableFieldAssignOk() {
  const char* src =
    "Point :: artifact { x : i32 }"
    "main : void () { p : Point = { 1 }; p.x = 2; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateUnknownArtifactMember() {
  const char* src =
    "Point :: artifact { x : i32 }"
    "main : i32 () { p : Point = { 1 }; return p.y; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateSelfOutsideMethod() {
  const char* src = "main : void () { self; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralTooManyPositional() {
  const char* src =
    "Point :: artifact { x : i32 y : i32 }"
    "main : void () { p : Point = { 1, 2, 3 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralDuplicateNamed() {
  const char* src =
    "Point :: artifact { x : i32 y : i32 }"
    "main : void () { p : Point = { .x = 1, .x = 2 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralUnknownField() {
  const char* src =
    "Point :: artifact { x : i32 y : i32 }"
    "main : void () { p : Point = { .z = 1 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralPositionalThenNamedDuplicate() {
  const char* src =
    "Point :: artifact { x : i32 y : i32 }"
    "main : void () { p : Point = { 1, .x = 2 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralNamedOk() {
  const char* src =
    "Point :: artifact { x : i32 y : i32 }"
    "main : void () { p : Point = { .x = 1 }; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralTypeMismatchPositional() {
  const char* src =
    "Point :: artifact { x : i32 y : i32 }"
    "main : void () { p : Point = { 1, true }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralTypeMismatchNamed() {
  const char* src =
    "Point :: artifact { x : i32 y : i32 }"
    "main : void () { p : Point = { .y = true }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexFloatLiteral() {
  const char* src = "main : i32 () { return [1,2,3][1.5]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexStringLiteral() {
  const char* src = "main : i32 () { return [1,2,3][\"no\"]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexLiteralBase() {
  const char* src = "main : i32 () { return 123[0]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexIntOk() {
  const char* src = "main : i32 () { return [1,2,3][1]; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexNonIndexableVar() {
  const char* src = "main : i32 () { x : i32 = 1; return x[0]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexNonIntegerExpr() {
  const char* src = "main : i32 () { a : i32[] = []; return a[true]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallArgCount() {
  const char* src = "add : i32 (a : i32, b : i32) { return a; } main : i32 () { return add(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallNonFunction() {
  const char* src = "x : i32 = 1; main : i32 () { return x(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallModuleFuncCount() {
  const char* src =
    "Math :: module { add : i32 (a : i32, b : i32) { return a; } }"
    "main : i32 () { return Math.add(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallModuleVar() {
  const char* src =
    "Math :: module { PI :: f64 = 3.14; }"
    "main : i32 () { return Math.PI(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallMethodArgCount() {
  const char* src =
    "Point :: artifact { x : i32 get : i32 () { return self.x; } }"
    "main : i32 () { p : Point = { 1 }; return p.get(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallFieldAsMethod() {
  const char* src =
    "Point :: artifact { x : i32 }"
    "main : i32 () { p : Point = { 1 }; return p.x(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIoPrintArgCountFail() {
  const char* src = "main : void () { IO.print(); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIoPrintTypeArgsOk() {
  const char* src = "main : void () { IO.print<i32>(1); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIoPrintRejectsArray() {
  const char* src = "main : void () { a : i32[] = [1,2]; IO.print(a); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangRunsSimpleFixtures() {
  const std::string dir = "Simple/Tests/simple";
  return Simple::VM::Tests::RunSimplePerfDir(dir, 1, true) == 0;
}

bool LangValidateCallFnLiteralCount() {
  const char* src =
    "main : i32 () { f : (i32) : i32 = (x : i32) { return x; }; return f(1, 2); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallFnLiteralOk() {
  const char* src =
    "main : i32 () { f : (i32) : i32 = (x : i32) { return x; }; return f(1); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactMemberRequiresSelfField() {
  const char* src =
    "Point :: artifact { x : i32 get : i32 () { return x; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactMemberRequiresSelfMethod() {
  const char* src =
    "Point :: artifact { get : i32 () { return 1; } use : i32 () { return get(); } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactMemberSelfOk() {
  const char* src =
    "Point :: artifact { x : i32 get : i32 () { return self.x; } use : i32 () { return self.get(); } }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateTypeMismatchVarInit() {
  const char* src = "main : void () { x : i32 = \"hi\"; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateTypeMismatchAssign() {
  const char* src = "main : void () { x : i32 = 1; x = \"hi\"; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateFnLiteralAssignOk() {
  const char* src = "main : void () { f : (i32) : i32 = (a : i32) { return a; }; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateFnLiteralAssignTypeMismatch() {
  const char* src = "main : void () { f : (i32) : i32 = (a : f64) { return 1; }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateFnLiteralAssignNotProcType() {
  const char* src = "main : void () { f : i32 = (a : i32) { return a; }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCompoundAssignNumericOk() {
  const char* src = "main : void () { x : i32 = 1; x += 2; x <<= 1; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCompoundAssignTypeMismatch() {
  const char* src = "main : void () { x : i32 = 1; x += 1.0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCompoundAssignInvalidType() {
  const char* src = "main : void () { x : bool = true; x += false; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateReturnTypeMismatch() {
  const char* src = "main : i32 () { return \"hi\"; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateReturnTypeMatch() {
  const char* src = "main : string () { return \"hi\"; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexTypeOk() {
  const char* src = "main : void () { arr : i32[2] = [1,2]; x : i32 = arr[0]; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexTypeMismatch() {
  const char* src = "main : void () { arr : i32[2] = [1,2]; x : f64 = arr[0]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexNestedArrayTypeOk() {
  const char* src = "main : void () { arr : i32[2][2] = [[1,2],[3,4]]; row : i32[2] = arr[0]; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexListTypeOk() {
  const char* src = "main : void () { list : string[] = [\"a\"]; s : string = list[0]; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexListTypeMismatch() {
  const char* src = "main : void () { list : string[] = [\"a\"]; x : i32 = list[0]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateAssignExprStatementMismatch() {
  const char* src = "main : void () { x : i32 = 0; (x = \"hi\"); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateAssignExprStatementOk() {
  const char* src = "main : void () { x : i32 = 0; (x = 1); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableBaseFieldAssign() {
  const char* src = "Point :: artifact { x : i32 } main : void () { p :: Point = { 1 }; p.x = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableBaseIndexAssign() {
  const char* src = "main : void () { a :: i32[] = [1, 2]; a[0] = 3; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateImmutableReturnAssign() {
  const char* src = "Point :: artifact { x : i32 } make :: Point () { return { 1 }; } main : void () { make().x = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallArgTypeMismatch() {
  const char* src = "add : i32 (a : i32, b : i32) { return a + b; } main : void () { add(1, \"hi\"); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallArgTypeOk() {
  const char* src = "add : i32 (a : i32, b : i32) { return a + b; } main : void () { add(1, 2); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericArtifactLiteralOk() {
  const char* src = "Box<T> :: artifact { value : T } main : void () { b : Box<i32> = { 1 }; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericArtifactLiteralMismatch() {
  const char* src = "Box<T> :: artifact { value : T } main : void () { b : Box<i32> = { \"hi\" }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericFieldAccessOk() {
  const char* src = "Box<T> :: artifact { value : T } main : void () { b : Box<i32> = { 1 }; x : i32 = b.value; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericFieldAccessMismatch() {
  const char* src = "Box<T> :: artifact { value : T } main : void () { b : Box<i32> = { 1 }; x : f64 = b.value; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericMethodReturnOk() {
  const char* src =
      "Box<T> :: artifact { value : T; get : T () { return self.value; } } "
      "main : void () { b : Box<i32> = { 1 }; x : i32 = b.get(); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericMethodReturnMismatch() {
  const char* src =
      "Box<T> :: artifact { value : T; get : T () { return self.value; } } "
      "main : void () { b : Box<i32> = { 1 }; x : f64 = b.get(); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericCallExplicit() {
  const char* src =
      "identity<T> : T (value : T) { return value; } "
      "main : void () { x : i32 = identity<i32>(10); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericCallInferred() {
  const char* src =
      "identity<T> : T (value : T) { return value; } "
      "main : void () { x : i32 = identity(10); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericCallInferFail() {
  const char* src =
      "identity<T> : T (value : T) { return value; } "
      "main : void () { x : i32 = identity(); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericCallTypeMismatch() {
  const char* src =
      "identity<T> : T (value : T) { return value; } "
      "main : void () { x : i32 = identity<i32>(\"hi\"); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNonGenericCallTypeArgs() {
  const char* src =
      "add : i32 (a : i32) { return a; } "
      "main : void () { x : i32 = add<i32>(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericTypeArgsMismatch() {
  const char* src = "Box<T> :: artifact { value : T } main : void () { x : Box = { 1 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateGenericTypeArgsWrongCount() {
  const char* src = "Box<T> :: artifact { value : T } main : void () { x : Box<i32, i32> = { 1 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNonGenericTypeArgs() {
  const char* src = "Point :: artifact { x : i32 } main : void () { p : Point<i32> = { 1 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumTypeArgsRejected() {
  const char* src = "Color :: enum { Red } main : void () { c : Color<i32> = Color.Red; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateModuleNotType() {
  const char* src = "Math :: module { pi : i32 = 3; } main : void () { x : Math = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateFunctionNotType() {
  const char* src = "fn Foo : i32 () { return 0; } main : void () { x : Foo = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralShapeMatch() {
  const char* src = "main : void () { a : i32[2][2] = [[1,2],[3,4]]; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralShapeMismatch() {
  const char* src = "main : void () { a : i32[2] = [1,2,3]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralNestedMismatch() {
  const char* src = "main : void () { a : i32[2][2] = [[1,2,3],[4,5,6]]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralNonArrayChild() {
  const char* src = "main : void () { a : i32[2][2] = [1,2]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralEmptyMismatch() {
  const char* src = "main : void () { a : i32[2] = []; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralElementMismatch() {
  const char* src = "main : void () { a : i32[2] = [1, true]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralNestedElementMismatch() {
  const char* src = "main : void () { a : i32[2][2] = [[1,2],[3,4]]; b : i32[2][2] = [[1,2],[3,true]]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateListLiteralElementMismatch() {
  const char* src = "main : void () { a : i32[] = [1, true]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNestedListLiteralElementMismatch() {
  const char* src = "main : void () { a : i32[][] = [[1,2],[3,true]]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralScalarTarget() {
  const char* src = "main : void () { a : i32 = [1,2]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateListLiteralScalarTarget() {
  const char* src = "main : void () { a : i32 = []; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateListLiteralOk() {
  const char* src = "main : void () { a : i32[] = [1,2]; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIfConditionTypeMismatch() {
  const char* src = "main : void () { if 1 { return; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIfChainConditionTypeMismatch() {
  const char* src = "main : void () { |> 1 { return; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateWhileConditionTypeMismatch() {
  const char* src = "main : void () { while 1 { break; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateForConditionTypeMismatch() {
  const char* src = "main : void () { for i : i32 = 0; 1; i = i + 1 { break; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLenArrayOk() {
  const char* src = "main : i32 () { a : i32[3] = [1,2,3]; return len(a); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLenListOk() {
  const char* src = "main : i32 () { a : i32[] = [1,2,3]; return len(a); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLenStringOk() {
  const char* src = "main : i32 () { s : string = \"hi\"; return len(s); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateStrFromI32Ok() {
  const char* src = "main : string () { x : i32 = 1; return str(x); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateStrFromBoolOk() {
  const char* src = "main : string () { return str(true); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateStrFromStringFail() {
  const char* src = "main : string () { s : string = \"hi\"; return str(s); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateI32FromStringOk() {
  const char* src = "main : i32 () { s : string = \"42\"; return i32(s); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateI32FromI32Fail() {
  const char* src = "main : i32 () { x : i32 = 1; return i32(x); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateF64FromStringOk() {
  const char* src = "main : f64 () { s : string = \"1.5\"; return f64(s); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateF64FromF64Fail() {
  const char* src = "main : f64 () { x : f64 = 1.0; return f64(x); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLenScalarFail() {
  const char* src = "main : i32 () { x : i32 = 1; return len(x); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLenArgCountFail() {
  const char* src = "main : i32 () { a : i32[] = []; return len(a, a); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateUnaryTypeMismatch() {
  const char* src = "main : i32 () { return !1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateBinaryTypeMismatch() {
  const char* src = "main : i32 () { return 1 + 2.0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateComparisonTypeMismatch() {
  const char* src = "main : bool () { return 1 < true; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateBitwiseTypeMismatch() {
  const char* src = "main : i32 () { return 1 & 2.0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateModuloFloatMismatch() {
  const char* src = "main : f64 () { return 1.0 % 2.0; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangParsesQualifiedMember() {
  const char* src = "main : i32 () { return Math.PI; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  return true;
}

bool LangParsesComparisons() {
  const char* src = "main : bool () { return 1 + 2 * 3 == 7 && 4 < 5; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& expr = program.decls[0].func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  if (expr.op != "&&") return false;
  return true;
}

bool LangParsesBitwisePrecedence() {
  const char* src = "main : i32 () { return 1 | 2 ^ 3 & 4 << 1; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& expr = program.decls[0].func.body[0].expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary) return false;
  const auto& left = expr.children[0];
  if (left.kind != Simple::Lang::ExprKind::Index) return false;
  const auto& list_index = expr.children[1];
  if (list_index.kind != Simple::Lang::ExprKind::Index) return false;
  return true;
}

bool LangParsesArtifactLiteral() {
  const char* src = "main : void () { foo({ 1, .y = 2 }); }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::Expr) return false;
  if (stmt.expr.kind != Simple::Lang::ExprKind::Call) return false;
  if (stmt.expr.args.size() != 1) return false;
  const auto& arg = stmt.expr.args[0];
  if (arg.kind != Simple::Lang::ExprKind::ArtifactLiteral) return false;
  if (arg.children.size() != 1) return false;
  if (arg.field_names.size() != 1) return false;
  if (arg.field_values.size() != 1) return false;
  if (arg.field_names[0] != "y") return false;
  return true;
}

bool LangParsesFnLiteral() {
  const char* src = "main : void () { f : (i32) : i32 = (x : i32) { return x; }; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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

bool LangParsesAssignments() {
  const char* src = "main : i32 () { x : i32 = 1; x += 2; x = x * 3; return x; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& body = program.decls[0].func.body;
  if (body.size() != 4) return false;
  for (const auto& stmt : body) {
    if (stmt.kind != Simple::Lang::StmtKind::Expr) return false;
    if (stmt.expr.kind != Simple::Lang::ExprKind::Unary) return false;
  }
  return true;
}

bool LangParsesIfChain() {
  const char* src = "main : i32 () { |> true { return 1; } |> default { return 2; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::IfChain) return false;
  if (stmt.if_branches.size() != 1) return false;
  if (stmt.else_branch.empty()) return false;
  return true;
}

bool LangParsesIfElse() {
  const char* src = "main : i32 () { if x < 1 { return 1; } else { return 2; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::IfStmt) return false;
  if (stmt.if_then.size() != 1) return false;
  if (stmt.if_else.size() != 1) return false;
  return true;
}

bool LangParsesWhileLoop() {
  const char* src = "main : void () { while x < 10 { x = x + 1; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::WhileLoop) return false;
  return true;
}

bool LangParsesBreakSkip() {
  const char* src = "main : void () { while true { break; skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& loop = program.decls[0].func.body[0];
  if (loop.kind != Simple::Lang::StmtKind::WhileLoop) return false;
  if (loop.loop_body.size() != 2) return false;
  if (loop.loop_body[0].kind != Simple::Lang::StmtKind::Break) return false;
  if (loop.loop_body[1].kind != Simple::Lang::StmtKind::Skip) return false;
  return true;
}

bool LangParsesForLoop() {
  const char* src = "main : void () { for i = 0; i < 10; i = i + 1 { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  return true;
}

bool LangParsesForLoopPostInc() {
  const char* src = "main : void () { for i = 0; i < 10; i++ { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (stmt.loop_step.kind != Simple::Lang::ExprKind::Unary) return false;
  return true;
}

const TestCase kLangTests[] = {
  {"lang_lex_keywords_ops", LangLexesKeywordsAndOps},
  {"lang_lex_literals", LangLexesLiterals},
  {"lang_lex_reject_invalid_hex", LangLexRejectsInvalidHex},
  {"lang_lex_reject_invalid_binary", LangLexRejectsInvalidBinary},
  {"lang_lex_reject_invalid_string_escape", LangLexRejectsInvalidStringEscape},
  {"lang_lex_reject_invalid_char_escape", LangLexRejectsInvalidCharEscape},
  {"lang_parse_type_literals", LangParsesTypeLiterals},
  {"lang_parse_bad_array_size", LangRejectsBadArraySize},
  {"lang_parse_func_decl", LangParsesFuncDecl},
  {"lang_parse_fn_keyword", LangParsesFnKeywordDecl},
  {"lang_parse_var_decl", LangParsesVarDecl},
  {"lang_parse_var_decl_no_init", LangParsesVarDeclNoInit},
  {"lang_parse_local_var_decl_no_init", LangParsesLocalVarDeclNoInit},
  {"lang_parse_artifact_decl", LangParsesArtifactDecl},
  {"lang_parse_module_decl", LangParsesModuleDecl},
  {"lang_parse_enum_decl", LangParsesEnumDecl},
  {"lang_parse_return_expr", LangParsesReturnExpr},
  {"lang_parse_call_member", LangParsesCallAndMember},
  {"lang_parse_self", LangParsesSelf},
  {"lang_parse_qualified_member", LangParsesQualifiedMember},
  {"lang_parse_reject_double_colon_member", LangRejectsDoubleColonMember},
  {"lang_sir_emit_return_i32", LangSirEmitsReturnI32},
  {"lang_sir_emit_local_assign", LangSirEmitsLocalAssign},
  {"lang_sir_emit_if_else", LangSirEmitsIfElse},
  {"lang_sir_emit_while_loop", LangSirEmitsWhileLoop},
  {"lang_sir_emit_function_call", LangSirEmitsFunctionCall},
  {"lang_sir_emit_io_print_string", LangSirEmitsIoPrintString},
  {"lang_sir_emit_io_print_i32", LangSirEmitsIoPrintI32},
  {"lang_sir_implicit_main_return", LangSirImplicitMainReturn},
  {"lang_parse_missing_semicolon_same_line", LangParseMissingSemicolonSameLine},
  {"lang_simple_fixture_hello", LangSimpleFixtureHello},
  {"lang_simple_fixture_math", LangSimpleFixtureMath},
  {"lang_simple_fixture_sum_loop", LangSimpleFixtureSumLoop},
  {"lang_simple_fixture_sum_array", LangSimpleFixtureSumArray},
  {"lang_simple_fixture_point_sum", LangSimpleFixturePointSum},
  {"lang_simple_fixture_list_len", LangSimpleFixtureListLen},
  {"lang_sir_emit_inc_dec", LangSirEmitsIncDec},
  {"lang_sir_emit_compound_assign_local", LangSirEmitsCompoundAssignLocal},
  {"lang_sir_emit_bitwise_shift", LangSirEmitsBitwiseShift},
  {"lang_sir_emit_index_compound_assign", LangSirEmitsIndexCompoundAssign},
  {"lang_sir_emit_member_compound_assign", LangSirEmitsMemberCompoundAssign},
  {"lang_sir_emit_index_inc_dec", LangSirEmitsIndexIncDec},
  {"lang_sir_emit_member_inc_dec", LangSirEmitsMemberIncDec},
  {"lang_sir_emit_array_literal_index", LangSirEmitsArrayLiteralIndex},
  {"lang_sir_emit_array_assign", LangSirEmitsArrayAssign},
  {"lang_sir_emit_list_literal_index", LangSirEmitsListLiteralIndex},
  {"lang_sir_emit_list_assign", LangSirEmitsListAssign},
  {"lang_sir_emit_len", LangSirEmitsLen},
  {"lang_sir_emit_artifact_literal_member", LangSirEmitsArtifactLiteralAndMember},
  {"lang_sir_emit_artifact_member_assign", LangSirEmitsArtifactMemberAssign},
  {"lang_sir_emit_enum_value", LangSirEmitsEnumValue},
  {"lang_sir_emit_fn_literal_call", LangSirEmitsFnLiteralCall},
  {"lang_validate_enum_qualified", LangValidateEnumQualified},
  {"lang_validate_enum_qualified_dot", LangValidateEnumQualifiedDot},
  {"lang_validate_enum_unqualified", LangValidateEnumUnqualified},
  {"lang_validate_enum_duplicate", LangValidateEnumDuplicateMember},
  {"lang_validate_enum_missing_value", LangValidateEnumMissingValue},
  {"lang_validate_enum_type_not_value", LangValidateEnumTypeNotValue},
  {"lang_validate_enum_unknown_member", LangValidateEnumUnknownMember},
  {"lang_validate_module_not_value", LangValidateModuleNotValue},
  {"lang_validate_artifact_type_not_value", LangValidateArtifactTypeNotValue},
  {"lang_validate_top_level_duplicate", LangValidateTopLevelDuplicate},
  {"lang_validate_local_duplicate_same_scope", LangValidateLocalDuplicateSameScope},
  {"lang_validate_local_duplicate_shadow_allowed", LangValidateLocalDuplicateShadowAllowed},
  {"lang_validate_for_loop_scope", LangValidateForLoopScope},
  {"lang_validate_artifact_duplicate_member", LangValidateArtifactDuplicateMember},
  {"lang_validate_module_duplicate_member", LangValidateModuleDuplicateMember},
  {"lang_validate_module_var_no_init", LangValidateModuleVarNoInit},
  {"lang_validate_global_var_no_init", LangValidateGlobalVarNoInit},
  {"lang_validate_duplicate_params", LangValidateDuplicateParams},
  {"lang_validate_void_return_value", LangValidateVoidReturnValue},
  {"lang_validate_nonvoid_missing_return", LangValidateNonVoidMissingReturn},
  {"lang_validate_nonvoid_no_return", LangValidateNonVoidNoReturn},
  {"lang_validate_nonvoid_all_paths", LangValidateNonVoidAllPathsReturn},
  {"lang_validate_nonvoid_missing_path", LangValidateNonVoidMissingPath},
  {"lang_validate_break_outside_loop", LangValidateBreakOutsideLoop},
  {"lang_validate_skip_outside_loop", LangValidateSkipOutsideLoop},
  {"lang_validate_undeclared_identifier", LangValidateUndeclaredIdentifier},
  {"lang_validate_unknown_type", LangValidateUnknownType},
  {"lang_validate_void_value_type", LangValidateVoidValueType},
  {"lang_validate_void_param_type", LangValidateVoidParamType},
  {"lang_validate_primitive_type_args", LangValidatePrimitiveTypeArgs},
  {"lang_validate_type_param_ok", LangValidateTypeParamOk},
  {"lang_validate_type_param_with_args", LangValidateTypeParamWithArgs},
  {"lang_validate_immutable_var_assign", LangValidateImmutableVarAssign},
  {"lang_validate_immutable_param_assign", LangValidateImmutableParamAssign},
  {"lang_validate_immutable_field_assign", LangValidateImmutableFieldAssign},
  {"lang_validate_immutable_self_field_assign", LangValidateImmutableSelfFieldAssign},
  {"lang_validate_immutable_module_assign", LangValidateImmutableModuleAssign},
  {"lang_validate_assign_to_function_fail", LangValidateAssignToFunctionFail},
  {"lang_validate_assign_to_module_function_fail", LangValidateAssignToModuleFunctionFail},
  {"lang_validate_assign_to_artifact_method_fail", LangValidateAssignToArtifactMethodFail},
  {"lang_validate_assign_to_self_method_fail", LangValidateAssignToSelfMethodFail},
  {"lang_validate_incdec_immutable_local", LangValidateIncDecImmutableLocal},
  {"lang_validate_incdec_invalid_target", LangValidateIncDecInvalidTarget},
  {"lang_validate_unknown_module_member", LangValidateUnknownModuleMember},
  {"lang_validate_mutable_field_assign_ok", LangValidateMutableFieldAssignOk},
  {"lang_validate_unknown_artifact_member", LangValidateUnknownArtifactMember},
  {"lang_validate_self_outside_method", LangValidateSelfOutsideMethod},
  {"lang_validate_artifact_literal_too_many_positional", LangValidateArtifactLiteralTooManyPositional},
  {"lang_validate_artifact_literal_duplicate_named", LangValidateArtifactLiteralDuplicateNamed},
  {"lang_validate_artifact_literal_unknown_field", LangValidateArtifactLiteralUnknownField},
  {"lang_validate_artifact_literal_positional_then_named_duplicate", LangValidateArtifactLiteralPositionalThenNamedDuplicate},
  {"lang_validate_artifact_literal_named_ok", LangValidateArtifactLiteralNamedOk},
  {"lang_validate_artifact_literal_type_mismatch_positional", LangValidateArtifactLiteralTypeMismatchPositional},
  {"lang_validate_artifact_literal_type_mismatch_named", LangValidateArtifactLiteralTypeMismatchNamed},
  {"lang_validate_index_float_literal", LangValidateIndexFloatLiteral},
  {"lang_validate_index_string_literal", LangValidateIndexStringLiteral},
  {"lang_validate_index_literal_base", LangValidateIndexLiteralBase},
  {"lang_validate_index_int_ok", LangValidateIndexIntOk},
  {"lang_validate_index_non_indexable_var", LangValidateIndexNonIndexableVar},
  {"lang_validate_index_non_integer_expr", LangValidateIndexNonIntegerExpr},
  {"lang_validate_call_arg_count", LangValidateCallArgCount},
  {"lang_validate_call_non_function", LangValidateCallNonFunction},
  {"lang_validate_call_module_func_count", LangValidateCallModuleFuncCount},
  {"lang_validate_call_module_var", LangValidateCallModuleVar},
  {"lang_validate_call_method_arg_count", LangValidateCallMethodArgCount},
  {"lang_validate_call_field_as_method", LangValidateCallFieldAsMethod},
  {"lang_validate_io_print_arg_count", LangValidateIoPrintArgCountFail},
  {"lang_validate_io_print_type_args_ok", LangValidateIoPrintTypeArgsOk},
  {"lang_validate_io_print_rejects_array", LangValidateIoPrintRejectsArray},
  {"lang_run_simple_fixtures", LangRunsSimpleFixtures},
  {"lang_validate_call_fn_literal_count", LangValidateCallFnLiteralCount},
  {"lang_validate_call_fn_literal_ok", LangValidateCallFnLiteralOk},
  {"lang_validate_artifact_member_requires_self_field", LangValidateArtifactMemberRequiresSelfField},
  {"lang_validate_artifact_member_requires_self_method", LangValidateArtifactMemberRequiresSelfMethod},
  {"lang_validate_artifact_member_self_ok", LangValidateArtifactMemberSelfOk},
  {"lang_validate_type_mismatch_var_init", LangValidateTypeMismatchVarInit},
  {"lang_validate_type_mismatch_assign", LangValidateTypeMismatchAssign},
  {"lang_validate_fn_literal_assign_ok", LangValidateFnLiteralAssignOk},
  {"lang_validate_fn_literal_assign_type_mismatch", LangValidateFnLiteralAssignTypeMismatch},
  {"lang_validate_fn_literal_assign_not_proc_type", LangValidateFnLiteralAssignNotProcType},
  {"lang_validate_compound_assign_numeric_ok", LangValidateCompoundAssignNumericOk},
  {"lang_validate_compound_assign_type_mismatch", LangValidateCompoundAssignTypeMismatch},
  {"lang_validate_compound_assign_invalid_type", LangValidateCompoundAssignInvalidType},
  {"lang_validate_return_type_mismatch", LangValidateReturnTypeMismatch},
  {"lang_validate_return_type_match", LangValidateReturnTypeMatch},
  {"lang_validate_index_type_ok", LangValidateIndexTypeOk},
  {"lang_validate_index_type_mismatch", LangValidateIndexTypeMismatch},
  {"lang_validate_index_nested_array_type_ok", LangValidateIndexNestedArrayTypeOk},
  {"lang_validate_index_list_type_ok", LangValidateIndexListTypeOk},
  {"lang_validate_index_list_type_mismatch", LangValidateIndexListTypeMismatch},
  {"lang_validate_assign_expr_statement_mismatch", LangValidateAssignExprStatementMismatch},
  {"lang_validate_assign_expr_statement_ok", LangValidateAssignExprStatementOk},
  {"lang_validate_immutable_base_field_assign", LangValidateImmutableBaseFieldAssign},
  {"lang_validate_immutable_base_index_assign", LangValidateImmutableBaseIndexAssign},
  {"lang_validate_immutable_return_assign", LangValidateImmutableReturnAssign},
  {"lang_validate_call_arg_type_mismatch", LangValidateCallArgTypeMismatch},
  {"lang_validate_call_arg_type_ok", LangValidateCallArgTypeOk},
  {"lang_validate_generic_artifact_literal_ok", LangValidateGenericArtifactLiteralOk},
  {"lang_validate_generic_artifact_literal_mismatch", LangValidateGenericArtifactLiteralMismatch},
  {"lang_validate_generic_field_access_ok", LangValidateGenericFieldAccessOk},
  {"lang_validate_generic_field_access_mismatch", LangValidateGenericFieldAccessMismatch},
  {"lang_validate_generic_method_return_ok", LangValidateGenericMethodReturnOk},
  {"lang_validate_generic_method_return_mismatch", LangValidateGenericMethodReturnMismatch},
  {"lang_validate_generic_call_explicit", LangValidateGenericCallExplicit},
  {"lang_validate_generic_call_inferred", LangValidateGenericCallInferred},
  {"lang_validate_generic_call_infer_fail", LangValidateGenericCallInferFail},
  {"lang_validate_generic_call_type_mismatch", LangValidateGenericCallTypeMismatch},
  {"lang_validate_non_generic_call_type_args", LangValidateNonGenericCallTypeArgs},
  {"lang_validate_generic_type_args_mismatch", LangValidateGenericTypeArgsMismatch},
  {"lang_validate_generic_type_args_wrong_count", LangValidateGenericTypeArgsWrongCount},
  {"lang_validate_non_generic_type_args", LangValidateNonGenericTypeArgs},
  {"lang_validate_enum_type_args_rejected", LangValidateEnumTypeArgsRejected},
  {"lang_validate_module_not_type", LangValidateModuleNotType},
  {"lang_validate_function_not_type", LangValidateFunctionNotType},
  {"lang_validate_array_literal_shape_match", LangValidateArrayLiteralShapeMatch},
  {"lang_validate_array_literal_shape_mismatch", LangValidateArrayLiteralShapeMismatch},
  {"lang_validate_array_literal_nested_mismatch", LangValidateArrayLiteralNestedMismatch},
  {"lang_validate_array_literal_non_array_child", LangValidateArrayLiteralNonArrayChild},
  {"lang_validate_array_literal_empty_mismatch", LangValidateArrayLiteralEmptyMismatch},
  {"lang_validate_array_literal_element_mismatch", LangValidateArrayLiteralElementMismatch},
  {"lang_validate_array_literal_nested_element_mismatch", LangValidateArrayLiteralNestedElementMismatch},
  {"lang_validate_list_literal_element_mismatch", LangValidateListLiteralElementMismatch},
  {"lang_validate_nested_list_literal_element_mismatch", LangValidateNestedListLiteralElementMismatch},
  {"lang_validate_array_literal_scalar_target", LangValidateArrayLiteralScalarTarget},
  {"lang_validate_list_literal_scalar_target", LangValidateListLiteralScalarTarget},
  {"lang_validate_list_literal_ok", LangValidateListLiteralOk},
  {"lang_validate_if_condition_type_mismatch", LangValidateIfConditionTypeMismatch},
  {"lang_validate_if_chain_condition_type_mismatch", LangValidateIfChainConditionTypeMismatch},
  {"lang_validate_while_condition_type_mismatch", LangValidateWhileConditionTypeMismatch},
  {"lang_validate_for_condition_type_mismatch", LangValidateForConditionTypeMismatch},
  {"lang_validate_len_array_ok", LangValidateLenArrayOk},
  {"lang_validate_len_list_ok", LangValidateLenListOk},
  {"lang_validate_len_string_ok", LangValidateLenStringOk},
  {"lang_validate_str_from_i32_ok", LangValidateStrFromI32Ok},
  {"lang_validate_str_from_bool_ok", LangValidateStrFromBoolOk},
  {"lang_validate_str_from_string_fail", LangValidateStrFromStringFail},
  {"lang_validate_i32_from_string_ok", LangValidateI32FromStringOk},
  {"lang_validate_i32_from_i32_fail", LangValidateI32FromI32Fail},
  {"lang_validate_f64_from_string_ok", LangValidateF64FromStringOk},
  {"lang_validate_f64_from_f64_fail", LangValidateF64FromF64Fail},
  {"lang_validate_len_scalar_fail", LangValidateLenScalarFail},
  {"lang_validate_len_arg_count_fail", LangValidateLenArgCountFail},
  {"lang_validate_unary_type_mismatch", LangValidateUnaryTypeMismatch},
  {"lang_validate_binary_type_mismatch", LangValidateBinaryTypeMismatch},
  {"lang_validate_comparison_type_mismatch", LangValidateComparisonTypeMismatch},
  {"lang_validate_bitwise_type_mismatch", LangValidateBitwiseTypeMismatch},
  {"lang_validate_modulo_float_mismatch", LangValidateModuloFloatMismatch},
  {"lang_parse_comparisons", LangParsesComparisons},
  {"lang_parse_bitwise_precedence", LangParsesBitwisePrecedence},
  {"lang_parse_array_list_index", LangParsesArrayListAndIndex},
  {"lang_parse_artifact_literal", LangParsesArtifactLiteral},
  {"lang_parse_fn_literal", LangParsesFnLiteral},
  {"lang_parse_assignments", LangParsesAssignments},
  {"lang_ast_type_coverage", LangAstTypeCoverage},
  {"lang_parse_recover_in_block", LangParserRecoversInBlock},
  {"lang_parse_inc_dec", LangParsesIncDec},
  {"lang_parse_if_chain", LangParsesIfChain},
  {"lang_parse_if_else", LangParsesIfElse},
  {"lang_parse_while_loop", LangParsesWhileLoop},
  {"lang_parse_break_skip", LangParsesBreakSkip},
  {"lang_parse_for_loop", LangParsesForLoop},
  {"lang_parse_for_loop_post_inc", LangParsesForLoopPostInc},
};

} // namespace

static const TestSection kLangSections[] = {
  {"lang", kLangTests, sizeof(kLangTests) / sizeof(kLangTests[0])},
};

const TestSection* GetLangSections(size_t* count) {
  if (count) {
    *count = sizeof(kLangSections) / sizeof(kLangSections[0]);
  }
  return kLangSections;
}

} // namespace Simple::VM::Tests
