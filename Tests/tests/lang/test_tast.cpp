#include "test_utils.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST/lower_cast.h"
#include "CAST/parser.h"
#include "RAST/resolver.h"
#include "TAST/calls.h"
#include "TAST/control_flow.h"
#include "TAST/expressions.h"
#include "TAST/literals.h"
#include "TAST/mutability.h"
#include "TAST/statements.h"
#include "TAST/type_checker.h"
#include "TAST/types.h"
#include "TAST/abi.h"
#include "TAST/generics.h"

namespace Simple::VM::Tests {
namespace {

bool LangTastTypeUtilitiesClassifyAndCloneTypes() {
  const Simple::Lang::AST::TypeRef simple_i32 = Simple::Lang::TAST::MakeSimpleType("i32");
  if (simple_i32.name != "i32" || simple_i32.pointer_depth != 0 || simple_i32.is_proc || !simple_i32.dims.empty()) {
    return false;
  }
  const Simple::Lang::AST::TypeRef list_i32 = Simple::Lang::TAST::MakeListType("i32");
  if (list_i32.name != "i32" || list_i32.dims.size() != 1 || !list_i32.dims[0].is_list) return false;
  Simple::Lang::AST::TypeRef list_element;
  if (!Simple::Lang::TAST::CloneElementType(list_i32, &list_element)) return false;
  if (list_element.name != "i32" || !list_element.dims.empty()) return false;
  if (Simple::Lang::TAST::CloneElementType(simple_i32, &list_element)) return false;

  Simple::Lang::AST::TypeRef proc;
  proc.is_proc = true;
  proc.proc_return = std::make_unique<Simple::Lang::AST::TypeRef>();
  proc.proc_return->name = "bool";
  Simple::Lang::AST::TypeRef param;
  param.name = "i32";
  proc.proc_params.push_back(param);

  Simple::Lang::AST::TypeRef clone;
  if (!Simple::Lang::TAST::CloneTypeRef(proc, &clone)) return false;
  if (!clone.is_proc || !clone.proc_return || clone.proc_return->name != "bool") return false;
  if (clone.proc_params.size() != 1 || clone.proc_params[0].name != "i32") return false;
  if (!Simple::Lang::TAST::TypeEquals(proc, clone)) return false;
  std::vector<Simple::Lang::AST::TypeRef> cloned_params;
  if (!Simple::Lang::TAST::CloneTypeVector(proc.proc_params, &cloned_params)) return false;
  if (cloned_params.size() != 1 || cloned_params[0].name != "i32") return false;

  Simple::Lang::AST::TypeRef fixed_array;
  fixed_array.name = "i32";
  fixed_array.dims.push_back({false, true, 4});
  Simple::Lang::AST::TypeRef other_array;
  if (!Simple::Lang::TAST::CloneTypeRef(fixed_array, &other_array)) return false;
  if (!Simple::Lang::TAST::TypeDimsEqual(fixed_array.dims, other_array.dims)) return false;
  other_array.dims[0].size = 8;
  if (Simple::Lang::TAST::TypeEquals(fixed_array, other_array)) return false;

  auto scalar_i32 = Simple::Lang::TAST::MakeSimpleType("i32");
  auto pointer_i32 = scalar_i32;
  pointer_i32.pointer_depth = 1;
  auto generic_box = Simple::Lang::TAST::MakeSimpleType("Box");
  generic_box.type_args.push_back(scalar_i32);
  auto scalar_list_i32 = scalar_i32;
  scalar_list_i32.dims.push_back(Simple::Lang::AST::TypeDim{true, false, 0});
  auto proc_type = scalar_i32;
  proc_type.is_proc = true;

  std::string cast_target;
  auto string_type = Simple::Lang::TAST::MakeSimpleType("string");

  return Simple::Lang::TAST::IsLenCompatibleType(string_type) &&
         Simple::Lang::TAST::IsLenCompatibleType(scalar_list_i32) &&
         !Simple::Lang::TAST::IsLenCompatibleType(scalar_i32) &&
         Simple::Lang::TAST::IsScalarType(scalar_i32) &&
         !Simple::Lang::TAST::IsScalarType(pointer_i32) &&
         !Simple::Lang::TAST::IsScalarType(generic_box) &&
         !Simple::Lang::TAST::IsScalarType(scalar_list_i32) &&
         !Simple::Lang::TAST::IsScalarType(proc_type) &&
         Simple::Lang::TAST::IsIntegerScalarTypeName("u64") &&
         Simple::Lang::TAST::IsFloatTypeName("f32") &&
         Simple::Lang::TAST::IsNumericTypeName("char") &&
         Simple::Lang::TAST::IsBoolTypeName("bool") &&
         Simple::Lang::TAST::IsStringTypeName("string") &&
         Simple::Lang::TAST::IsPrimitiveTypeName("i32") &&
         Simple::Lang::TAST::IsListMethodName("push") &&
         !Simple::Lang::TAST::IsListMethodName("missing") &&
         Simple::Lang::TAST::GetAtCastTargetName("@f64", &cast_target) &&
         cast_target == "f64" &&
         !Simple::Lang::TAST::IsPrimitiveTypeName("Box");
}

bool LangTastPrimitiveCastArgTypeRules() {
  std::string error;
  auto i32 = Simple::Lang::TAST::MakeSimpleType("i32");
  auto string_type = Simple::Lang::TAST::MakeSimpleType("string");
  auto list_i32 = i32;
  list_i32.dims.push_back(Simple::Lang::AST::TypeDim{true, false, 0});
  if (!Simple::Lang::TAST::CheckPrimitiveCastArgType("string", i32, &error)) return false;
  if (Simple::Lang::TAST::CheckPrimitiveCastArgType("string", string_type, &error)) return false;
  if (error.find("string cast expects numeric or bool argument") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckPrimitiveCastArgType("i32", string_type, &error)) return false;
  if (Simple::Lang::TAST::CheckPrimitiveCastArgType("i64", string_type, &error)) return false;
  if (error.find("i64 cast from string is unsupported") == std::string::npos) return false;
  if (Simple::Lang::TAST::CheckPrimitiveCastArgType("i32", list_i32, &error)) return false;
  return error.find("i32 cast expects scalar argument") != std::string::npos;
}

bool LangTastCallsCheckSingleArgCallCount() {
  std::string error;
  if (!Simple::Lang::TAST::CheckSingleArgCallCount("len", 1, &error)) return false;
  if (Simple::Lang::TAST::CheckSingleArgCallCount("len", 2, &error)) return false;
  return error.find("call argument count mismatch for len: expected 1, got 2") != std::string::npos;
}

bool LangTastCallsCheckFormatAndPrintArgTypes() {
  std::string error;
  auto i32 = Simple::Lang::TAST::MakeSimpleType("i32");
  auto char_type = Simple::Lang::TAST::MakeSimpleType("char");
  auto artifact = Simple::Lang::TAST::MakeSimpleType("Point");
  auto list_i32 = i32;
  list_i32.dims.push_back(Simple::Lang::AST::TypeDim{true, false, 0});
  std::vector<Simple::Lang::AST::TypeRef> args = {i32, Simple::Lang::TAST::MakeSimpleType("string")};
  if (!Simple::Lang::TAST::CheckFormatCallArgTypes(args, &error)) return false;
  args = {artifact};
  if (Simple::Lang::TAST::CheckFormatCallArgTypes(args, &error)) return false;
  if (error.find("format supports numeric, bool, or string") == std::string::npos) return false;
  args = {list_i32};
  if (Simple::Lang::TAST::CheckIoPrintCallArgTypes(args, &error)) return false;
  if (error.find("IO.print expects scalar argument") == std::string::npos) return false;
  args = {char_type};
  if (!Simple::Lang::TAST::CheckIoPrintCallArgTypes(args, &error)) return false;
  args = {artifact};
  if (Simple::Lang::TAST::CheckIoPrintCallArgTypes(args, &error)) return false;
  return error.find("IO.print supports numeric, bool, char, or string") != std::string::npos;
}

bool LangTastCallsCheckScalarArgTypes() {
  std::string error;
  auto i32 = Simple::Lang::TAST::MakeSimpleType("i32");
  auto list_i32 = i32;
  list_i32.dims.push_back(Simple::Lang::AST::TypeDim{true, false, 0});
  std::vector<Simple::Lang::AST::TypeRef> args = {i32, Simple::Lang::TAST::MakeSimpleType("bool")};
  if (!Simple::Lang::TAST::CheckScalarCallArgTypes(args, "scalar only", &error)) return false;
  args.push_back(list_i32);
  if (Simple::Lang::TAST::CheckScalarCallArgTypes(args, "scalar only", &error)) return false;
  return error == "scalar only";
}

bool LangTastCallsCheckReservedDlOpenArgTypes() {
  std::string error;
  std::vector<Simple::Lang::AST::TypeRef> args = {Simple::Lang::TAST::MakeSimpleType("string")};
  if (!Simple::Lang::TAST::CheckReservedDlOpenArgTypes(args, &error)) return false;
  args.push_back(Simple::Lang::TAST::MakeSimpleType("manifest"));
  if (!Simple::Lang::TAST::CheckReservedDlOpenArgTypes(args, &error)) return false;
  args.clear();
  if (Simple::Lang::TAST::CheckReservedDlOpenArgTypes(args, &error)) return false;
  if (error.find("DL.open expects (string) or (string, manifest)") == std::string::npos) return false;
  args = {Simple::Lang::TAST::MakeSimpleType("i32")};
  if (Simple::Lang::TAST::CheckReservedDlOpenArgTypes(args, &error)) return false;
  return error.find("DL.open expects first argument string path") != std::string::npos;
}

bool LangTastCallsCheckReservedFileArgTypes() {
  std::string error;
  auto i32 = Simple::Lang::TAST::MakeSimpleType("i32");
  auto string_type = Simple::Lang::TAST::MakeSimpleType("string");
  auto i32_buffer = i32;
  i32_buffer.dims.push_back(Simple::Lang::AST::TypeDim{});
  std::vector<Simple::Lang::AST::TypeRef> args = {string_type, i32};
  if (!Simple::Lang::TAST::CheckReservedFileCallArgTypes("open", args, &error)) return false;
  args = {i32, i32};
  if (Simple::Lang::TAST::CheckReservedFileCallArgTypes("open", args, &error)) return false;
  if (error.find("File.open expects (string, i32)") == std::string::npos) return false;
  args = {i32_buffer};
  if (Simple::Lang::TAST::CheckReservedFileCallArgTypes("close", args, &error)) return false;
  if (error.find("File.close expects (i32)") == std::string::npos) return false;
  args = {i32, i32_buffer, i32};
  if (!Simple::Lang::TAST::CheckReservedFileCallArgTypes("read", args, &error)) return false;
  args = {i32, i32, i32};
  if (Simple::Lang::TAST::CheckReservedFileCallArgTypes("write", args, &error)) return false;
  return error.find("File.write expects (i32, i32[], i32)") != std::string::npos;
}

bool LangTastCallsCheckReservedIoBufferArgTypes() {
  std::string error;
  auto i32 = Simple::Lang::TAST::MakeSimpleType("i32");
  auto i32_buffer = i32;
  i32_buffer.dims.push_back(Simple::Lang::AST::TypeDim{});
  std::vector<Simple::Lang::AST::TypeRef> args = {i32};
  if (!Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_new", args, &error)) return false;
  args[0] = Simple::Lang::TAST::MakeSimpleType("string");
  if (Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_new", args, &error)) return false;
  if (error.find("IO.buffer_new expects (i32)") == std::string::npos) return false;
  args = {i32_buffer, i32, i32};
  if (!Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_fill", args, &error)) return false;
  args = {i32_buffer, Simple::Lang::TAST::MakeSimpleType("string"), i32};
  if (Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_fill", args, &error)) return false;
  if (error.find("IO.buffer_fill expects (i32[], i32, i32)") == std::string::npos) return false;
  args = {i32_buffer, i32_buffer, i32};
  if (!Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_copy", args, &error)) return false;
  args = {i32_buffer, i32, i32};
  if (Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_copy", args, &error)) return false;
  return error.find("IO.buffer_copy expects (i32[], i32[], i32)") != std::string::npos;
}

bool LangTastCallsCheckReservedMathArgTypes() {
  std::string error;
  std::vector<Simple::Lang::AST::TypeRef> args = {Simple::Lang::TAST::MakeSimpleType("i32")};
  if (!Simple::Lang::TAST::CheckReservedMathCallArgTypes("abs", args, &error)) return false;
  args[0] = Simple::Lang::TAST::MakeSimpleType("f64");
  if (Simple::Lang::TAST::CheckReservedMathCallArgTypes("abs", args, &error)) return false;
  if (error.find("Math.abs expects i32 or i64 argument") == std::string::npos) return false;
  args = {Simple::Lang::TAST::MakeSimpleType("f32"), Simple::Lang::TAST::MakeSimpleType("f64")};
  if (Simple::Lang::TAST::CheckReservedMathCallArgTypes("min", args, &error)) return false;
  return error.find("Math.min expects two numeric arguments of the same type") != std::string::npos;
}

bool LangTastCallsCheckReservedTimeArgTypes() {
  std::string error;
  std::vector<Simple::Lang::AST::TypeRef> no_args;
  if (!Simple::Lang::TAST::CheckReservedTimeCallArgTypes("mono_ns", no_args, &error)) return false;
  std::vector<Simple::Lang::AST::TypeRef> args = {Simple::Lang::TAST::MakeSimpleType("i32")};
  if (Simple::Lang::TAST::CheckReservedTimeCallArgTypes("wall_ns", args, &error)) return false;
  if (error.find("Time.wall_ns expects no arguments") == std::string::npos) return false;
  args[0] = Simple::Lang::TAST::MakeSimpleType("i64");
  if (!Simple::Lang::TAST::CheckReservedTimeCallArgTypes("formatWallNs", args, &error)) return false;
  args[0] = Simple::Lang::TAST::MakeSimpleType("string");
  if (Simple::Lang::TAST::CheckReservedTimeCallArgTypes("formatWallNs", args, &error)) return false;
  return error.find("Time.formatWallNs expects (i64)") != std::string::npos;
}

bool LangTastCallsCheckTypeArgCounts() {
  std::string error;
  if (!Simple::Lang::TAST::CheckCallTypeArgCount(2, 2, &error)) return false;
  if (!Simple::Lang::TAST::CheckCallTypeArgCount(2, 0, &error)) return false;
  if (Simple::Lang::TAST::CheckCallTypeArgCount(2, 1, &error)) return false;
  if (error.find("generic type argument count mismatch") == std::string::npos) return false;
  if (Simple::Lang::TAST::CheckCallTypeArgCount(0, 1, &error)) return false;
  return error.find("non-generic call cannot take type arguments") != std::string::npos;
}

bool LangTastFnLiteralChecksTargetProcedureShape() {
  Simple::Lang::AST::Expr fn;
  fn.kind = Simple::Lang::AST::ExprKind::FnLiteral;
  Simple::Lang::AST::ParamDecl param;
  param.name = "x";
  param.type.name = "i32";
  fn.fn_params.push_back(param);

  Simple::Lang::AST::TypeRef target;
  target.is_proc = true;
  target.proc_params.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  target.proc_return = std::make_unique<Simple::Lang::AST::TypeRef>(Simple::Lang::TAST::MakeSimpleType("void"));

  std::string error;
  if (!Simple::Lang::TAST::CheckFnLiteralAgainstType(fn, target, &error)) return false;
  target.proc_params[0].name = "i64";
  if (Simple::Lang::TAST::CheckFnLiteralAgainstType(fn, target, &error)) return false;
  if (error.find("parameter type mismatch") == std::string::npos) return false;
  target.proc_params[0].name = "i32";
  fn.fn_body_tokens.push_back({Simple::Lang::TokenKind::KwReturn, "return", 0, 0});
  fn.fn_body_tokens.push_back({Simple::Lang::TokenKind::LParen, "(", 0, 0});
  fn.fn_body_tokens.push_back({Simple::Lang::TokenKind::RParen, ")", 0, 0});
  fn.fn_body_tokens.push_back({Simple::Lang::TokenKind::LBrace, "{", 0, 0});
  if (Simple::Lang::TAST::CheckFnLiteralAgainstType(fn, target, &error)) return false;
  return error.find("nested fn literals are not supported") != std::string::npos;
}

bool LangSplitTastAbiAndGenericsSmoke() {
  std::unordered_set<std::string> collected;
  std::string generic_error;
  if (!Simple::Lang::TAST::CollectTypeParams({"T", "U"}, &collected, &generic_error)) return false;
  if (collected.size() != 2 || collected.find("T") == collected.end()) return false;
  if (!Simple::Lang::TAST::CollectTypeParamsMerged({"T"}, {"U"}, &collected, &generic_error)) return false;
  if (Simple::Lang::TAST::CollectTypeParamsMerged({"T"}, {"T"}, &collected, &generic_error)) return false;
  if (generic_error.find("duplicate generic parameter: T") == std::string::npos) return false;

  Simple::Lang::AST::TypeRef scalar;
  scalar.name = "i64";
  std::string error;
  if (!Simple::Lang::TAST::CheckAbiShape(scalar, false, &error)) return false;

  Simple::Lang::AST::TypeRef box;
  box.name = "Box";
  Simple::Lang::AST::TypeRef generic;
  generic.name = "T";
  box.type_args.push_back(generic);
  Simple::Lang::AST::TypeRef replacement;
  replacement.name = "string";
  Simple::Lang::TAST::GenericSubstitutionMap substitutions;
  substitutions["T"] = replacement;
  Simple::Lang::AST::TypeRef out;
  return Simple::Lang::TAST::SubstituteGenericTypes(box, substitutions, &out) &&
         out.type_args.size() == 1 && out.type_args[0].name == "string";
}

bool LangTastCheckerAcceptsResolvedProgram() {
  const char* src =
      "extern Ray.InitWindow : void (w : i32, h : i32)\n"
      "count : i32 = 1\n"
      "main : i32 () { return 42; }";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  return typed.resolved == &resolved &&
         !typed.typed_exprs.empty() &&
         !typed.typed_stmts.empty() &&
         !typed.expr_types.empty() &&
         typed.mutability_facts["count"] == Simple::Lang::Mutability::Mutable &&
         typed.abi_facts.extern_param_types.size() == 2 &&
         typed.abi_facts.extern_return_types.size() == 1;
}


bool LangTastCheckerRejectsTypeMismatch() {
  const char* src = "main : i32 () { x : i32 = true; return x; }";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  return error.find("initializer type mismatch") != std::string::npos;
}


bool LangTastFormatStringCountsPlaceholders() {
  size_t count = 0;
  std::string error;
  if (!Simple::Lang::TAST::CountFormatPlaceholders("a {} b {}", &count, &error)) return false;
  if (count != 2) return false;
  if (Simple::Lang::TAST::CountFormatPlaceholders("a { b", &count, &error)) return false;
  if (error.find("expected '{}' placeholder") == std::string::npos) return false;
  if (Simple::Lang::TAST::CountFormatPlaceholders("a } b", &count, &error)) return false;
  return error.find("unmatched '}'") != std::string::npos;
}

bool LangTastLiteralCompatibilityAcceptsFlexibleArrayAndScalarLiterals() {
  Simple::Lang::AST::TypeRef expected_array;
  expected_array.name = "i32";
  expected_array.dims.push_back({false, false, 0});
  Simple::Lang::AST::TypeRef actual_array;
  actual_array.name = "i32";
  actual_array.dims.push_back({false, true, 3});
  Simple::Lang::AST::Expr brace;
  brace.kind = Simple::Lang::AST::ExprKind::ArrayLiteral;
  if (!Simple::Lang::TAST::TypesCompatibleForExpr(expected_array, actual_array, brace)) return false;

  Simple::Lang::AST::TypeRef expected_scalar;
  expected_scalar.name = "i64";
  Simple::Lang::AST::TypeRef actual_scalar;
  actual_scalar.name = "i32";
  Simple::Lang::AST::Expr literal;
  literal.kind = Simple::Lang::AST::ExprKind::Literal;
  literal.literal_kind = Simple::Lang::AST::LiteralKind::Integer;
  literal.text = "1";
  if (!Simple::Lang::TAST::IsLiteralCompatibleWithScalarType(literal, expected_scalar)) return false;
  if (!Simple::Lang::TAST::TypesCompatibleForExpr(expected_scalar, actual_scalar, literal)) return false;

  expected_scalar.name = "bool";
  return !Simple::Lang::TAST::TypesCompatibleForExpr(expected_scalar, actual_scalar, literal);
}

bool LangTastLiteralHelpersClassifyBraceAndListShapes() {
  Simple::Lang::AST::Expr fixed_array;
  fixed_array.kind = Simple::Lang::AST::ExprKind::ArrayLiteral;
  fixed_array.children.resize(2);
  fixed_array.children[0].kind = Simple::Lang::AST::ExprKind::Literal;
  fixed_array.children[1].kind = Simple::Lang::AST::ExprKind::Literal;
  std::vector<Simple::Lang::AST::TypeDim> fixed_dims = {{false, true, 2}};
  std::string shape_error;
  if (!Simple::Lang::TAST::CheckArrayLiteralShape(fixed_array, fixed_dims, 0, &shape_error)) return false;
  fixed_dims[0].size = 3;
  if (Simple::Lang::TAST::CheckArrayLiteralShape(fixed_array, fixed_dims, 0, &shape_error)) return false;
  if (shape_error.find("array literal size does not match fixed dimensions") == std::string::npos) return false;

  Simple::Lang::AST::Expr list;
  list.kind = Simple::Lang::AST::ExprKind::ListLiteral;
  if (!Simple::Lang::TAST::IsListLiteralExpr(list)) return false;

  Simple::Lang::AST::Expr array;
  array.kind = Simple::Lang::AST::ExprKind::ArrayLiteral;
  if (!Simple::Lang::TAST::IsPositionalBraceLiteralExpr(array)) return false;

  Simple::Lang::AST::Expr positional_artifact;
  positional_artifact.kind = Simple::Lang::AST::ExprKind::ArtifactLiteral;
  if (!Simple::Lang::TAST::IsPositionalBraceLiteralExpr(positional_artifact)) return false;

  Simple::Lang::AST::Expr named_artifact;
  named_artifact.kind = Simple::Lang::AST::ExprKind::ArtifactLiteral;
  named_artifact.field_names.push_back("x");
  return !Simple::Lang::TAST::IsPositionalBraceLiteralExpr(named_artifact);
}

bool LangTastLiteralTypingUsesExpectedType() {
  Simple::Lang::AST::Expr expr;
  expr.kind = Simple::Lang::ExprKind::Literal;
  expr.literal_kind = Simple::Lang::LiteralKind::Integer;
  expr.text = "42";

  Simple::Lang::AST::TypeRef expected;
  expected.name = "u64";
  Simple::Lang::AST::TypeRef actual;
  std::string error;
  if (!Simple::Lang::TAST::InferLiteralType(expr, &expected, &actual, &error)) return false;
  if (actual.name != "u64") return false;
  if (!Simple::Lang::TAST::InferLiteralType(expr, nullptr, &actual, &error)) return false;
  return actual.name == "i32";
}


bool LangTastLiteralTypingRejectsInvalidExpectedType() {
  Simple::Lang::AST::Expr expr;
  expr.kind = Simple::Lang::ExprKind::Literal;
  expr.literal_kind = Simple::Lang::LiteralKind::String;
  expr.text = "nope";

  Simple::Lang::AST::TypeRef expected;
  expected.name = "i32";
  Simple::Lang::AST::TypeRef actual;
  std::string error;
  if (Simple::Lang::TAST::InferLiteralType(expr, &expected, &actual, &error)) return false;
  return error.find("literal is not compatible") != std::string::npos;
}


bool LangTastLiteralTypingRejectsNonLiteral() {
  Simple::Lang::AST::Expr expr;
  expr.kind = Simple::Lang::ExprKind::Identifier;
  expr.text = "x";

  Simple::Lang::AST::TypeRef actual;
  std::string error;
  if (Simple::Lang::TAST::InferLiteralType(expr, nullptr, &actual, &error)) return false;
  return error.find("expected literal expression") != std::string::npos;
}


bool LangTastCheckReturnFlowRejectsFallthrough() {
  std::vector<Simple::Lang::AST::Stmt> body;
  Simple::Lang::AST::Stmt return_stmt;
  return_stmt.kind = Simple::Lang::AST::StmtKind::Return;
  std::string error;
  if (Simple::Lang::TAST::CheckReturnFlow(body, true, &error)) return false;
  if (error.find("not all paths return a value") == std::string::npos) return false;
  body.push_back(return_stmt);
  return Simple::Lang::TAST::CheckReturnFlow(body, true, &error);
}


bool LangTastControlFlowExtractsSwitchBranchValues() {
  Simple::Lang::AST::SwitchBranch branch;
  branch.has_inline_value = true;
  branch.value.kind = Simple::Lang::AST::ExprKind::Literal;
  const Simple::Lang::AST::Expr* value = nullptr;
  std::string error;
  Simple::Lang::AST::TypeRef bool_type;
  bool_type.name = "bool";
  if (!Simple::Lang::TAST::CheckConditionType(bool_type, &error)) return false;
  bool_type.name = "i32";
  if (Simple::Lang::TAST::CheckConditionType(bool_type, &error)) return false;
  if (error.find("condition must be bool") == std::string::npos) return false;

  if (!Simple::Lang::TAST::GetSwitchBranchValueExpr(branch, false, &value, &error)) return false;
  if (value != &branch.value) return false;
  if (Simple::Lang::TAST::GetSwitchBranchValueExpr(branch, true, &value, &error)) return false;
  if (error.find("must use 'return'") == std::string::npos) return false;

  Simple::Lang::AST::SwitchBranch block_branch;
  block_branch.is_block = true;
  Simple::Lang::AST::Stmt ret;
  ret.kind = Simple::Lang::AST::StmtKind::Return;
  ret.has_return_expr = true;
  ret.expr.kind = Simple::Lang::AST::ExprKind::Literal;
  block_branch.block.push_back(ret);
  if (!Simple::Lang::TAST::GetSwitchBranchValueExpr(block_branch, true, &value, &error)) return false;
  if (value != &block_branch.block.back().expr) return false;
  block_branch.block.back().has_return_expr = false;
  if (Simple::Lang::TAST::GetSwitchBranchValueExpr(block_branch, false, &value, &error)) return false;
  return error.find("block must end with a return value") != std::string::npos;
}

bool LangTastControlFlowChecksFunctionReturns() {
  Simple::Lang::AST::FuncDecl fn;
  fn.name = "needs_return";
  fn.return_type = Simple::Lang::TAST::MakeSimpleType("i32");
  std::string error;
  if (Simple::Lang::TAST::CheckFunctionReturnFlow(fn, &error)) return false;
  if (error.find("non-void function does not return on all paths") == std::string::npos) return false;
  fn.name = "main";
  if (!Simple::Lang::TAST::CheckFunctionReturnFlow(fn, &error)) return false;
  fn.name = "void_fn";
  fn.return_type = Simple::Lang::TAST::MakeSimpleType("void");
  return Simple::Lang::TAST::CheckFunctionReturnFlow(fn, &error);
}

bool LangTastControlFlowTracksReturnsAndBreaks() {
  const char* src =
      "main : i32 () {\n"
      "  while (true) {\n"
      "    if (true) { break; } else { skip; }\n"
      "  }\n"
      "  if (true) { return 1; } else { return 2; }\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  const auto& body = ast_program.decls[0].func.body;
  Simple::Lang::TAST::Flow while_flow = Simple::Lang::TAST::AnalyzeStmtFlow(body[0]);
  Simple::Lang::TAST::Flow block_flow = Simple::Lang::TAST::AnalyzeBlockFlow(body);
  return while_flow.may_break &&
         while_flow.may_skip &&
         !while_flow.always_returns &&
         block_flow.always_returns &&
         !block_flow.may_fallthrough;
}


bool LangTastExpressionOperatorsValidateScalarAndCompoundAssign() {
  Simple::Lang::AST::Expr identifier;
  identifier.kind = Simple::Lang::AST::ExprKind::Identifier;
  if (!Simple::Lang::TAST::IsAddressableExpr(identifier)) return false;
  Simple::Lang::AST::Expr call_expr;
  call_expr.kind = Simple::Lang::AST::ExprKind::Call;
  if (Simple::Lang::TAST::IsAddressableExpr(call_expr)) return false;

  Simple::Lang::AST::TypeRef i32;
  i32.name = "i32";
  Simple::Lang::AST::TypeRef bool_type;
  bool_type.name = "bool";
  Simple::Lang::AST::TypeRef string_type;
  string_type.name = "string";
  Simple::Lang::AST::TypeRef list_type;
  list_type.name = "i32";
  list_type.dims.push_back({true, false, 0});

  std::string error;
  if (!Simple::Lang::TAST::RequireScalar(i32, "+", &error)) return false;
  if (Simple::Lang::TAST::RequireScalar(list_type, "+", &error)) return false;
  if (error.find("requires scalar operands") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckCompoundAssignOp("+", i32, i32, &error)) return false;
  if (!Simple::Lang::TAST::CheckCompoundAssignOp("&&", bool_type, bool_type, &error)) return false;
  if (Simple::Lang::TAST::CheckCompoundAssignOp("==", string_type, string_type, &error)) return false;
  if (error.find("does not support string operands") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckUnaryOpTypeRules("!", bool_type, identifier, &error)) return false;
  if (Simple::Lang::TAST::CheckUnaryOpTypeRules("&", i32, call_expr, &error)) return false;
  if (error.find("address-of requires assignable expression") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckBinaryOpTypeRules("+", i32, i32, identifier, identifier, &error)) return false;
  if (Simple::Lang::TAST::CheckBinaryOpTypeRules("==", string_type, string_type, identifier, identifier, &error)) return false;
  return error.find("does not support string operands") != std::string::npos;
}

bool LangTastCheckExpressionShapeValidatesIdentifiers() {
  Simple::Lang::AST::Expr ident;
  ident.kind = Simple::Lang::AST::ExprKind::Identifier;
  ident.text = "x";
  std::string error;
  if (!Simple::Lang::TAST::CheckExpressionShape(ident, &error)) return false;
  ident.text.clear();
  return !Simple::Lang::TAST::CheckExpressionShape(ident, &error) &&
         error.find("identifier expression missing name") != std::string::npos;
}


bool LangTastCheckAbiShapeRejectsGenericTypes() {
  Simple::Lang::AST::TypeRef scalar;
  scalar.name = "i32";
  std::string error;
  if (!Simple::Lang::TAST::CheckAbiShape(scalar, false, &error)) return false;
  Simple::Lang::AST::TypeRef generic;
  generic.name = "Box";
  generic.type_args.push_back(scalar);
  if (Simple::Lang::TAST::CheckAbiShape(generic, false, &error)) return false;
  if (error.find("extern ABI type shape is not supported") == std::string::npos) return false;

  Simple::Lang::AST::TypeRef mapped;
  if (!Simple::Lang::TAST::NativeTypeToLangType(Simple::Byte::TypeKind::I64, &mapped)) return false;
  if (mapped.name != "i64" || !mapped.dims.empty()) return false;
  if (!Simple::Lang::TAST::NativeTypeToLangType(Simple::Byte::TypeKind::Ref, &mapped)) return false;
  if (mapped.name != "i32" || mapped.dims.size() != 1 || !mapped.dims[0].is_list) return false;

  std::unordered_set<std::string> enum_types = {"Mode"};
  std::unordered_map<std::string, const Simple::Lang::AST::ArtifactDecl*> artifacts;
  Simple::Lang::AST::ArtifactDecl point;
  point.name = "Point";
  Simple::Lang::AST::VarDecl x;
  x.name = "x";
  x.type = Simple::Lang::TAST::MakeSimpleType("i32");
  point.fields.push_back(x);
  artifacts[point.name] = &point;
  Simple::Lang::AST::TypeRef point_type = Simple::Lang::TAST::MakeSimpleType("Point");
  if (!Simple::Lang::TAST::IsSupportedDlAbiType(point_type, enum_types, artifacts, false)) return false;
  Simple::Lang::AST::TypeRef enum_type = Simple::Lang::TAST::MakeSimpleType("Mode");
  if (!Simple::Lang::TAST::IsSupportedDlAbiType(enum_type, enum_types, artifacts, false)) return false;
  Simple::Lang::AST::ExternDecl ext;
  ext.module = "ffi";
  ext.name = "add";
  ext.return_type = Simple::Lang::TAST::MakeSimpleType("i32");
  Simple::Lang::AST::ParamDecl param;
  param.name = "x";
  param.type = point_type;
  ext.params.push_back(param);
  if (!Simple::Lang::TAST::CheckDlDynamicSignature(ext, enum_types, artifacts, &error)) return false;
  point.fields[0].type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  if (Simple::Lang::TAST::IsSupportedDlAbiType(point_type, enum_types, artifacts, false)) return false;
  if (Simple::Lang::TAST::CheckDlDynamicSignature(ext, enum_types, artifacts, &error)) return false;
  return error.find("dynamic DL parameter type for 'ffi.add' is not ABI-supported") != std::string::npos;
}


bool LangTastSubstituteGenericTypesRewritesNestedArgs() {
  Simple::Lang::AST::TypeRef input;
  input.name = "Box";
  Simple::Lang::AST::TypeRef inner;
  inner.name = "T";
  input.type_args.push_back(inner);

  Simple::Lang::AST::TypeRef replacement;
  replacement.name = "i32";
  Simple::Lang::TAST::GenericSubstitutionMap substitutions;
  substitutions["T"] = replacement;

  Simple::Lang::AST::TypeRef output;
  if (!Simple::Lang::TAST::SubstituteGenericTypes(input, substitutions, &output)) return false;
  if (output.name != "Box" || output.type_args.size() != 1 || output.type_args[0].name != "i32") return false;

  Simple::Lang::AST::TypeRef pointer_to_t;
  pointer_to_t.name = "T";
  pointer_to_t.pointer_depth = 1;
  Simple::Lang::AST::TypeRef applied;
  if (!Simple::Lang::TAST::SubstituteTypeParams(pointer_to_t, substitutions, &applied)) return false;
  if (applied.name != "i32" || applied.pointer_depth != 1) return false;

  Simple::Lang::AST::ArtifactDecl artifact;
  artifact.name = "Box";
  artifact.generics.push_back("T");
  Simple::Lang::AST::TypeRef instance;
  instance.name = "Box";
  instance.type_args.push_back(replacement);
  Simple::Lang::TAST::GenericSubstitutionMap map;
  std::string error;
  if (!Simple::Lang::TAST::BuildArtifactTypeParamMap(instance, &artifact, &map, &error)) return false;
  if (map.size() != 1 || map["T"].name != "i32") return false;
  std::vector<std::string> explicit_params = {"T"};
  std::vector<Simple::Lang::AST::TypeRef> explicit_args = {replacement};
  if (!Simple::Lang::TAST::BuildExplicitTypeArgMap(explicit_params, explicit_args, &map, &error)) return false;
  if (map.size() != 1 || map["T"].name != "i32") return false;
  explicit_args.push_back(replacement);
  if (Simple::Lang::TAST::BuildExplicitTypeArgMap(explicit_params, explicit_args, &map, &error)) return false;
  if (error.find("generic type argument count mismatch") == std::string::npos) return false;

  Simple::Lang::AST::TypeRef generic_param;
  generic_param.name = "T";
  generic_param.dims.push_back({false, true, 2});
  Simple::Lang::AST::TypeRef array_arg;
  array_arg.name = "string";
  array_arg.dims.push_back({false, true, 2});
  Simple::Lang::TAST::GenericSubstitutionMap inferred;
  std::unordered_set<std::string> type_params = {"T"};
  if (!Simple::Lang::TAST::UnifyTypeParams(generic_param, array_arg, type_params, &inferred)) return false;
  return inferred.size() == 1 && inferred["T"].name == "string" && inferred["T"].dims.empty();
}


bool LangTastMutabilityChecksAssignments() {
  std::vector<std::unordered_map<std::string, int>> scopes;
  std::string error;
  if (!Simple::Lang::TAST::AddLocal(scopes, "x", 1, &error)) return false;
  if (scopes.size() != 1 || scopes[0]["x"] != 1) return false;
  if (!Simple::Lang::TAST::FindLocal(scopes, "x") || *Simple::Lang::TAST::FindLocal(scopes, "x") != 1) return false;
  if (Simple::Lang::TAST::FindLocal(scopes, "missing")) return false;
  if (Simple::Lang::TAST::AddLocal(scopes, "x", 2, &error)) return false;
  if (error.find("duplicate local declaration: x") == std::string::npos) return false;

  if (!Simple::Lang::TAST::CheckMutableAssignment(Simple::Lang::Mutability::Mutable, &error)) return false;
  if (Simple::Lang::TAST::CheckMutableAssignment(Simple::Lang::Mutability::Immutable, &error)) return false;
  return error.find("cannot assign to immutable value") != std::string::npos;
}


bool LangTastCheckAssignmentValidatesShape() {
  if (!Simple::Lang::TAST::IsAssignOp("=")) return false;
  if (!Simple::Lang::TAST::IsAssignOp("<<=")) return false;
  if (Simple::Lang::TAST::IsAssignOp("==")) return false;

  Simple::Lang::AST::Stmt assign;
  assign.kind = Simple::Lang::AST::StmtKind::Assign;
  assign.target.kind = Simple::Lang::AST::ExprKind::Identifier;
  assign.target.text = "x";
  std::string error;
  if (!Simple::Lang::TAST::CheckAssignment(assign, &error)) return false;
  Simple::Lang::AST::Stmt expr_stmt;
  expr_stmt.kind = Simple::Lang::AST::StmtKind::Expr;
  return !Simple::Lang::TAST::CheckAssignment(expr_stmt, &error) &&
         error.find("expected assignment statement") != std::string::npos;
}


bool LangTastCheckCallExpressionValidatesShape() {
  Simple::Lang::AST::TypeRef proc;
  proc.is_proc = true;
  Simple::Lang::AST::TypeRef param;
  param.name = "i32";
  proc.proc_params.push_back(param);
  std::string error;
  if (!Simple::Lang::TAST::CheckProcTypeArgs(&proc, 1, &error)) return false;
  if (Simple::Lang::TAST::CheckProcTypeArgs(&proc, 2, &error)) return false;
  if (error.find("call argument count mismatch") == std::string::npos) return false;

  Simple::Lang::AST::FuncDecl fn;
  fn.name = "f";
  Simple::Lang::AST::ParamDecl fn_param;
  fn_param.name = "x";
  fn.params.push_back(fn_param);
  if (!Simple::Lang::TAST::CheckFunctionCallArgs(&fn, 1, &error)) return false;
  if (Simple::Lang::TAST::CheckFunctionCallArgs(&fn, 0, &error)) return false;
  if (error.find("call argument count mismatch for f") == std::string::npos) return false;

  Simple::Lang::AST::Expr call;
  call.kind = Simple::Lang::AST::ExprKind::Call;
  Simple::Lang::AST::Expr callee;
  callee.kind = Simple::Lang::AST::ExprKind::Identifier;
  callee.text = "f";
  call.children.push_back(callee);
  if (!Simple::Lang::TAST::CheckCallExpression(call, &error)) return false;
  Simple::Lang::AST::Expr literal;
  literal.kind = Simple::Lang::AST::ExprKind::Literal;
  return !Simple::Lang::TAST::CheckCallExpression(literal, &error) &&
         error.find("expected call expression") != std::string::npos;
}



const TestCase kLangTastTests[] = {
  {"lang_tast_type_utilities_classify_and_clone_types", LangTastTypeUtilitiesClassifyAndCloneTypes},
  {"lang_tast_primitive_cast_arg_type_rules", LangTastPrimitiveCastArgTypeRules},
  {"lang_tast_calls_check_single_arg_call_count", LangTastCallsCheckSingleArgCallCount},
  {"lang_tast_calls_check_format_and_print_arg_types", LangTastCallsCheckFormatAndPrintArgTypes},
  {"lang_tast_calls_check_scalar_arg_types", LangTastCallsCheckScalarArgTypes},
  {"lang_tast_calls_check_reserved_dl_open_arg_types", LangTastCallsCheckReservedDlOpenArgTypes},
  {"lang_tast_calls_check_reserved_file_arg_types", LangTastCallsCheckReservedFileArgTypes},
  {"lang_tast_calls_check_reserved_io_buffer_arg_types", LangTastCallsCheckReservedIoBufferArgTypes},
  {"lang_tast_calls_check_reserved_math_arg_types", LangTastCallsCheckReservedMathArgTypes},
  {"lang_tast_calls_check_reserved_time_arg_types", LangTastCallsCheckReservedTimeArgTypes},
  {"lang_tast_calls_check_type_arg_counts", LangTastCallsCheckTypeArgCounts},
  {"lang_tast_fn_literal_checks_target_procedure_shape", LangTastFnLiteralChecksTargetProcedureShape},
  {"lang_split_tast_abi_and_generics_smoke", LangSplitTastAbiAndGenericsSmoke},
  {"lang_tast_check_abi_shape_rejects_generic_types", LangTastCheckAbiShapeRejectsGenericTypes},
  {"lang_tast_substitute_generic_types_rewrites_nested_args", LangTastSubstituteGenericTypesRewritesNestedArgs},
  {"lang_tast_mutability_checks_assignments", LangTastMutabilityChecksAssignments},
  {"lang_tast_check_assignment_validates_shape", LangTastCheckAssignmentValidatesShape},
  {"lang_tast_expression_operators_validate_scalar_and_compound_assign", LangTastExpressionOperatorsValidateScalarAndCompoundAssign},
  {"lang_tast_check_expression_shape_validates_identifiers", LangTastCheckExpressionShapeValidatesIdentifiers},
  {"lang_tast_check_call_expression_validates_shape", LangTastCheckCallExpressionValidatesShape},
  {"lang_tast_checker_accepts_resolved_program", LangTastCheckerAcceptsResolvedProgram},
  {"lang_tast_checker_rejects_type_mismatch", LangTastCheckerRejectsTypeMismatch},
  {"lang_tast_control_flow_extracts_switch_branch_values", LangTastControlFlowExtractsSwitchBranchValues},
  {"lang_tast_control_flow_checks_function_returns", LangTastControlFlowChecksFunctionReturns},
  {"lang_tast_control_flow_tracks_returns_and_breaks", LangTastControlFlowTracksReturnsAndBreaks},
  {"lang_tast_check_return_flow_rejects_fallthrough", LangTastCheckReturnFlowRejectsFallthrough},
  {"lang_tast_format_string_counts_placeholders", LangTastFormatStringCountsPlaceholders},
  {"lang_tast_literal_compatibility_accepts_flexible_array_and_scalar_literals", LangTastLiteralCompatibilityAcceptsFlexibleArrayAndScalarLiterals},
  {"lang_tast_literal_helpers_classify_brace_and_list_shapes", LangTastLiteralHelpersClassifyBraceAndListShapes},
  {"lang_tast_literal_typing_uses_expected_type", LangTastLiteralTypingUsesExpectedType},
  {"lang_tast_literal_typing_rejects_invalid_expected_type", LangTastLiteralTypingRejectsInvalidExpectedType},
  {"lang_tast_literal_typing_rejects_non_literal", LangTastLiteralTypingRejectsNonLiteral},
};

const TestSection kLangTastSections[] = {
  {"lang_tast", kLangTastTests, sizeof(kLangTastTests) / sizeof(kLangTastTests[0])},
};

} // namespace

const TestSection* GetLangTastSections(size_t* count) {
  if (count) *count = sizeof(kLangTastSections) / sizeof(kLangTastSections[0]);
  return kLangTastSections;
}

} // namespace Simple::VM::Tests
