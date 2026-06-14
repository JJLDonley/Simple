#include "test_utils.h"

#include <memory>
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

  std::string cast_target;
  return Simple::Lang::TAST::IsIntegerScalarTypeName("u64") &&
         Simple::Lang::TAST::IsFloatTypeName("f32") &&
         Simple::Lang::TAST::IsNumericTypeName("char") &&
         Simple::Lang::TAST::IsBoolTypeName("bool") &&
         Simple::Lang::TAST::IsStringTypeName("string") &&
         Simple::Lang::TAST::IsPrimitiveTypeName("i32") &&
         Simple::Lang::TAST::GetAtCastTargetName("@f64", &cast_target) &&
         cast_target == "f64" &&
         !Simple::Lang::TAST::IsPrimitiveTypeName("Box");
}

bool LangSplitTastAbiAndGenericsSmoke() {
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
  return actual.name == "u64";
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
  return error.find("extern ABI type shape is not supported") != std::string::npos;
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
  return map.size() == 1 && map["T"].name == "i32";
}


bool LangTastMutabilityChecksAssignments() {
  std::string error;
  if (!Simple::Lang::TAST::CheckMutableAssignment(Simple::Lang::Mutability::Mutable, &error)) return false;
  if (Simple::Lang::TAST::CheckMutableAssignment(Simple::Lang::Mutability::Immutable, &error)) return false;
  return error.find("cannot assign to immutable value") != std::string::npos;
}


bool LangTastCheckAssignmentValidatesShape() {
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
  {"lang_split_tast_abi_and_generics_smoke", LangSplitTastAbiAndGenericsSmoke},
  {"lang_tast_check_abi_shape_rejects_generic_types", LangTastCheckAbiShapeRejectsGenericTypes},
  {"lang_tast_substitute_generic_types_rewrites_nested_args", LangTastSubstituteGenericTypesRewritesNestedArgs},
  {"lang_tast_mutability_checks_assignments", LangTastMutabilityChecksAssignments},
  {"lang_tast_check_assignment_validates_shape", LangTastCheckAssignmentValidatesShape},
  {"lang_tast_check_expression_shape_validates_identifiers", LangTastCheckExpressionShapeValidatesIdentifiers},
  {"lang_tast_check_call_expression_validates_shape", LangTastCheckCallExpressionValidatesShape},
  {"lang_tast_checker_accepts_resolved_program", LangTastCheckerAcceptsResolvedProgram},
  {"lang_tast_checker_rejects_type_mismatch", LangTastCheckerRejectsTypeMismatch},
  {"lang_tast_control_flow_tracks_returns_and_breaks", LangTastControlFlowTracksReturnsAndBreaks},
  {"lang_tast_check_return_flow_rejects_fallthrough", LangTastCheckReturnFlowRejectsFallthrough},
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
