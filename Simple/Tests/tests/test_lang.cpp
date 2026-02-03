#include "lang_lexer.h"
#include "lang_parser.h"
#include "lang_validate.h"
#include "test_utils.h"

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
  const char* src = "x : i32 = 42; y : f32 = 3.5; s : string = \"hi\\n\"; c : char = '\\n';";
  Simple::Lang::Lexer lex(src);
  if (!lex.Lex()) return false;
  const auto& toks = lex.Tokens();
  bool saw_int = false;
  bool saw_float = false;
  bool saw_string = false;
  bool saw_char = false;
  for (const auto& tok : toks) {
    if (tok.kind == Simple::Lang::TokenKind::Integer) saw_int = true;
    if (tok.kind == Simple::Lang::TokenKind::Float) saw_float = true;
    if (tok.kind == Simple::Lang::TokenKind::String) saw_string = true;
    if (tok.kind == Simple::Lang::TokenKind::Char) saw_char = true;
  }
  return saw_int && saw_float && saw_string && saw_char;
}

bool LangParsesTypeLiterals() {
  Simple::Lang::TypeRef type;
  std::string error;
  if (!Simple::Lang::ParseTypeFromString("i32", &type, &error)) return false;
  if (type.name != "i32") return false;
  if (!type.dims.empty()) return false;

  Simple::Lang::TypeRef arr;
  if (!Simple::Lang::ParseTypeFromString("i32[10][]", &arr, &error)) return false;
  if (arr.dims.size() != 2) return false;
  if (!arr.dims[0].has_size || arr.dims[0].size != 10) return false;
  if (!arr.dims[1].is_list) return false;

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
  const char* src = "Color :: enum { Red } main : i32 () { return Color.Red; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumQualifiedDot() {
  const char* src = "Color :: enum { Red } main : i32 () { return Color::Red; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumUnqualified() {
  const char* src = "Color :: enum { Red } main : i32 () { return Red; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateEnumDuplicateMember() {
  const char* src = "Color :: enum { Red, Red }";
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
  const char* src = "main : i32 () { x : i32 = 1; }";
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
    "main : i32 () {"
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

bool LangValidateMutableFieldAssignOk() {
  const char* src =
    "Point :: artifact { x : i32 }"
    "main : void () { p : Point = { 1 }; p.x = 2; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
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
  {"lang_parse_type_literals", LangParsesTypeLiterals},
  {"lang_parse_func_decl", LangParsesFuncDecl},
  {"lang_parse_fn_keyword", LangParsesFnKeywordDecl},
  {"lang_parse_var_decl", LangParsesVarDecl},
  {"lang_parse_artifact_decl", LangParsesArtifactDecl},
  {"lang_parse_module_decl", LangParsesModuleDecl},
  {"lang_parse_enum_decl", LangParsesEnumDecl},
  {"lang_parse_return_expr", LangParsesReturnExpr},
  {"lang_parse_call_member", LangParsesCallAndMember},
  {"lang_parse_self", LangParsesSelf},
  {"lang_parse_qualified_member", LangParsesQualifiedMember},
  {"lang_parse_reject_double_colon_member", LangRejectsDoubleColonMember},
  {"lang_validate_enum_qualified", LangValidateEnumQualified},
  {"lang_validate_enum_qualified_dot", LangValidateEnumQualifiedDot},
  {"lang_validate_enum_unqualified", LangValidateEnumUnqualified},
  {"lang_validate_enum_duplicate", LangValidateEnumDuplicateMember},
  {"lang_validate_top_level_duplicate", LangValidateTopLevelDuplicate},
  {"lang_validate_local_duplicate_same_scope", LangValidateLocalDuplicateSameScope},
  {"lang_validate_local_duplicate_shadow_allowed", LangValidateLocalDuplicateShadowAllowed},
  {"lang_validate_for_loop_scope", LangValidateForLoopScope},
  {"lang_validate_artifact_duplicate_member", LangValidateArtifactDuplicateMember},
  {"lang_validate_module_duplicate_member", LangValidateModuleDuplicateMember},
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
  {"lang_validate_mutable_field_assign_ok", LangValidateMutableFieldAssignOk},
  {"lang_validate_self_outside_method", LangValidateSelfOutsideMethod},
  {"lang_validate_artifact_literal_too_many_positional", LangValidateArtifactLiteralTooManyPositional},
  {"lang_validate_artifact_literal_duplicate_named", LangValidateArtifactLiteralDuplicateNamed},
  {"lang_validate_artifact_literal_unknown_field", LangValidateArtifactLiteralUnknownField},
  {"lang_validate_artifact_literal_positional_then_named_duplicate", LangValidateArtifactLiteralPositionalThenNamedDuplicate},
  {"lang_validate_artifact_literal_named_ok", LangValidateArtifactLiteralNamedOk},
  {"lang_validate_index_float_literal", LangValidateIndexFloatLiteral},
  {"lang_validate_index_string_literal", LangValidateIndexStringLiteral},
  {"lang_validate_index_literal_base", LangValidateIndexLiteralBase},
  {"lang_validate_index_int_ok", LangValidateIndexIntOk},
  {"lang_validate_call_arg_count", LangValidateCallArgCount},
  {"lang_validate_call_non_function", LangValidateCallNonFunction},
  {"lang_validate_call_module_func_count", LangValidateCallModuleFuncCount},
  {"lang_validate_call_module_var", LangValidateCallModuleVar},
  {"lang_validate_call_method_arg_count", LangValidateCallMethodArgCount},
  {"lang_validate_call_field_as_method", LangValidateCallFieldAsMethod},
  {"lang_validate_call_fn_literal_count", LangValidateCallFnLiteralCount},
  {"lang_validate_call_fn_literal_ok", LangValidateCallFnLiteralOk},
  {"lang_parse_comparisons", LangParsesComparisons},
  {"lang_parse_array_list_index", LangParsesArrayListAndIndex},
  {"lang_parse_artifact_literal", LangParsesArtifactLiteral},
  {"lang_parse_fn_literal", LangParsesFnLiteral},
  {"lang_parse_assignments", LangParsesAssignments},
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
