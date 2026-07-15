#include "test_utils.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
#include "GEN/specializer.h"
#include "IRB/ir_builder.h"
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
  std::string cast_error;
  auto string_type = Simple::Lang::TAST::MakeSimpleType("string");
  std::string type_error;
  if (Simple::Lang::TAST::CheckKnownTypeName(Simple::Lang::TAST::MakeSimpleType("Missing"),
                                            false,
                                            false,
                                            false,
                                            &type_error)) {
    return false;
  }
  if (type_error.find("unknown type: Missing") == std::string::npos) return false;
  auto void_type = Simple::Lang::TAST::MakeSimpleType("void");
  void_type.type_args.push_back(scalar_i32);
  if (Simple::Lang::TAST::CheckVoidTypeArgs(void_type, &type_error)) return false;
  if (type_error.find("void cannot have type arguments") == std::string::npos) return false;
  if (Simple::Lang::TAST::CheckTypeArgumentRules(generic_box, false, false, true, nullptr, &type_error)) return false;
  if (type_error.find("enum type cannot have type arguments: Box") == std::string::npos) return false;
  const size_t expected_type_args = 2;
  if (Simple::Lang::TAST::CheckTypeArgumentRules(generic_box, false, false, false, &expected_type_args, &type_error)) return false;
  if (type_error.find("generic type argument count mismatch for Box") == std::string::npos) return false;
  if (Simple::Lang::TAST::CheckTypeArgumentRules(generic_box, true, false, false, nullptr, &type_error)) return false;
  if (type_error.find("primitive type cannot have type arguments: Box") == std::string::npos) return false;
  if (Simple::Lang::TAST::CheckTypeArgumentRules(generic_box, false, true, false, nullptr, &type_error)) return false;
  if (type_error.find("type parameter cannot have type arguments: Box") == std::string::npos) return false;

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
         Simple::Lang::TAST::IsNumericScalarTypeName("i64") &&
         Simple::Lang::TAST::IsNumericScalarTypeName("f64") &&
         !Simple::Lang::TAST::IsNumericScalarTypeName("char") &&
         Simple::Lang::TAST::IsNumericTypeName("char") &&
         Simple::Lang::TAST::IsBoolTypeName("bool") &&
         Simple::Lang::TAST::IsStringTypeName("string") &&
         Simple::Lang::TAST::IsPrimitiveTypeName("i32") &&
         Simple::Lang::TAST::IsListMethodName("push") &&
         !Simple::Lang::TAST::IsListMethodName("missing") &&
         Simple::Lang::TAST::GetAtCastTargetName("@f64", &cast_target) &&
         cast_target == "f64" &&
         !Simple::Lang::TAST::CheckPrimitiveCastSyntaxName("i32", &cast_error) &&
         cast_error.find("primitive cast syntax requires '@': use @i32(value)") != std::string::npos &&
         Simple::Lang::TAST::CheckPrimitiveCastSyntaxName("make", &cast_error) &&
         Simple::Lang::TAST::IsBuiltinValueIdentifierName("len") &&
         Simple::Lang::TAST::IsBuiltinValueIdentifierName("i32") &&
         Simple::Lang::TAST::IsBuiltinValueIdentifierName("@f64") &&
         !Simple::Lang::TAST::IsBuiltinValueIdentifierName("make") &&
         Simple::Lang::TAST::IsBuiltinCallIdentifierName("len") &&
         Simple::Lang::TAST::IsBuiltinCallIdentifierName("@f64") &&
         !Simple::Lang::TAST::IsBuiltinCallIdentifierName("i32") &&
         !Simple::Lang::TAST::IsBuiltinCallIdentifierName("make") &&
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

bool LangTastCallsExtractCallCallee() {
  Simple::Lang::AST::Expr callee;
  callee.kind = Simple::Lang::AST::ExprKind::Identifier;
  callee.text = "f";
  Simple::Lang::AST::Expr call;
  call.kind = Simple::Lang::AST::ExprKind::Call;
  call.children.push_back(callee);
  const Simple::Lang::AST::Expr* extracted = nullptr;
  if (!Simple::Lang::TAST::IsCallExpr(call, &extracted) || !extracted || extracted->text != "f") return false;
  return !Simple::Lang::TAST::IsCallExpr(callee, nullptr);
}

bool LangTastCallsCheckIoPrintFormatTemplateArg() {
  std::string error;
  Simple::Lang::AST::Expr string_literal;
  string_literal.kind = Simple::Lang::AST::ExprKind::Literal;
  string_literal.literal_kind = Simple::Lang::AST::LiteralKind::String;
  if (!Simple::Lang::TAST::CheckIoPrintFormatTemplateArg(string_literal, &error)) return false;
  Simple::Lang::AST::Expr identifier;
  identifier.kind = Simple::Lang::AST::ExprKind::Identifier;
  if (Simple::Lang::TAST::CheckIoPrintFormatTemplateArg(identifier, &error)) return false;
  return error.find("Standard.IO.print format call expects string literal as first argument") != std::string::npos;
}

bool LangTastCallsCheckUniqueParamName() {
  std::string error;
  std::unordered_set<std::string> seen;
  if (!Simple::Lang::TAST::CheckUniqueParamName("x", &seen, "duplicate parameter name: ", &error)) return false;
  if (Simple::Lang::TAST::CheckUniqueParamName("x", &seen, "duplicate parameter name: ", &error)) return false;
  return error.find("duplicate parameter name: x") != std::string::npos;
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
  if (error.find("Standard.IO.print expects scalar argument") == std::string::npos) return false;
  args = {char_type};
  if (!Simple::Lang::TAST::CheckIoPrintCallArgTypes(args, &error)) return false;
  args = {artifact};
  if (Simple::Lang::TAST::CheckIoPrintCallArgTypes(args, &error)) return false;
  return error.find("Standard.IO.print supports numeric, bool, char, or string") != std::string::npos;
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
  if (error.find("System.FFI.open expects (string) or (string, manifest)") == std::string::npos) return false;
  args = {Simple::Lang::TAST::MakeSimpleType("i32")};
  if (Simple::Lang::TAST::CheckReservedDlOpenArgTypes(args, &error)) return false;
  return error.find("System.FFI.open expects first argument string path") != std::string::npos;
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
  if (error.find("System.FS.open expects (string, i32)") == std::string::npos) return false;
  args = {i32_buffer};
  if (Simple::Lang::TAST::CheckReservedFileCallArgTypes("close", args, &error)) return false;
  if (error.find("System.FS.close expects (i32)") == std::string::npos) return false;
  args = {i32, i32_buffer, i32};
  if (!Simple::Lang::TAST::CheckReservedFileCallArgTypes("read", args, &error)) return false;
  args = {i32, i32, i32};
  if (Simple::Lang::TAST::CheckReservedFileCallArgTypes("write", args, &error)) return false;
  return error.find("System.FS.write expects (i32, i32[], i32)") != std::string::npos;
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
  if (error.find("System.IO.buffer_new expects (i32)") == std::string::npos) return false;
  args = {i32_buffer, i32, i32};
  if (!Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_fill", args, &error)) return false;
  args = {i32_buffer, Simple::Lang::TAST::MakeSimpleType("string"), i32};
  if (Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_fill", args, &error)) return false;
  if (error.find("System.IO.buffer_fill expects (i32[], i32, i32)") == std::string::npos) return false;
  args = {i32_buffer, i32_buffer, i32};
  if (!Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_copy", args, &error)) return false;
  args = {i32_buffer, i32, i32};
  if (Simple::Lang::TAST::CheckReservedIoBufferCallArgTypes("buffer_copy", args, &error)) return false;
  return error.find("System.IO.buffer_copy expects (i32[], i32[], i32)") != std::string::npos;
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
  return Simple::Lang::TAST::CheckFnLiteralAgainstType(fn, target, &error);
}

bool LangGenBuildsSpecializationPlanFromProgram() {
  using Simple::Lang::AST::DeclKind;
  using Simple::Lang::GEN::BuildSpecializationPlanFromProgram;
  using Simple::Lang::GEN::GenericSpecializationPlan;

  Simple::Lang::AST::Program program;
  Simple::Lang::AST::Decl box;
  box.kind = DeclKind::Artifact;
  box.artifact.name = "Box";
  box.artifact.generics = {"T"};
  program.decls.push_back(box);

  Simple::Lang::AST::Decl value;
  value.kind = DeclKind::Variable;
  value.var.name = "boxed";
  value.var.type.name = "Box";
  value.var.type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.decls.push_back(value);

  Simple::Lang::AST::Decl builtin_generic;
  builtin_generic.kind = DeclKind::Variable;
  builtin_generic.var.name = "items";
  builtin_generic.var.type.name = "List";
  builtin_generic.var.type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.decls.push_back(builtin_generic);

  std::vector<GenericSpecializationPlan> plan;
  std::string error;
  if (!BuildSpecializationPlanFromProgram(program, &plan, &error) || !error.empty()) {
    return false;
  }
  if (plan.size() != 1 || plan[0].declaration.name != "Box" ||
      plan[0].request.argument_identities.size() != 1 ||
      plan[0].request.argument_identities[0] != "i32" || plan[0].request.argument_types.size() != 1 ||
      plan[0].bindings.size() != 1 || plan[0].bindings[0].parameter_name != "T" ||
      plan[0].bindings[0].type_identity != "i32" || !plan[0].bindings[0].has_concrete_type ||
      plan[0].bindings[0].concrete_type.name != "i32" ||
      plan[0].specialized_symbol.rfind("Box__g_", 0) != 0) {
    return false;
  }

  Simple::Lang::TAST::GenericSubstitutionMap substitutions;
  if (!Simple::Lang::GEN::BuildGenericSubstitutionMap(plan[0], &substitutions, &error) ||
      substitutions.size() != 1 || substitutions["T"].name != "i32") {
    return false;
  }

  program.decls[0].artifact.generics = {"T", "T"};
  plan.clear();
  return !BuildSpecializationPlanFromProgram(program, &plan, &error) &&
         error.find("duplicate generic parameter: T") != std::string::npos;
}

bool LangGenBuildsSpecializationPlan() {
  using Simple::Lang::GEN::BuildGenericSubstitutionMap;
  using Simple::Lang::GEN::BuildSpecializationPlan;
  using Simple::Lang::GEN::GenericInstantiationRequest;
  using Simple::Lang::GEN::GenericSpecializationPlan;
  using Simple::Lang::GEN::SpecializedSymbolName;
  using Simple::Lang::TAST::GenericDeclarationKind;
  using Simple::Lang::TAST::GenericDeclarationMetadata;

  GenericDeclarationMetadata box;
  box.kind = GenericDeclarationKind::Data;
  box.name = "Box";
  box.type_params = {"T"};
  GenericDeclarationMetadata method;
  method.kind = GenericDeclarationKind::Method;
  method.owner_name = "Box";
  method.name = "map";
  method.type_params = {"T", "U"};

  std::vector<GenericSpecializationPlan> plan;
  std::string error;
  std::vector<GenericInstantiationRequest> requests = {
      GenericInstantiationRequest{"Box", {"i32"}, 0, 0, {}},
      GenericInstantiationRequest{"Box.map", {"i32", "string"}, 0, 0, {}},
  };
  if (!BuildSpecializationPlan({box, method}, requests, &plan, &error) || !error.empty()) {
    return false;
  }
  if (plan.size() != 2 || plan[0].declaration.kind != GenericDeclarationKind::Data ||
      plan[0].specialized_symbol != SpecializedSymbolName(requests[0]) ||
      plan[0].bindings.size() != 1 || plan[0].bindings[0].parameter_name != "T" ||
      plan[0].bindings[0].type_identity != "i32" || plan[1].declaration.owner_name != "Box" ||
      plan[1].bindings.size() != 2 || plan[1].bindings[1].parameter_name != "U" ||
      plan[1].bindings[1].type_identity != "string") {
    return false;
  }

  if (plan[0].bindings[0].has_concrete_type || plan[1].bindings[0].has_concrete_type) {
    return false;
  }
  Simple::Lang::TAST::GenericSubstitutionMap substitutions;
  if (BuildGenericSubstitutionMap(plan[0], &substitutions, &error) ||
      error.find("generic specialization missing concrete type") == std::string::npos) {
    return false;
  }

  requests[0].argument_identities[0] = "T";
  plan.clear();
  if (BuildSpecializationPlan({box, method}, requests, &plan, &error) ||
      error.find("generic specialization is not concrete") == std::string::npos) {
    return false;
  }

  requests[0].argument_identities = {"i32", "extra"};
  plan.clear();
  return !BuildSpecializationPlan({box, method}, requests, &plan, &error) &&
         error.find("generic specialization argument count mismatch") != std::string::npos;
}

bool LangGenSpecializesConcreteDeclarations() {
  using Simple::Lang::AST::DeclKind;
  using Simple::Lang::GEN::BuildSpecializationPlanFromProgram;
  using Simple::Lang::GEN::GenericSpecializationPlan;
  using Simple::Lang::GEN::SpecializeArtifactLayoutDeclaration;
  using Simple::Lang::GEN::SpecializeFunctionDeclaration;

  Simple::Lang::AST::Program program;
  Simple::Lang::AST::Decl fn_decl;
  fn_decl.kind = DeclKind::Function;
  fn_decl.func.name = "id";
  fn_decl.func.generics = {"T"};
  fn_decl.func.return_type = Simple::Lang::TAST::MakeSimpleType("T");
  Simple::Lang::AST::ParamDecl param;
  param.name = "value";
  param.type = Simple::Lang::TAST::MakeSimpleType("T");
  fn_decl.func.params.push_back(param);
  Simple::Lang::AST::Stmt local;
  local.kind = Simple::Lang::AST::StmtKind::VarDecl;
  local.var_decl.name = "copy";
  local.var_decl.type = Simple::Lang::TAST::MakeSimpleType("T");
  fn_decl.func.body.push_back(local);
  program.decls.push_back(fn_decl);

  Simple::Lang::AST::Decl box_decl;
  box_decl.kind = DeclKind::Artifact;
  box_decl.artifact.name = "Box";
  box_decl.artifact.generics = {"T"};
  Simple::Lang::AST::VarDecl field;
  field.name = "value";
  field.type = Simple::Lang::TAST::MakeSimpleType("T");
  box_decl.artifact.fields.push_back(field);
  program.decls.push_back(box_decl);

  Simple::Lang::AST::Decl value;
  value.kind = DeclKind::Variable;
  value.var.name = "boxed";
  value.var.type.name = "Box";
  value.var.type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.decls.push_back(value);

  Simple::Lang::AST::Stmt call_stmt;
  call_stmt.kind = Simple::Lang::AST::StmtKind::Expr;
  call_stmt.expr.kind = Simple::Lang::AST::ExprKind::Call;
  Simple::Lang::AST::Expr callee;
  callee.kind = Simple::Lang::AST::ExprKind::Identifier;
  callee.text = "id";
  call_stmt.expr.children.push_back(callee);
  call_stmt.expr.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.top_level_stmts.push_back(call_stmt);

  std::vector<GenericSpecializationPlan> plan;
  std::string error;
  if (!BuildSpecializationPlanFromProgram(program, &plan, &error) || plan.size() != 2) return false;
  Simple::Lang::AST::FuncDecl concrete_fn;
  Simple::Lang::AST::ArtifactDecl concrete_box;
  const GenericSpecializationPlan* fn_plan = plan[0].request.base_name == "id" ? &plan[0] : &plan[1];
  const GenericSpecializationPlan* box_plan = plan[0].request.base_name == "Box" ? &plan[0] : &plan[1];
  if (!SpecializeFunctionDeclaration(program.decls[0].func, *fn_plan, &concrete_fn, &error)) return false;
  if (!SpecializeArtifactLayoutDeclaration(program.decls[1].artifact, *box_plan, &concrete_box, &error)) {
    return false;
  }
  return concrete_fn.generics.empty() && concrete_fn.name == fn_plan->specialized_symbol &&
         concrete_fn.return_type.name == "i32" && concrete_fn.params[0].type.name == "i32" &&
         concrete_fn.body[0].var_decl.type.name == "i32" && concrete_box.generics.empty() &&
         concrete_box.name == box_plan->specialized_symbol && concrete_box.fields[0].type.name == "i32";
}

bool LangGenMaterializesConcreteProgram() {
  using Simple::Lang::AST::DeclKind;
  using Simple::Lang::GEN::BuildSpecializationPlanFromProgram;
  using Simple::Lang::GEN::GenericSpecializationPlan;
  using Simple::Lang::GEN::MaterializeConcreteProgram;

  Simple::Lang::AST::Program program;
  Simple::Lang::AST::Decl ext;
  ext.kind = DeclKind::Extern;
  ext.ext.name = "native";
  ext.ext.return_type = Simple::Lang::TAST::MakeSimpleType("i32");
  program.decls.push_back(ext);

  Simple::Lang::AST::Decl fn_decl;
  fn_decl.kind = DeclKind::Function;
  fn_decl.func.name = "id";
  fn_decl.func.generics = {"T"};
  fn_decl.func.return_type = Simple::Lang::TAST::MakeSimpleType("T");
  Simple::Lang::AST::ParamDecl param;
  param.name = "value";
  param.type = Simple::Lang::TAST::MakeSimpleType("T");
  fn_decl.func.params.push_back(param);
  Simple::Lang::AST::Stmt ret;
  ret.kind = Simple::Lang::AST::StmtKind::Return;
  ret.has_return_expr = true;
  ret.expr.kind = Simple::Lang::AST::ExprKind::Identifier;
  ret.expr.text = "value";
  fn_decl.func.body.push_back(ret);
  program.decls.push_back(fn_decl);

  Simple::Lang::AST::Decl box_decl;
  box_decl.kind = DeclKind::Artifact;
  box_decl.artifact.name = "Box";
  box_decl.artifact.generics = {"T"};
  Simple::Lang::AST::VarDecl field;
  field.name = "value";
  field.type = Simple::Lang::TAST::MakeSimpleType("T");
  box_decl.artifact.fields.push_back(field);
  program.decls.push_back(box_decl);

  Simple::Lang::AST::Decl use_box;
  use_box.kind = DeclKind::Variable;
  use_box.var.name = "boxed";
  use_box.var.type.name = "Box";
  use_box.var.type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.decls.push_back(use_box);

  Simple::Lang::AST::Stmt call_stmt;
  call_stmt.kind = Simple::Lang::AST::StmtKind::Expr;
  call_stmt.expr.kind = Simple::Lang::AST::ExprKind::Call;
  Simple::Lang::AST::Expr callee;
  callee.kind = Simple::Lang::AST::ExprKind::Identifier;
  callee.text = "id";
  call_stmt.expr.children.push_back(callee);
  Simple::Lang::AST::Expr arg;
  arg.kind = Simple::Lang::AST::ExprKind::Literal;
  arg.literal_kind = Simple::Lang::AST::LiteralKind::Integer;
  arg.text = "1";
  call_stmt.expr.args.push_back(arg);
  call_stmt.expr.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.top_level_stmts.push_back(call_stmt);

  std::vector<GenericSpecializationPlan> plan;
  std::string error;
  if (!BuildSpecializationPlanFromProgram(program, &plan, &error) || plan.size() != 2) return false;
  Simple::Lang::AST::Program concrete;
  if (!MaterializeConcreteProgram(program, plan, &concrete, &error) || !error.empty()) return false;
  if (concrete.top_level_stmts.size() != 1 || concrete.decls.size() != 4) return false;
  const auto& rewritten_call = concrete.top_level_stmts[0].expr;
  if (!rewritten_call.type_args.empty() || rewritten_call.children.empty() ||
      rewritten_call.children[0].text == "id") {
    return false;
  }
  bool saw_fn = false;
  bool saw_box = false;
  bool saw_rewritten_var = false;
  std::string concrete_fn_name;
  for (const auto& decl : concrete.decls) {
    if (decl.kind == DeclKind::Function) {
      if (!decl.func.generics.empty() || decl.func.name == "id" || decl.func.return_type.name != "i32") {
        return false;
      }
      concrete_fn_name = decl.func.name;
      saw_fn = true;
    }
    if (decl.kind == DeclKind::Artifact) {
      if (!decl.artifact.generics.empty() || decl.artifact.name == "Box" ||
          decl.artifact.fields[0].type.name != "i32") {
        return false;
      }
      saw_box = true;
    }
    if (decl.kind == DeclKind::Variable && decl.var.name == "boxed") {
      if (decl.var.type.name == "Box" || !decl.var.type.type_args.empty()) return false;
      saw_rewritten_var = true;
    }
  }
  if (!saw_fn || !saw_box || !saw_rewritten_var) return false;

  Simple::Lang::RAST::ResolvedProgram resolved;
  resolved.program = &program;
  Simple::Lang::TAST::TypedProgram typed;
  typed.resolved = &resolved;
  Simple::Lang::IRB::Module module;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  if (module.sir_text.find("func id ") != std::string::npos ||
      module.sir_text.find("func id__g_") == std::string::npos) {
    return false;
  }
  bool ir_has_specialized_box = false;
  for (const auto& layout : module.ir.artifact_layouts) {
    if (layout.name.find("Box__g_") != std::string::npos) ir_has_specialized_box = true;
  }
  if (!ir_has_specialized_box) return false;

  Simple::Lang::AST::Decl collision;
  collision.kind = DeclKind::Function;
  collision.func.name = concrete_fn_name;
  collision.func.return_type = Simple::Lang::TAST::MakeSimpleType("void");
  program.decls.push_back(collision);
  concrete.decls.clear();
  return !MaterializeConcreteProgram(program, plan, &concrete, &error) &&
         error.find("duplicate top-level declaration") != std::string::npos;
}

bool LangIrbRejectsInvalidConcreteSpecialization() {
  using Simple::Lang::AST::DeclKind;

  Simple::Lang::AST::Program program;
  Simple::Lang::AST::Decl fn_decl;
  fn_decl.kind = DeclKind::Function;
  fn_decl.func.name = "bad";
  fn_decl.func.generics = {"T"};
  fn_decl.func.return_type = Simple::Lang::TAST::MakeSimpleType("i32");
  Simple::Lang::AST::ParamDecl param;
  param.name = "value";
  param.type = Simple::Lang::TAST::MakeSimpleType("T");
  fn_decl.func.params.push_back(param);
  Simple::Lang::AST::Stmt ret;
  ret.kind = Simple::Lang::AST::StmtKind::Return;
  ret.has_return_expr = true;
  ret.expr.kind = Simple::Lang::AST::ExprKind::Identifier;
  ret.expr.text = "value";
  fn_decl.func.body.push_back(ret);
  program.decls.push_back(fn_decl);

  Simple::Lang::AST::Stmt call_stmt;
  call_stmt.kind = Simple::Lang::AST::StmtKind::Expr;
  call_stmt.expr.kind = Simple::Lang::AST::ExprKind::Call;
  Simple::Lang::AST::Expr callee;
  callee.kind = Simple::Lang::AST::ExprKind::Identifier;
  callee.text = "bad";
  call_stmt.expr.children.push_back(callee);
  Simple::Lang::AST::Expr arg;
  arg.kind = Simple::Lang::AST::ExprKind::Literal;
  arg.literal_kind = Simple::Lang::AST::LiteralKind::String;
  arg.text = "x";
  call_stmt.expr.args.push_back(arg);
  call_stmt.expr.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("string"));
  program.top_level_stmts.push_back(call_stmt);

  Simple::Lang::RAST::ResolvedProgram resolved;
  resolved.program = &program;
  Simple::Lang::TAST::TypedProgram typed;
  typed.resolved = &resolved;
  Simple::Lang::IRB::Module module;
  std::string error;
  return !Simple::Lang::IRB::BuildModule(typed, &module, &error) &&
         error.find("return type mismatch") != std::string::npos;
}

bool LangGenDistinguishesProcedureTypeIdentities() {
  Simple::Lang::AST::TypeRef procedure;
  procedure.is_proc = true;
  procedure.proc_params.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  procedure.proc_return =
      std::make_unique<Simple::Lang::AST::TypeRef>(
          Simple::Lang::TAST::MakeSimpleType("bool"));
  if (Simple::Lang::GEN::TypeRefIdentity(procedure) != "fn(i32)->bool") return false;

  procedure.proc_return_mutability = Simple::Lang::Mutability::Immutable;
  if (Simple::Lang::GEN::TypeRefIdentity(procedure) != "fn::(i32)->bool") return false;

  procedure.pointer_depth = 1;
  return Simple::Lang::GEN::TypeRefIdentity(procedure) == "ptr<fn::(i32)->bool>";
}

bool LangGenNormalizesConcreteRequestMetadata() {
  using Simple::Lang::GEN::GenericInstantiationRequest;
  using Simple::Lang::GEN::NormalizeInstantiationRequests;

  GenericInstantiationRequest missing{"Box", {"i32"}, 0, 0, {}};
  GenericInstantiationRequest concrete{"Box", {"i32"}, 4, 2, {Simple::Lang::TAST::MakeSimpleType("i32")}};
  std::vector<GenericInstantiationRequest> unique;
  if (!NormalizeInstantiationRequests({missing, concrete}, &unique)) return false;
  if (unique.size() != 1 || unique[0].argument_types.size() != 1 ||
      unique[0].argument_types[0].name != "i32") {
    return false;
  }

  Simple::Lang::AST::TypeRef invalid = Simple::Lang::TAST::MakeSimpleType("i32");
  invalid.pointer_depth = 1;
  GenericInstantiationRequest conflict{"Box", {"i32"}, 0, 0, {invalid}};
  unique.clear();
  return !NormalizeInstantiationRequests({concrete, conflict}, &unique);
}

bool LangGenCollectsInstantiationRequests() {
  using Simple::Lang::AST::DeclKind;

  Simple::Lang::AST::Program program;
  Simple::Lang::AST::Decl var;
  var.kind = DeclKind::Variable;
  var.var.name = "boxes";
  var.var.type.name = "List";
  Simple::Lang::AST::TypeRef box;
  box.name = "Box";
  box.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  var.var.type.type_args.push_back(box);
  program.decls.push_back(var);

  Simple::Lang::AST::Decl ext;
  ext.kind = DeclKind::Extern;
  ext.ext.name = "native";
  ext.ext.return_type.name = "Promise";
  ext.ext.return_type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.decls.push_back(ext);

  Simple::Lang::AST::Decl fn;
  fn.kind = DeclKind::Function;
  fn.func.name = "use";
  fn.func.return_type.name = Simple::Lang::kOptionalTypeInternalName;
  fn.func.return_type.is_optional_syntax = true;
  fn.func.return_type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("string"));
  Simple::Lang::AST::ParamDecl param;
  param.name = "value";
  param.type.name = "Result";
  param.type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  param.type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("string"));
  fn.func.params.push_back(param);
  program.decls.push_back(fn);

  Simple::Lang::AST::Stmt call_stmt;
  call_stmt.kind = Simple::Lang::AST::StmtKind::Expr;
  call_stmt.expr.kind = Simple::Lang::AST::ExprKind::Call;
  Simple::Lang::AST::Expr callee;
  callee.kind = Simple::Lang::AST::ExprKind::Identifier;
  callee.text = "identity";
  call_stmt.expr.children.push_back(callee);
  call_stmt.expr.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  program.top_level_stmts.push_back(call_stmt);

  std::vector<Simple::Lang::GEN::GenericInstantiationRequest> requests;
  if (!Simple::Lang::GEN::CollectInstantiationRequestsFromProgram(program, &requests)) return false;
  if (requests.size() != 6) return false;
  std::vector<Simple::Lang::GEN::GenericInstantiationRequest> unique;
  std::vector<Simple::Lang::GEN::GenericInstantiationRequest> duplicated = requests;
  duplicated.push_back(requests[0]);
  if (!Simple::Lang::GEN::NormalizeInstantiationRequests(duplicated, &unique) ||
      unique.size() != requests.size()) {
    return false;
  }
  return requests[0].base_name == "Box" && requests[0].argument_identities.size() == 1 &&
         requests[0].argument_identities[0] == "i32" &&
         Simple::Lang::GEN::InstantiationRequestKey(requests[0]) == "Box<i32>" &&
         requests[1].base_name == "List" && requests[1].argument_identities[0] == "Box<i32>" &&
         requests[2].base_name == "Promise" && requests[2].argument_identities[0] == "i32" &&
         requests[3].base_name == Simple::Lang::kOptionalTypeInternalName &&
         requests[3].argument_identities[0] == "string" &&
         Simple::Lang::GEN::InstantiationRequestKey(requests[3]) == "string?" &&
         requests[4].base_name == "Result" && requests[4].argument_identities.size() == 2 &&
         requests[5].base_name == "identity" && requests[5].argument_identities[0] == "i32";
}

bool LangTastCollectsGenericDeclarationMetadata() {
  using Simple::Lang::AST::DeclKind;
  using Simple::Lang::TAST::GenericDeclarationKind;
  using Simple::Lang::TAST::GenericDeclarationMetadata;

  Simple::Lang::AST::Program program;
  Simple::Lang::AST::Decl function;
  function.kind = DeclKind::Function;
  function.func.name = "identity";
  function.func.generics = {"T"};
  program.decls.push_back(function);

  Simple::Lang::AST::Decl data;
  data.kind = DeclKind::Artifact;
  data.artifact.name = "Box";
  data.artifact.is_data = true;
  data.artifact.generics = {"T"};
  Simple::Lang::AST::FuncDecl method;
  method.name = "map";
  method.generics = {"U"};
  data.artifact.methods.push_back(method);
  program.decls.push_back(data);

  std::vector<GenericDeclarationMetadata> metadata;
  std::string error;
  if (!Simple::Lang::TAST::CollectGenericDeclarationMetadata(program, &metadata, &error)) {
    return false;
  }
  if (metadata.size() != 3) return false;
  if (metadata[0].kind != GenericDeclarationKind::Function || metadata[0].name != "identity" ||
      metadata[0].type_params.size() != 1 || metadata[0].type_params[0] != "T") {
    return false;
  }
  if (metadata[1].kind != GenericDeclarationKind::Data || metadata[1].name != "Box") {
    return false;
  }
  if (metadata[2].kind != GenericDeclarationKind::Method || metadata[2].owner_name != "Box" ||
      metadata[2].name != "map" || metadata[2].type_params.size() != 2 ||
      metadata[2].type_params[0] != "T" || metadata[2].type_params[1] != "U") {
    return false;
  }

  program.decls[1].artifact.methods[0].generics = {"T"};
  metadata.clear();
  return !Simple::Lang::TAST::CollectGenericDeclarationMetadata(program, &metadata, &error) &&
         error.find("duplicate generic parameter: T") != std::string::npos;
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
  return Simple::Lang::TAST::SubstituteTypeParams(box, substitutions, &out) &&
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
  if (error.find("unmatched '}'") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckFormatPlaceholderCount("{} {}", 2, "format", &error)) return false;
  if (Simple::Lang::TAST::CheckFormatPlaceholderCount("{} {}", 1, "Standard.IO.print format", &error)) return false;
  return error.find("Standard.IO.print format placeholder count mismatch: expected 2, got 1") != std::string::npos;
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
  if (Simple::Lang::TAST::TypesCompatibleForExpr(expected_scalar, actual_scalar, literal)) return false;
  std::string error;
  if (Simple::Lang::TAST::CheckTypesCompatibleForExpr(expected_scalar, actual_scalar, literal,
                                                       "type mismatch", &error)) return false;
  if (error != "type mismatch") return false;
  expected_scalar.name = "i64";
  return Simple::Lang::TAST::CheckTypesCompatibleForExpr(expected_scalar, actual_scalar, literal,
                                                         "type mismatch", &error);
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
  if (Simple::Lang::TAST::IsPositionalBraceLiteralExpr(named_artifact)) return false;
  Simple::Lang::AST::Expr artifact_values;
  artifact_values.kind = Simple::Lang::AST::ExprKind::ArtifactLiteral;
  artifact_values.children.resize(2);
  if (!Simple::Lang::TAST::CheckArtifactLiteralPositionalCount(artifact_values, 2, &shape_error)) return false;
  if (Simple::Lang::TAST::CheckArtifactLiteralPositionalCount(artifact_values, 1, &shape_error)) return false;
  if (shape_error.find("too many positional values in artifact literal") == std::string::npos) return false;
  Simple::Lang::AST::Expr duplicate_fields;
  duplicate_fields.kind = Simple::Lang::AST::ExprKind::ArtifactLiteral;
  duplicate_fields.field_names = {"x", "x"};
  if (Simple::Lang::TAST::CheckArtifactLiteralDuplicateNamedFields(duplicate_fields, &shape_error)) return false;
  if (shape_error.find("duplicate named field in artifact literal: x") == std::string::npos) return false;
  duplicate_fields.field_names = {"x", "y"};
  if (!Simple::Lang::TAST::CheckArtifactLiteralDuplicateNamedFields(duplicate_fields, &shape_error)) return false;
  std::unordered_set<std::string> seen_fields = {"x"};
  if (Simple::Lang::TAST::CheckArtifactLiteralFieldSpecifiedOnce("x", seen_fields, &shape_error)) return false;
  if (shape_error.find("field specified twice in artifact literal: x") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckArtifactLiteralFieldSpecifiedOnce("y", seen_fields, &shape_error)) return false;
  if (!Simple::Lang::TAST::CheckArtifactLiteralKnownField("x", seen_fields, &shape_error)) return false;
  if (Simple::Lang::TAST::CheckArtifactLiteralKnownField("z", seen_fields, &shape_error)) return false;
  if (shape_error.find("unknown artifact field: z") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckArtifactLiteralRequiredField("x", false, seen_fields, &shape_error)) return false;
  if (!Simple::Lang::TAST::CheckArtifactLiteralRequiredField("z", true, seen_fields, &shape_error)) return false;
  if (Simple::Lang::TAST::CheckArtifactLiteralRequiredField("z", false, seen_fields, &shape_error)) return false;
  if (shape_error.find("missing artifact field: z") == std::string::npos) return false;
  Simple::Lang::AST::TypeRef scalar_target;
  scalar_target.name = "i32";
  if (Simple::Lang::TAST::CheckArrayListLiteralTargetShape(scalar_target, list, &shape_error)) return false;
  if (shape_error.find("array/list literal requires array or list type") == std::string::npos) return false;
  Simple::Lang::AST::TypeRef list_target;
  list_target.name = "i32";
  list_target.dims.push_back(Simple::Lang::AST::TypeDim{true, false, 0});
  if (Simple::Lang::TAST::CheckArrayListLiteralTargetShape(list_target, array, &shape_error)) return false;
  Simple::Lang::AST::TypeRef array_target;
  array_target.name = "i32";
  array_target.dims.push_back(Simple::Lang::AST::TypeDim{false, false, 0});
  return Simple::Lang::TAST::CheckArrayListLiteralTargetShape(array_target, array, &shape_error);
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

  Simple::Lang::AST::Expr switch_expr;
  switch_expr.kind = Simple::Lang::AST::ExprKind::Switch;
  if (Simple::Lang::TAST::CheckSwitchExprShape(switch_expr, &error)) return false;
  if (error.find("invalid switch expression") == std::string::npos) return false;
  switch_expr.children.push_back(Simple::Lang::AST::Expr{});
  if (Simple::Lang::TAST::CheckSwitchExprShape(switch_expr, &error)) return false;
  if (error.find("switch requires at least one branch") == std::string::npos) return false;
  switch_expr.switch_branches.push_back(Simple::Lang::AST::SwitchBranch{});
  if (!Simple::Lang::TAST::CheckSwitchExprShape(switch_expr, &error)) return false;

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
  if (!Simple::Lang::TAST::CheckFunctionReturnFlow(fn, &error)) return false;
  Simple::Lang::AST::Stmt ret;
  ret.kind = Simple::Lang::AST::StmtKind::Return;
  ret.has_return_expr = true;
  if (Simple::Lang::TAST::CheckReturnStmtValuePresence(ret, true, &error)) return false;
  if (error.find("void function cannot return a value") == std::string::npos) return false;
  ret.has_return_expr = false;
  if (Simple::Lang::TAST::CheckReturnStmtValuePresence(ret, false, &error)) return false;
  if (error.find("non-void function must return a value") == std::string::npos) return false;
  if (Simple::Lang::TAST::CheckTopLevelStmtAllowsReturn(ret, &error)) return false;
  if (error.find("top-level return is not allowed") == std::string::npos) return false;
  Simple::Lang::AST::Stmt expr_stmt;
  expr_stmt.kind = Simple::Lang::AST::StmtKind::Expr;
  return Simple::Lang::TAST::CheckTopLevelStmtAllowsReturn(expr_stmt, &error);
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
  Simple::Lang::AST::Expr member_expr;
  member_expr.kind = Simple::Lang::AST::ExprKind::Member;
  member_expr.op = "->";
  member_expr.children.push_back(identifier);
  const Simple::Lang::AST::Expr* member_base = nullptr;
  bool pointer_access = false;
  if (!Simple::Lang::TAST::IsMemberAccessExpr(member_expr, &member_base, &pointer_access)) return false;
  if (!member_base || member_base->kind != Simple::Lang::AST::ExprKind::Identifier || !pointer_access) return false;
  member_expr.op = "+";
  if (Simple::Lang::TAST::IsMemberAccessExpr(member_expr, nullptr, nullptr)) return false;
  Simple::Lang::AST::Expr unary_expr;
  unary_expr.kind = Simple::Lang::AST::ExprKind::Unary;
  unary_expr.children.push_back(identifier);
  const Simple::Lang::AST::Expr* unary_operand = nullptr;
  if (!Simple::Lang::TAST::IsUnaryExpr(unary_expr, &unary_operand) || unary_operand != &unary_expr.children[0]) return false;
  Simple::Lang::AST::Expr binary_expr;
  binary_expr.kind = Simple::Lang::AST::ExprKind::Binary;
  binary_expr.children.push_back(identifier);
  binary_expr.children.push_back(identifier);
  const Simple::Lang::AST::Expr* lhs_expr = nullptr;
  const Simple::Lang::AST::Expr* rhs_expr = nullptr;
  if (!Simple::Lang::TAST::IsBinaryExpr(binary_expr, &lhs_expr, &rhs_expr)) return false;
  if (lhs_expr != &binary_expr.children[0] || rhs_expr != &binary_expr.children[1]) return false;
  if (Simple::Lang::TAST::IsBinaryExpr(unary_expr, nullptr, nullptr)) return false;

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
  if (!Simple::Lang::TAST::CheckCompoundAssignOp("==", string_type, string_type, &error)) return false;
  if (!Simple::Lang::TAST::CheckUnaryOpTypeRules("!", bool_type, identifier, &error)) return false;
  if (Simple::Lang::TAST::CheckUnaryOpTypeRules("&", i32, call_expr, &error)) return false;
  if (error.find("address-of requires assignable expression") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckBinaryOpTypeRules("+", i32, i32, identifier, identifier, &error)) return false;
  if (!Simple::Lang::TAST::CheckBinaryOpTypeRules("==", string_type, string_type, identifier, identifier, &error)) return false;
  if (!Simple::Lang::TAST::CheckBinaryOpTypeRules("!=", string_type, string_type, identifier, identifier, &error)) return false;
  if (Simple::Lang::TAST::CheckBinaryOpTypeRules("<", string_type, string_type, identifier, identifier, &error)) return false;
  return error.find("requires numeric operands") != std::string::npos;
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
  point.is_data = true;
  Simple::Lang::AST::VarDecl x;
  x.name = "x";
  x.type = Simple::Lang::TAST::MakeSimpleType("i32");
  point.fields.push_back(x);
  artifacts[point.name] = &point;
  Simple::Lang::AST::TypeRef point_type = Simple::Lang::TAST::MakeSimpleType("Point");
  if (!Simple::Lang::TAST::IsSupportedDlAbiType(point_type, enum_types, artifacts, false)) return false;
  Simple::Lang::AST::TypeRef enum_type = Simple::Lang::TAST::MakeSimpleType("Mode");
  if (Simple::Lang::TAST::IsSupportedDlAbiType(enum_type, enum_types, artifacts, false)) return false;
  Simple::Lang::AST::ExternDecl ext;
  ext.module = "ffi";
  ext.name = "add";
  ext.return_type = Simple::Lang::TAST::MakeSimpleType("i32");
  Simple::Lang::AST::ParamDecl param;
  param.name = "x";
  param.type = point_type;
  ext.params.push_back(param);
  if (!Simple::Lang::TAST::CheckDlDynamicSignature(ext, enum_types, artifacts, &error)) return false;
  if (!Simple::Lang::TAST::CheckExternAbiType(point_type,
                                             enum_types,
                                             artifacts,
                                             false,
                                             "extern ABI parameter type is not supported",
                                             &error)) {
    return false;
  }
  point.fields[0].type.type_args.push_back(Simple::Lang::TAST::MakeSimpleType("i32"));
  if (Simple::Lang::TAST::IsSupportedDlAbiType(point_type, enum_types, artifacts, false)) return false;
  if (Simple::Lang::TAST::CheckExternAbiType(point_type,
                                             enum_types,
                                             artifacts,
                                             false,
                                             "extern ABI parameter type is not supported",
                                             &error)) {
    return false;
  }
  if (error.find("extern ABI parameter type is not supported") == std::string::npos) return false;
  if (Simple::Lang::TAST::CheckDlDynamicSignature(ext, enum_types, artifacts, &error)) return false;
  return error.find("dynamic DL parameter type for 'ffi.add' is not ABI-supported") != std::string::npos;
}


bool LangTastSubstituteTypeParamsRewritesNestedTypes() {
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
  if (!Simple::Lang::TAST::SubstituteTypeParams(input, substitutions, &output)) return false;
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
  array_arg.dims.push_back({false, true, 3});
  Simple::Lang::TAST::GenericSubstitutionMap inferred;
  std::unordered_set<std::string> type_params = {"T"};
  if (!Simple::Lang::TAST::UnifyTypeParams(generic_param, array_arg, type_params, &inferred)) return false;
  if (inferred.size() != 1 || inferred["T"].name != "string" ||
      inferred["T"].dims.size() != 1 || inferred["T"].dims[0].size != 3) {
    return false;
  }

  Simple::Lang::AST::TypeRef substituted_array;
  if (!Simple::Lang::TAST::SubstituteTypeParams(generic_param, inferred,
                                                &substituted_array)) {
    return false;
  }
  if (substituted_array.dims.size() != 2 || substituted_array.dims[0].size != 2 ||
      substituted_array.dims[1].size != 3) {
    return false;
  }

  Simple::Lang::AST::TypeRef pointer_param;
  pointer_param.name = "T";
  pointer_param.pointer_depth = 1;
  Simple::Lang::AST::TypeRef pointer_arg = Simple::Lang::TAST::MakeSimpleType("i32");
  pointer_arg.pointer_depth = 2;
  inferred.clear();
  if (!Simple::Lang::TAST::UnifyTypeParams(
          pointer_param, pointer_arg, type_params, &inferred)) {
    return false;
  }
  return inferred["T"].name == "i32" && inferred["T"].pointer_depth == 1;
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

  Simple::Lang::AST::Expr target;
  target.kind = Simple::Lang::AST::ExprKind::Identifier;
  target.text = "x";
  Simple::Lang::AST::Expr address_of;
  address_of.kind = Simple::Lang::AST::ExprKind::Unary;
  address_of.op = "&";
  address_of.children.push_back(target);
  const Simple::Lang::AST::Expr* extracted = nullptr;
  if (!Simple::Lang::TAST::IsAddressOfExpr(address_of, &extracted) || !extracted || extracted->text != "x") return false;
  if (Simple::Lang::TAST::IsAddressOfExpr(target, nullptr)) return false;
  Simple::Lang::AST::Expr index_expr;
  index_expr.kind = Simple::Lang::AST::ExprKind::Index;
  index_expr.children.push_back(target);
  const Simple::Lang::AST::Expr* index_base = nullptr;
  if (!Simple::Lang::TAST::IsIndexExpr(index_expr, &index_base) || !index_base || index_base->text != "x") return false;
  if (Simple::Lang::TAST::IsIndexExpr(target, nullptr)) return false;

  Simple::Lang::AST::Expr self_target;
  self_target.kind = Simple::Lang::AST::ExprKind::Identifier;
  self_target.text = "self";
  if (Simple::Lang::TAST::CheckAssignTargetSelfName(self_target, &error)) return false;
  if (error.find("cannot assign to self") == std::string::npos) return false;
  if (!Simple::Lang::TAST::CheckAssignTargetSelfName(target, &error)) return false;

  if (!Simple::Lang::TAST::CheckMutableAssignment(Simple::Lang::Mutability::Mutable, &error)) return false;
  if (Simple::Lang::TAST::CheckMutableAssignment(Simple::Lang::Mutability::Immutable, &error)) return false;
  return error.find("cannot assign to immutable value") != std::string::npos;
}


bool LangTastCheckAssignmentValidatesShape() {
  std::string program_error;
  Simple::Lang::AST::Program empty_program;
  if (Simple::Lang::TAST::CheckProgramHasDeclarationsOrTopLevelStatements(empty_program, &program_error)) return false;
  if (program_error.find("program has no declarations or top-level statements") == std::string::npos) return false;
  Simple::Lang::AST::Stmt top_stmt;
  top_stmt.kind = Simple::Lang::AST::StmtKind::Expr;
  empty_program.top_level_stmts.push_back(top_stmt);
  if (!Simple::Lang::TAST::CheckProgramHasDeclarationsOrTopLevelStatements(empty_program, &program_error)) return false;

  if (!Simple::Lang::TAST::IsAssignOp("=")) return false;
  if (!Simple::Lang::TAST::IsAssignOp("<<=")) return false;
  if (Simple::Lang::TAST::IsAssignOp("==")) return false;
  std::string member_error;
  Simple::Lang::AST::EnumMember enum_member;
  enum_member.name = "A";
  if (Simple::Lang::TAST::CheckEnumMemberValue(enum_member, &member_error)) return false;
  if (member_error.find("enum member requires explicit value: A") == std::string::npos) return false;
  enum_member.has_value = true;
  if (!Simple::Lang::TAST::CheckEnumMemberValue(enum_member, &member_error)) return false;
  std::unordered_set<std::string> seen_members;
  if (!Simple::Lang::TAST::CheckUniqueNamedMember("A", &seen_members, "duplicate enum member: ", &member_error)) return false;
  if (Simple::Lang::TAST::CheckUniqueNamedMember("A", &seen_members, "duplicate enum member: ", &member_error)) return false;
  if (member_error.find("duplicate enum member: A") == std::string::npos) return false;
  std::unordered_set<std::string> seen_decls;
  if (!Simple::Lang::TAST::CheckUniqueNamedMember("Thing", &seen_decls, "duplicate top-level declaration: ", &member_error)) return false;
  if (Simple::Lang::TAST::CheckUniqueNamedMember("Thing", &seen_decls, "duplicate top-level declaration: ", &member_error)) return false;
  if (member_error.find("duplicate top-level declaration: Thing") == std::string::npos) return false;

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


bool LangTastInfersGenericMethodOnIndexedReceiver() {
  const std::string source =
      "Box<T> :: artifact { item : T; choose<U> :: U (value : U) { return value } }\n"
      "main :: i32 () { boxes : Box<i32>[] = []; box : Box<i32> = { 1 }; "
      "boxes.push(box); return boxes[0].choose(42) }";
  Simple::Lang::AST::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(source, &program, &error) ||
      !Simple::Lang::TAST::AnnotateInferredGenericCallTypeArguments(&program, &error)) {
    return false;
  }
  if (program.decls.size() != 2 || program.decls[1].func.body.empty()) return false;
  const auto& call = program.decls[1].func.body.back().expr;
  return call.kind == Simple::Lang::AST::ExprKind::Call && call.type_args.size() == 1 &&
         call.type_args[0].name == "i32";
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


bool LangTastRejectsLegacyOptionalGenericName() {
  const char* src = "main : i32 () { value : Option<i32>; return 0 }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("unknown type: Option") != std::string::npos;
}

bool LangTastRejectsTaggedWholeValueEquality() {
  const char* src =
      "main :: i32 () { a : i32?; b : i32?; if (a == b) { return 1 } return 0 }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("requires scalar operands") != std::string::npos;
}

bool LangTastRejectsDirectTaggedPayloadAccess() {
  for (const char* src : {
           "main : i32 () { value : i32? = { 1 }; return value.value }",
           "Error :: enum { Bad = 1 } main : i32 () { "
           "result : Result<i32, Error> = { .value = 1 }; return result.value }",
       }) {
    std::string error;
    if (Simple::Lang::ValidateProgramFromString(src, &error) ||
        error.find("tagged payload access requires exhaustive pattern binding or '?'") ==
            std::string::npos) {
      return false;
    }
  }
  return true;
}

bool LangTastRejectsOptionalImplicitLift() {
  const char* src = "main : i32 () { value : i32? = 42; return 0 }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("initializer type mismatch") != std::string::npos;
}

bool LangTastRejectsMalformedOptionalLiteral() {
  const char* src = "main : i32 () { value : i32? = { 1, 2 }; return 0 }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("optional literal must be '{}' or '{ value }'") != std::string::npos;
}

bool LangTastRejectsUncontextualBraceLiteral() {
  const char* src = "main : void () { {}; }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("contextual literal requires a typed value context") != std::string::npos;
}

bool LangTastRejectsInexhaustiveOptionalPattern() {
  const char* src =
      "main : i32 () { value : i32?; return switch (value) { "
      "{ present } => return present } }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("each state exactly once") != std::string::npos;
}

bool LangTastRejectsInvalidResultLiteral() {
  for (const auto& test : {
           std::pair<const char*, const char*>{
               "Error :: enum { Bad = 1 } main : Result<i32, Error> () { "
               "return { .other = 1 } }",
               "'.value' or '.error'"},
           std::pair<const char*, const char*>{
               "Error :: enum { Bad = 1 } main : i32 () { "
               "result : Result<i32, Error> = { .value = \"bad\" }; return 0 }",
               "expression type mismatch"},
           std::pair<const char*, const char*>{
               "Error :: enum { Bad = 1 } main : i32 () { "
               "result : Result<i32, Error>; return switch (result) { "
               "{ .value = value } => return value } }",
               "each state exactly once"},
       }) {
    std::string error;
    if (Simple::Lang::ValidateProgramFromString(test.first, &error) ||
        error.find(test.second) == std::string::npos) {
      return false;
    }
  }
  return true;
}

bool LangTastRejectsDiscardedResult() {
  const char* src =
      "Error :: enum { Bad = 1 } make : Result<i32, Error> () { "
      "return { .value = 1 } } main : i32 () { make(); return 0 }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("Result value must be returned, stored, propagated") != std::string::npos;
}

bool LangTastRejectsIncompatibleOptionalPropagation() {
  for (const auto& test : {
           std::pair<const char*, const char*>{
               "main : i32 () { value : i32?; return value? }",
               "optional propagation requires an optional return type"},
           std::pair<const char*, const char*>{
               "main : string? () { value : i32?; number : i32 = value?; return {} }",
               "optional propagation requires the same payload type"},
           std::pair<const char*, const char*>{
               "main : i32? () { value : i32 = 1; number : i32 = value?; return {} }",
               "operator '?' requires optional or Result operand"},
       }) {
    std::string error;
    if (Simple::Lang::ValidateProgramFromString(test.first, &error) ||
        error.find(test.second) == std::string::npos) {
      return false;
    }
  }
  return true;
}

bool LangTastRejectsMismatchedResultPropagation() {
  const char* src =
      "First :: enum { Bad = 1 } Second :: enum { Bad = 1 } "
      "source : Result<i32, First> () { return { .error = First.Bad } } "
      "target : Result<i32, Second> () { value : i32 = source()?; "
      "return { .value = value } }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error) &&
         error.find("same error type") != std::string::npos;
}

const TestCase kLangTastTests[] = {
  {"lang_tast_type_utilities_classify_and_clone_types", LangTastTypeUtilitiesClassifyAndCloneTypes},
  {"lang_tast_primitive_cast_arg_type_rules", LangTastPrimitiveCastArgTypeRules},
  {"lang_tast_calls_extract_call_callee", LangTastCallsExtractCallCallee},
  {"lang_tast_calls_check_io_print_format_template_arg", LangTastCallsCheckIoPrintFormatTemplateArg},
  {"lang_tast_calls_check_unique_param_name", LangTastCallsCheckUniqueParamName},
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
  {"lang_gen_builds_specialization_plan_from_program", LangGenBuildsSpecializationPlanFromProgram},
  {"lang_gen_builds_specialization_plan", LangGenBuildsSpecializationPlan},
  {"lang_gen_specializes_concrete_declarations", LangGenSpecializesConcreteDeclarations},
  {"lang_gen_materializes_concrete_program", LangGenMaterializesConcreteProgram},
  {"lang_irb_rejects_invalid_concrete_specialization", LangIrbRejectsInvalidConcreteSpecialization},
  {"lang_gen_distinguishes_procedure_type_identities",
   LangGenDistinguishesProcedureTypeIdentities},
  {"lang_gen_normalizes_concrete_request_metadata", LangGenNormalizesConcreteRequestMetadata},
  {"lang_gen_collects_instantiation_requests", LangGenCollectsInstantiationRequests},
  {"lang_tast_collects_generic_declaration_metadata", LangTastCollectsGenericDeclarationMetadata},
  {"lang_split_tast_abi_and_generics_smoke", LangSplitTastAbiAndGenericsSmoke},
  {"lang_tast_check_abi_shape_rejects_generic_types", LangTastCheckAbiShapeRejectsGenericTypes},
  {"lang_tast_substitute_type_params_rewrites_nested_types",
   LangTastSubstituteTypeParamsRewritesNestedTypes},
  {"lang_tast_mutability_checks_assignments", LangTastMutabilityChecksAssignments},
  {"lang_tast_check_assignment_validates_shape", LangTastCheckAssignmentValidatesShape},
  {"lang_tast_expression_operators_validate_scalar_and_compound_assign", LangTastExpressionOperatorsValidateScalarAndCompoundAssign},
  {"lang_tast_check_expression_shape_validates_identifiers", LangTastCheckExpressionShapeValidatesIdentifiers},
  {"lang_tast_check_call_expression_validates_shape", LangTastCheckCallExpressionValidatesShape},
  {"lang_tast_infers_generic_method_on_indexed_receiver",
   LangTastInfersGenericMethodOnIndexedReceiver},
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
  {"lang_tast_rejects_legacy_optional_generic_name",
   LangTastRejectsLegacyOptionalGenericName},
  {"lang_tast_rejects_tagged_whole_value_equality",
   LangTastRejectsTaggedWholeValueEquality},
  {"lang_tast_rejects_direct_tagged_payload_access",
   LangTastRejectsDirectTaggedPayloadAccess},
  {"lang_tast_rejects_optional_implicit_lift", LangTastRejectsOptionalImplicitLift},
  {"lang_tast_rejects_malformed_optional_literal", LangTastRejectsMalformedOptionalLiteral},
  {"lang_tast_rejects_uncontextual_brace_literal", LangTastRejectsUncontextualBraceLiteral},
  {"lang_tast_rejects_inexhaustive_optional_pattern",
   LangTastRejectsInexhaustiveOptionalPattern},
  {"lang_tast_rejects_invalid_result_literal", LangTastRejectsInvalidResultLiteral},
  {"lang_tast_rejects_discarded_result", LangTastRejectsDiscardedResult},
  {"lang_tast_rejects_incompatible_optional_propagation",
   LangTastRejectsIncompatibleOptionalPropagation},
  {"lang_tast_rejects_mismatched_result_propagation",
   LangTastRejectsMismatchedResultPropagation},
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
