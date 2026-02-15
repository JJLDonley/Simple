#include "lang_lexer.h"
#include "lang_parser.h"
#include "lang_sir.h"
#include "lang_validate.h"
#include "ir_lang.h"
#include "ir_compiler.h"
#include "simple_runner.h"
#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <iostream>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace Simple::VM::Tests {
namespace {

bool RunCommand(const std::string& command) {
  const int result = std::system(command.c_str());
  return result == 0;
}

int SystemExitCode(int result) {
#ifdef _WIN32
  return result;
#else
  if (WIFEXITED(result)) return WEXITSTATUS(result);
  if (WIFSIGNALED(result)) return 128 + WTERMSIG(result);
  return result;
#endif
}

std::string TempPath(const std::string& name);

std::string ReadFileText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::string text;
  in.seekg(0, std::ios::end);
  text.resize(static_cast<size_t>(in.tellg()));
  in.seekg(0, std::ios::beg);
  in.read(text.data(), static_cast<std::streamsize>(text.size()));
  return text;
}

std::string RunCommandCaptureStdout(const std::string& command) {
  const std::string out_path = TempPath("simple_command_capture.txt");
  const std::string wrapped = command + " > " + out_path + " 2>/dev/null";
  if (std::system(wrapped.c_str()) != 0) return {};
  return ReadFileText(out_path);
}

std::string RunCommandCaptureStderr(const std::string& command, int* out_exit_code = nullptr) {
  const std::string err_path = TempPath("simple_command_stderr_capture.txt");
  const std::string wrapped = command + " 1>/dev/null 2> " + err_path;
  const int result = std::system(wrapped.c_str());
  if (out_exit_code) *out_exit_code = SystemExitCode(result);
  return ReadFileText(err_path);
}

bool RunCommandExpectFail(const std::string& command) {
  const std::string err_path = TempPath("simple_expect_fail_err.txt");
  const std::string wrapped = command + " 1>/dev/null 2> " + err_path;
  const int result = std::system(wrapped.c_str());
  if (result == 0) {
    std::cerr << "expected failure: command succeeded\n";
    return false;
  }
  std::ifstream in(err_path);
  std::string line;
  if (in.good() && std::getline(in, line)) {
    std::cerr << "expected failure: " << line << "\n";
  } else {
    std::cerr << "expected failure: (no error output)\n";
  }
  return true;
}

std::string TempPath(const std::string& name) {
  namespace fs = std::filesystem;
  return (fs::temp_directory_path() / name).string();
}

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

bool LangSirTopLevelScriptExecutes() {
  const char* src =
      "add : i32 (a : i32, b : i32) { return a + b; }\n"
      "x : i32 = add(40, 2);\n"
      "x = x + 1;\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  if (sir.find("entry __script_entry") == std::string::npos) return false;
  return RunSirTextExpectExit(sir, 0);
}

bool LangTopLevelReturnDisallowed() {
  const char* src = "return 1;";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("top-level return is not allowed") != std::string::npos;
}

bool LangTopLevelIoPrintlnArithmetic() {
  const char* src =
      "import \"IO\"\n"
      "IO.println(\"Hello World\");\n"
      "IO.println(10 + 20 + 60 / 3);\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 0);
}

bool LangSimpleFixtureHello() {
  return RunSimpleFileExpectExit("Tests/simple/hello.simple", 0);
}

bool LangSimpleFixtureMath() {
  return RunSimpleFileExpectExit("Tests/simple/math.simple", 0);
}

bool LangSimpleFixtureSumLoop() {
  return RunSimpleFileExpectExit("Tests/simple/sum_loop.simple", 4950);
}

bool LangSimpleFixtureSumArray() {
  return RunSimpleFileExpectExit("Tests/simple/sum_array.simple", 6);
}

bool LangSimpleFixturePointSum() {
  return RunSimpleFileExpectExit("Tests/simple/point_sum.simple", 7);
}

bool LangSimpleFixtureListLen() {
  return RunSimpleFileExpectExit("Tests/simple/list_len.simple", 4);
}

bool LangSimpleFixtureListNested() {
  return RunSimpleFileExpectExit("Tests/simple/list_nested.simple", 3);
}

bool LangSimpleFixtureListMethods() {
  return RunSimpleFileExpectExit("Tests/simple/list_methods.simple", 31);
}

bool LangSimpleFixtureArrayEmpty() {
  return RunSimpleFileExpectExit("Tests/simple/array_empty.simple", 0);
}

bool LangSimpleFixtureListEmpty() {
  return RunSimpleFileExpectExit("Tests/simple/list_empty.simple", 0);
}

bool LangSimpleFixtureAddFn() {
  return RunSimpleFileExpectExit("Tests/simple/add_fn.simple", 42);
}

bool LangSimpleFixtureIfElse() {
  return RunSimpleFileExpectExit("Tests/simple/if_else.simple", 7);
}

bool LangSimpleFixtureForLoop() {
  return RunSimpleFileExpectExit("Tests/simple/for_loop.simple", 15);
}

bool LangSimpleFixtureForRangeLoop() {
  return RunSimpleFileExpectExit("Tests/simple/for_range.simple", 55);
}

bool LangSimpleFixtureForRangeHeaderInit() {
  return RunSimpleFileExpectExit("Tests/simple/for_range_header_init.simple", 3);
}

bool LangSimpleFixtureWhileBreak() {
  return RunSimpleFileExpectExit("Tests/simple/while_break.simple", 6);
}

bool LangSimpleFixtureEnumValue() {
  return RunSimpleFileExpectExit("Tests/simple/enum_value.simple", 1);
}

bool LangSimpleFixtureEnumExplicit() {
  return RunSimpleFileExpectExit("Tests/simple/enum_explicit.simple", 9);
}

bool LangSimpleFixtureModuleAccess() {
  return RunSimpleFileExpectExit("Tests/simple/module_access.simple", 5);
}

bool LangSimpleFixtureIoPrint() {
  return RunSimpleFileExpectExit("Tests/simple/io_print.simple", 0);
}

bool LangSimpleFixtureFnLiteral() {
  return RunSimpleFileExpectExit("Tests/simple/fn_literal.simple", 42);
}

bool LangSimpleFixtureArrayAssign() {
  return RunSimpleFileExpectExit("Tests/simple/array_assign.simple", 9);
}

bool LangSimpleFixtureListIndex() {
  return RunSimpleFileExpectExit("Tests/simple/list_index.simple", 6);
}

bool LangSimpleFixtureStringLen() {
  return RunSimpleFileExpectExit("Tests/simple/string_len.simple", 5);
}

bool LangSimpleFixtureArtifactMethod() {
  return RunSimpleFileExpectExit("Tests/simple/artifact_method.simple", 7);
}

bool LangSimpleFixtureModuleMulti() {
  return RunSimpleFileExpectExit("Tests/simple/module_multi.simple", 6);
}

bool LangSimpleFixtureModuleFuncParams() {
  return RunSimpleFileExpectExit("Tests/simple/module_func_params.simple", 42);
}

bool LangSimpleFixtureImportBasic() {
  return RunSimpleFileExpectExit("Tests/simple/import_basic.simple", 42);
}

bool LangSimpleFixtureExternDecl() {
  return RunSimpleFileExpectExit("Tests/simple/extern_decl.simple", 0);
}

bool LangSimpleFixtureExternCoreOsArgsCount() {
  return RunSimpleFileExpectExit("Tests/simple/extern_core_os_args_count.simple", 0);
}

bool LangSimpleFixtureCoreDlOpen() {
  return RunSimpleFileExpectExit("Tests/simple/core_dl_open.simple", 1);
}

bool LangSimpleFixtureCoreDlOpenGlobal() {
  return RunSimpleFileExpectExit("Tests/simple/core_dl_open_global.simple", 1);
}

bool LangSimpleFixtureFloatLiteralContext() {
  return RunSimpleFileExpectExit("Tests/simple/float_literal_context.simple", 0);
}

bool LangSimpleFixtureReservedMath() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_math.simple", 0);
}

bool LangSimpleFixtureReservedTime() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_time.simple", 0);
}

bool LangSimpleFixtureReservedFile() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_file.simple", 0);
}

bool LangSimpleFixtureReservedIoBuffer() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_io_buffer.simple", 0);
}

bool LangSimpleFixtureReservedMathPi() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_math_pi.simple", 0);
}

bool LangSimpleFixtureArtifactNamedInit() {
  return RunSimpleFileExpectExit("Tests/simple/artifact_named_init.simple", 7);
}

bool LangSimpleFixtureArrayNested() {
  return RunSimpleFileExpectExit("Tests/simple/array_nested.simple", 3);
}

bool LangSimpleFixtureBoolOps() {
  return RunSimpleFileExpectExit("Tests/simple/bool_ops.simple", 1);
}

bool LangSimpleFixtureCharCompare() {
  return RunSimpleFileExpectExit("Tests/simple/char_compare.simple", 1);
}

bool LangSimpleFixtureCharEscapeHex() {
  return RunSimpleFileExpectExit("Tests/simple/char_escape_hex.simple", 1);
}

bool LangSimpleFixtureStringEscapeHex() {
  return RunSimpleFileExpectExit("Tests/simple/string_escape_hex.simple", 0);
}

bool LangSimpleFixtureCastI8ToI32() {
  return RunSimpleFileExpectExit("Tests/simple/cast_i8_to_i32.simple", 42);
}

bool LangSimpleFixtureStressLangFeatures() {
  return RunSimpleFileExpectExit("Tests/simple_modules/stress_lang_features_main.simple", 41);
}

bool LangSimpleFixtureStressRaylibLike() {
  int exit_code = 0;
  RunCommandCaptureStderr("bin/simple run Tests/simple_modules/stress_raylib_like_main.simple",
                          &exit_code);
  return exit_code == 16;
}

bool LangStressEnumAsTypeRuntime() {
  const char* src =
      "State :: enum { Idle = 0, Running = 1 }\n"
      "Task :: artifact { state : State }\n"
      "touch : State (s : State) { return s }\n"
      "main : i32 () {\n"
      "  t : Task = { touch(State.Running) }\n"
      "  return 1\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 1);
}

bool LangStressEnumAsTypeRejectScalarAssignment() {
  const char* src =
      "State :: enum { Idle = 0, Running = 1 }\n"
      "main : i32 () {\n"
      "  s : State = 1\n"
      "  return 0\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("type mismatch") != std::string::npos;
}

bool LangStressArtifactMethodMutationRuntime() {
  const char* src =
      "Counter :: artifact {\n"
      "  value : i32\n"
      "  add : void (step : i32) { self.value = self.value + step }\n"
      "  get : i32 () { return self.value }\n"
      "}\n"
      "main : i32 () {\n"
      "  c : Counter = { 0 }\n"
      "  c.add(19)\n"
      "  c.add(23)\n"
      "  return c.get()\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangStressArtifactMethodTypeStrict() {
  const char* src =
      "Counter :: artifact {\n"
      "  value : i32\n"
      "  add : void (step : i32) { self.value = self.value + step }\n"
      "}\n"
      "main : i32 () {\n"
      "  c : Counter = { 0 }\n"
      "  c.add(\"bad\")\n"
      "  return 0\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("call argument type mismatch") != std::string::npos;
}

bool LangStressProcedureVariableRuntime() {
  const char* src =
      "main : i32 () {\n"
      "  f : (i32, i32) : i32 = (a : i32, b : i32) { return a + b }\n"
      "  g : (i32) : i32 = (x : i32) { return x + 2 }\n"
      "  h : (i32, i32) : i32 = f\n"
      "  return 42\n"
      "}";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangStressProcedureParameterRuntime() {
  const char* src =
      "accept : void (f : (i32, i32) : i32) { return }\n"
      "main : i32 () {\n"
      "  accept((x : i32, y : i32) { return x + y })\n"
      "  return 0\n"
      "}";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangStressProcedureArgTypeStrict() {
  const char* src =
      "main : i32 () {\n"
      "  f : (i32) : i32 = (x : i32) { return x }\n"
      "  return f(\"oops\")\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("call argument type mismatch") != std::string::npos;
}

bool LangStressProcedureReturnTypeStrict() {
  const char* src =
      "main : i32 () {\n"
      "  f : (i32) : i32 = (x : i32) { return true }\n"
      "  g : (i32) : string = f\n"
      "  return 0\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("initializer type mismatch") != std::string::npos;
}

bool LangStressEnumArtifactProcedureCompositionRuntime() {
  const char* src =
      "Op :: enum { Add = 1, Mul = 2 }\n"
      "add : i32 (a : i32, b : i32) { return a + b }\n"
      "mul : i32 (a : i32, b : i32) { return a * b }\n"
      "Acc :: artifact {\n"
      "  op : Op\n"
      "  value : i32\n"
      "  step_add : void (x : i32) { self.value = add(self.value, x) }\n"
      "  step_mul : void (x : i32) { self.value = mul(self.value, x) }\n"
      "}\n"
      "main : i32 () {\n"
      "  a : Acc = { Op.Add, 2 }\n"
      "  a.step_add(5)\n"
      "  a.step_mul(6)\n"
      "  return a.value\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangStressImportChainCliRun() {
  return RunCommand("bin/simple run Tests/simple_modules/stress_import_main.simple");
}

bool LangStressImportMissingCliCheck() {
  int exit_code = 0;
  const std::string stderr_text =
      RunCommandCaptureStderr("bin/simple check Tests/simple_modules/stress_import_missing_main.simple",
                              &exit_code);
  return exit_code != 0 &&
         stderr_text.find("import file not found") != std::string::npos;
}

bool LangStressImportAmbiguousCliCheck() {
  int exit_code = 0;
  const std::string stderr_text =
      RunCommandCaptureStderr("bin/simple check Tests/simple_modules/stress_import_ambiguous_main.simple",
                              &exit_code);
  return exit_code != 0 &&
         stderr_text.find("ambiguous import path") != std::string::npos;
}

bool LangStressTypeExplicitArtifactFieldFail() {
  const char* src =
      "Wrap :: artifact { x : i32 }\n"
      "main : i32 () {\n"
      "  w : Wrap = { 1 }\n"
      "  w.x = \"bad\"\n"
      "  return 0\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("type mismatch") != std::string::npos;
}

bool LangStressParseCallMemberIndexPrecedence() {
  const char* src =
      "main : i32 () { return f(1).items[2].value + 3; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::Return) return false;
  const auto& expr = stmt.expr;
  if (expr.kind != Simple::Lang::ExprKind::Binary || expr.op != "+") return false;
  const auto& lhs = expr.children[0];
  if (lhs.kind != Simple::Lang::ExprKind::Member || lhs.text != "value") return false;
  if (lhs.children.empty()) return false;
  const auto& idx = lhs.children[0];
  if (idx.kind != Simple::Lang::ExprKind::Index) return false;
  if (idx.children.empty()) return false;
  const auto& items = idx.children[0];
  if (items.kind != Simple::Lang::ExprKind::Member || items.text != "items") return false;
  if (items.children.empty()) return false;
  if (items.children[0].kind != Simple::Lang::ExprKind::Call) return false;
  return true;
}

bool LangStressParseFnLiteralCallInCallArg() {
  const char* src =
      "apply : i32 (f : (i32) : i32, x : i32) { return x; }"
      "main : i32 () { return apply((x : i32) { return x + 1; }, 41); }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 2) return false;
  const auto& call = program.decls[1].func.body[0].expr;
  if (call.kind != Simple::Lang::ExprKind::Call) return false;
  if (call.args.size() != 2) return false;
  if (call.args[0].kind != Simple::Lang::ExprKind::FnLiteral) return false;
  return true;
}

bool LangStressParseForLoopComplexStep() {
  const char* src =
      "main : i32 () {"
      "  i : i32 = 0;"
      "  for i : i32 = 0; i < 10; i += 2 { skip; }"
      "  return i;"
      "}";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& body = program.decls[0].func.body;
  if (body.size() < 2) return false;
  const auto& loop = body[1];
  if (loop.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (loop.loop_step.kind != Simple::Lang::ExprKind::Binary) return false;
  if (loop.loop_step.op != "+=") return false;
  return true;
}

bool LangStressParseNestedIfElseInElseBranch() {
  const char* src =
      "main : i32 () {"
      "  if false { return 0; }"
      "  else { if true { return 1; } else { return 2; } }"
      "}";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::IfStmt) return false;
  if (stmt.if_else.size() != 1) return false;
  if (stmt.if_else[0].kind != Simple::Lang::StmtKind::IfStmt) return false;
  return true;
}

bool LangStressImportDeepChainCliRun() {
  int exit_code = 0;
  const std::string stderr_text =
      RunCommandCaptureStderr("bin/simple check Tests/simple_modules/stress_deep_main.simple",
                              &exit_code);
  return exit_code == 0 && stderr_text.empty();
}

bool LangStressImportRelativeSubdirCliRun() {
  int exit_code = 0;
  const std::string stderr_text =
      RunCommandCaptureStderr("bin/simple check Tests/simple_modules/stress_rel/main.simple",
                              &exit_code);
  return exit_code == 0 && stderr_text.empty();
}

bool LangStressImportCycleCliCheck() {
  int exit_code = 0;
  const std::string stderr_text =
      RunCommandCaptureStderr("bin/simple check Tests/simple_modules/stress_cycle_main.simple",
                              &exit_code);
  return exit_code != 0 &&
         stderr_text.find("cyclic import detected") != std::string::npos;
}

bool LangSimpleBadMissingReturn() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/missing_return.simple",
      "non-void function does not return on all paths");
}

bool LangSimpleBadTypeMismatch() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/type_mismatch.simple",
      "type mismatch");
}

bool LangSimpleBadPrintArray() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/print_array.simple",
      "IO.print");
}

bool LangSimpleBadImportUnknown() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/import_unknown.simple",
      "import");
}

bool LangSimpleBadEnumUnqualified() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/enum_unqualified.simple",
      "enum");
}

bool LangSimpleBadBreakOutsideLoop() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/break_outside_loop.simple",
      "break");
}

bool LangSimpleBadModuleVarAccess() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/module_var_access.simple",
      "module");
}

bool LangSimpleBadSelfOutsideArtifact() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/self_outside_artifact.simple",
      "self");
}

bool LangSimpleBadLenInvalidArg() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/len_invalid_arg.simple",
      "len");
}

bool LangSimpleBadIndexNonInt() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/index_non_int.simple",
      "index");
}

bool LangSimpleBadAssignToImmutable() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/assign_to_immutable.simple",
      "immutable");
}

bool LangSimpleBadUnknownIdentifier() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/unknown_identifier.simple",
      "undeclared identifier");
}

bool LangSimpleBadCallArgCount() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/call_arg_count.simple",
      "argument count");
}

bool LangSimpleBadModuleFuncReturnMismatch() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/module_func_return_mismatch.simple",
      "Math.bad");
}

bool LangSimpleBadUnknownType() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/unknown_type.simple",
      "unknown type");
}

bool LangSimpleBadEnumTypeAsValue() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/enum_type_as_value.simple",
      "enum type is not a value");
}

bool LangSimpleBadModuleAsType() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/module_as_type.simple",
      "module is not a type");
}

bool LangSimpleBadFunctionAsType() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/function_as_type.simple",
      "function is not a type");
}

bool LangSimpleBadArtifactMemberNoSelf() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/artifact_member_without_self.simple",
      "artifact members must be accessed via self");
}

bool LangSimpleBadEnumUnknownMember() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/enum_unknown_member.simple",
      "unknown enum member");
}

bool LangSimpleBadModuleUnknownMember() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/module_unknown_member.simple",
      "unknown module member");
}

bool LangSimpleBadArtifactUnknownMember() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/artifact_unknown_member.simple",
      "unknown artifact member");
}

bool LangSimpleBadArraySizeMismatch() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/array_size_mismatch.simple",
      "array literal size");
}

bool LangSimpleBadArrayElemTypeMismatch() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/array_elem_type_mismatch.simple",
      "array literal element type mismatch");
}

bool LangSimpleBadListElemTypeMismatch() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/list_elem_type_mismatch.simple",
      "array literal element type mismatch");
}

bool LangSimpleBadIndexNonContainer() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/index_non_container.simple",
      "indexing is only valid");
}

bool LangSimpleBadArrayMissingDim() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/array_missing_dim.simple",
      "array/list literal requires array or list type");
}

bool LangSimpleBadMissingSemicolonSameLine() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/missing_semicolon_same_line.simple",
      "expected ';'");
}

bool LangSimpleBadInvalidStringEscape() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/invalid_string_escape.simple",
      "invalid string escape");
}

bool LangSimpleBadInvalidCharEscape() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/invalid_char_escape.simple",
      "invalid char escape");
}

bool LangSimpleBadLexerInvalidChar() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/lexer_invalid_char.simple",
      "unexpected character");
}

bool LangSimpleBadParserUnterminatedBlock() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/parser_unterminated_block.simple",
      "unterminated block");
}

bool LangSimpleBadBoolArithmetic() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/bool_arith.simple",
      "operator '+' requires matching operand types");
}

bool LangSimpleBadCharCompareInt() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/char_compare_with_int.simple",
      "operator '==' requires matching operand types");
}

bool LangSimpleBadCharArithmetic() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/char_arith.simple",
      "operator '+' requires matching operand types");
}

bool LangSimpleBadInvalidHexEscape() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/invalid_hex_escape.simple",
      "invalid hex escape");
}

bool LangSimpleBadExternCallArgCount() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/extern_call_arg_count.simple",
      "argument count mismatch for extern");
}

bool LangSimpleBadCallArgTypeMismatch() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/call_arg_type_mismatch.simple",
      "call argument type mismatch");
}

bool LangSimpleBadIndexNonIntExpr() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/index_non_int_expr.simple",
      "index");
}

bool LangSimpleBadIndexNegative() {
  return Simple::VM::Tests::RunSimpleFileExpectTrap(
      "Tests/simple_bad/index_negative.simple",
      "runtime trap");
}

bool LangSimpleBadIndexOutOfBounds() {
  return Simple::VM::Tests::RunSimpleFileExpectTrap(
      "Tests/simple_bad/index_oob.simple",
      "runtime trap");
}

bool LangSimpleBadForRangeMissingEnd() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/for_range_missing_end.simple",
      "expected expression");
}

bool LangSimpleBadForMissingInit() {
  return Simple::VM::Tests::RunSimpleFileExpectError(
      "Tests/simple_bad/for_missing_init.simple",
      "expected expression");
}

bool LangCliCheckSimpleErrorFormat() {
  const std::string err_path = TempPath("simple_check_err.txt");
  const std::string cmd =
      "bin/simplevm check Tests/simple_bad/unknown_identifier.simple 2> " + err_path;
  if (RunCommand(cmd)) return false;
  std::ifstream in(err_path);
  if (!in) return false;
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return contents.find("error[E0001]:") != std::string::npos &&
         contents.find("undeclared identifier") != std::string::npos &&
         contents.find(" --> ") != std::string::npos &&
         contents.find('^') != std::string::npos;
}

bool LangCliCheckSimpleLexerErrorFormat() {
  const std::string err_path = TempPath("simple_check_lex_err.txt");
  const std::string cmd =
      "bin/simplevm check Tests/simple_bad/lexer_invalid_char.simple 2> " + err_path;
  if (RunCommand(cmd)) return false;
  std::ifstream in(err_path);
  if (!in) return false;
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return contents.find("error[E0001]:") != std::string::npos &&
         contents.find("unexpected character") != std::string::npos &&
         contents.find(" --> ") != std::string::npos &&
         contents.find('^') != std::string::npos;
}

bool LangCliCheckSimpleParserErrorFormat() {
  const std::string err_path = TempPath("simple_check_parse_err.txt");
  const std::string cmd =
      "bin/simplevm check Tests/simple_bad/parser_unterminated_block.simple 2> " + err_path;
  if (RunCommand(cmd)) return false;
  std::ifstream in(err_path);
  if (!in) return false;
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return contents.find("error[E0001]:") != std::string::npos &&
         contents.find("unterminated block") != std::string::npos &&
         contents.find(" --> ") != std::string::npos &&
         contents.find('^') != std::string::npos;
}

bool LangCliEmitIr() {
  const std::string out_path = TempPath("simple_emit_ir.sir");
  const std::string cmd = "bin/simplevm emit -ir Tests/simple/hello.simple --out " + out_path;
  if (!RunCommand(cmd)) return false;
  std::ifstream in(out_path);
  if (!in) return false;
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return contents.find("func") != std::string::npos;
}

bool LangCliEmitSbc() {
  const std::string out_path = TempPath("simple_emit_sbc.sbc");
  const std::string cmd = "bin/simplevm emit -sbc Tests/simple/hello.simple --out " + out_path;
  if (!RunCommand(cmd)) return false;
  std::ifstream in(out_path, std::ios::binary);
  return in.good() && in.peek() != std::ifstream::traits_type::eof();
}

bool LangCliCheckSimple() {
  return RunCommand("bin/simplevm check Tests/simple/hello.simple");
}

bool LangCliCheckSir() {
  return RunCommand("bin/simplevm check Tests/sir/fib_iter.sir");
}

bool LangCliCheckSbc() {
  return RunCommand("bin/simplevm check Tests/tests/fixtures/add_i32.sbc");
}

bool LangCliBuildSimple() {
  const std::string out_path = TempPath("simple_build_hello.sbc");
  const std::string cmd = "bin/simplevm build Tests/simple/hello.simple --out " + out_path;
  if (!RunCommand(cmd)) return false;
  std::ifstream in(out_path, std::ios::binary);
  return in.good() && in.peek() != std::ifstream::traits_type::eof();
}

bool LangCliBuildSimpleAliasDefaultsToExe() {
  const std::string out_path = TempPath("simple_build_hello_alias_exec");
  const std::string cmd = "bin/simple build Tests/simple/hello.simple --out " + out_path;
  if (!RunCommand(cmd)) return false;
  return RunCommand(out_path);
}

bool LangCliCompileSimpleAliasDefaultsToExe() {
  const std::string out_path = TempPath("simple_compile_hello_alias_exec");
  const std::string cmd = "bin/simple compile Tests/simple/hello.simple --out " + out_path;
  if (!RunCommand(cmd)) return false;
  return RunCommand(out_path);
}

bool LangCliBuildDynamicExe() {
  const std::string out_path = TempPath("simple_build_hello_exec");
  const std::string cmd =
      "bin/simplevm build -d Tests/simple/hello.simple --out " + out_path;
  if (!RunCommand(cmd)) return false;
  if (!RunCommand(out_path)) return false;
#if defined(__linux__)
  const std::string deps = RunCommandCaptureStdout("ldd " + out_path);
  if (deps.empty()) return false;
  if (deps.find("libsimplevm_runtime.so") == std::string::npos) return false;
#endif
  return true;
}

bool LangCliBuildStaticExe() {
  const std::string out_path = TempPath("simple_build_hello_exec_static");
  const std::string cmd =
      "bin/simplevm build -s Tests/simple/hello.simple --out " + out_path;
  if (!RunCommand(cmd)) return false;
  if (!RunCommand(out_path)) return false;
#if defined(__linux__)
  const std::string deps = RunCommandCaptureStdout("ldd " + out_path);
  if (deps.empty()) return false;
  if (deps.find("libsimplevm_runtime.so") != std::string::npos) return false;
#endif
  return true;
}

bool LangCliRunSimple() {
  return RunCommand("bin/simplevm run Tests/simple/hello.simple");
}

bool LangCliRunSimpleAlias() {
  return RunCommand("bin/simple run Tests/simple/hello.simple");
}

bool LangCliRunSimpleLocalImport() {
  return RunCommand("bin/simple run Tests/simple_modules/import_local_main.simple");
}

bool LangCliCheckSimpleAlias() {
  return RunCommand("bin/simple check Tests/simple/hello.simple");
}

bool LangCliSimpleRejectSir() {
  return RunCommandExpectFail("bin/simple check Tests/sir/fib_iter.sir");
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

bool LangSirEmitsIoPrintNewline() {
  const char* src =
      "main : i32 () { IO.print(\"hello\\n\"); return 3; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsIoPrintFormat() {
  const char* src =
      "main : i32 () { x : i32 = 7; IO.println(\"value={}\", x); return x; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
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

bool LangParseErrorIncludesLocation() {
  const char* src = "main : i32 () { $ }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  return error.find(':') != std::string::npos;
}

bool LangParseArtifactCommaDiagnosticHint() {
  const char* src = "Point :: artifact { x : i32, y : i32 }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  return error.find("use newline or ';' between members") != std::string::npos;
}

bool LangParseReservedKeywordParameterDiagnosticHint() {
  const char* src = "f : void (artifact: i32) { return; }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  return error.find("keyword 'artifact' cannot be used as identifier") != std::string::npos;
}

bool LangValidateErrorIncludesLocation() {
  const char* src = "main : i32 () { return missing }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("undeclared identifier") != std::string::npos &&
         error.find(':') != std::string::npos;
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

bool LangLexesRangeOp() {
  const char* src = "0..10";
  Simple::Lang::Lexer lex(src);
  if (!lex.Lex()) return false;
  const auto& toks = lex.Tokens();
  std::vector<Simple::Lang::TokenKind> kinds = {
    Simple::Lang::TokenKind::Integer,
    Simple::Lang::TokenKind::DotDot,
    Simple::Lang::TokenKind::Integer,
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

  Simple::Lang::TypeRef ptr;
  if (!Simple::Lang::ParseTypeFromString("*i32", &ptr, &error)) return false;
  if (ptr.name != "i32" || ptr.pointer_depth != 1) return false;
  if (!Simple::Lang::ParseTypeFromString("**void", &ptr, &error)) return false;
  if (ptr.name != "void" || ptr.pointer_depth != 2) return false;

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

bool LangParsesArtifactDeclCapitalized() {
  const char* src = "Point :: Artifact { x : f32 y :: f32 len : i32 () { return 1; } }";
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

bool LangParsesModuleDeclCapitalized() {
  const char* src = "Math :: Module { scale : i32 = 2; add : i32 (a : i32, b : i32) { return a + b; } }";
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

bool LangParsesImportDecl() {
  const char* src = "import \"raylib\"";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Import) return false;
  if (decl.import_decl.path != "raylib") return false;
  if (!decl.import_decl.has_alias) return false;
  if (decl.import_decl.alias != "Ray") return false;
  return true;
}

bool LangParsesImportDeclUnquotedPath() {
  const char* src = "import System.io";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Import) return false;
  if (decl.import_decl.path != "System.io") return false;
  if (decl.import_decl.has_alias) return false;
  return true;
}

bool LangValidateSystemImportRejectsMixedCase() {
  const char* src =
      "import sYsTeM.iO as IO\n"
      "main : void () { IO.println(1); }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateSystemImportImplicitLowerAlias() {
  const char* src =
      "import system.io\n"
      "main : void () { io.println(1); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateSystemOsCapabilityConstants() {
  const char* src =
      "import system.os\n"
      "main : i32 () { if os.is_linux || os.is_macos || os.is_windows { return 1 } return 0 }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateSystemDlCapabilityConstant() {
  const char* src =
      "import system.dl\n"
      "main : i32 () { if dl.supported { return 1 } return 0 }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateUnknownReservedMemberSuggestsClosest() {
  const char* src =
      "import system.io\n"
      "main : void () { io.printlnn(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("did you mean 'println'") != std::string::npos;
}

bool LangValidateSystemIoBufferApis() {
  const char* src =
      "import system.io\n"
      "main : i32 () {\n"
      "  a : i32[] = io.buffer_new(4);\n"
      "  b : i32[] = io.buffer_new(4);\n"
      "  io.buffer_fill(a, 7, 3);\n"
      "  io.buffer_copy(b, a, 4);\n"
      "  return io.buffer_len(b);\n"
      "}";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangParsesExternDecl() {
  const char* src = "extern Ray.InitWindow : void (w : i32, h : i32)";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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

bool LangValidateExternCallOk() {
  const char* src =
      "extern Ray.InitWindow : void (w : i32, h : i32)\n"
      "main : i32 () { Ray.InitWindow(1, 2); return 0; }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateExternPointerCallOk() {
  const char* src =
      "Node :: artifact { next: *Node }\n"
      "extern C.walk : *Node (head : *Node)\n"
      "main : i32 () { return 0; }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
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

bool LangParsesEnumDeclCapitalized() {
  const char* src =
    "Status :: Enum { Pending = 1, Active = 2 }"
    "Color :: Enum { Red, Green, Blue }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
    "  for x : i32 = x; x < 1; x = x + 1 { x : i32 = 2; }"
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

bool LangValidateIoPrintFormatOk() {
  const char* src = "main : void () { x : i32 = 42; IO.println(\"x={}\", x); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateIoPrintFormatPlaceholderMismatch() {
  const char* src = "main : void () { IO.println(\"x={}, y={}\", 1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("format placeholder count mismatch") != std::string::npos;
}

bool LangValidateIoPrintFormatNeedsStringLiteral() {
  const char* src = "main : void () { fmt : string = \"x={}\"; IO.println(fmt, 1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("format call expects string literal") != std::string::npos;
}

bool LangRunsSimpleFixtures() {
  const std::string dir = "Tests/simple";
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

bool LangValidateFnShorthandAssignAndCallOk() {
  const char* src =
      "Player :: artifact { position : i32 velocity : i32 }\n"
      "main : i32 () {\n"
      "  update : fn = void (p : Player) { p.position += p.velocity }\n"
      "  player : Player = { 40, 2 }\n"
      "  update(player)\n"
      "  return player.position\n"
      "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangSirEmitsFnShorthandAssignAndCall() {
  const char* src =
      "main : i32 () {\n"
      "  f : fn = i32 (a : i32, b : i32) { return a + b }\n"
      "  return f(20, 22)\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangValidateCallbackParamWithFnArgOk() {
  const char* src =
      "invoke : void (cb : callback, x : i32) { cb(x) }\n"
      "main : i32 () {\n"
      "  printv : fn = void (v : i32) { IO.println(v) }\n"
      "  invoke(printv, 42)\n"
      "  return 0\n"
      "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangSirEmitsCallbackParamWithFnArgCall() {
  const char* src =
      "invoke : void (cb : callback, x : i32) { cb(x) }\n"
      "main : i32 () {\n"
      "  noop : fn = void (v : i32) { return; }\n"
      "  invoke(noop, 7)\n"
      "  return 0\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 0);
}

bool LangValidateCallbackTypeInVarDeclRejected() {
  const char* src = "main : void () { cb : callback; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallbackTypeInReturnRejected() {
  const char* src = "make : callback () { return; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallbackTypeInArtifactFieldRejected() {
  const char* src = "Node :: artifact { cb : callback }";
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
  const char* src = "main : i32 () { s : string = \"42\"; return @i32(s); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateI32FromI32Ok() {
  const char* src = "main : i32 () { x : i32 = 1; return @i32(x); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCastWithoutAtFails() {
  const char* src = "main : i32 () { x : i8 = 1; return i32(x); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("primitive cast syntax requires '@'") != std::string::npos;
}

bool LangValidateF64FromStringOk() {
  const char* src = "main : f64 () { s : string = \"1.5\"; return @f64(s); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateF64FromF64Ok() {
  const char* src = "main : f64 () { x : f64 = 1.0; return @f64(x); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
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

bool LangParsesFnShorthandLiteralBinding() {
  const char* src = "main : void () { f : fn = i32 (a : i32, b : i32) { return a + b; }; }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
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
  const char* src = "main : void () { for i : i32 = 0; i < 10; i = i + 1 { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  return true;
}

bool LangParsesForLoopPostInc() {
  const char* src = "main : void () { for i : i32 = 0; i < 10; i++ { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (stmt.loop_step.kind != Simple::Lang::ExprKind::Unary) return false;
  return true;
}

bool LangParsesForLoopRange() {
  const char* src = "main : void () { for i : i32 = 0; 0..10 { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (!stmt.has_loop_var_decl) return false;
  if (stmt.loop_cond.kind != Simple::Lang::ExprKind::Binary) return false;
  if (stmt.loop_cond.op != "<=") return false;
  if (stmt.loop_step.kind != Simple::Lang::ExprKind::Unary) return false;
  return true;
}

bool LangParsesForLoopRangeDefaultType() {
  const char* src = "main : void () { for i; 0..10 { skip; } }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::ForLoop) return false;
  if (!stmt.has_loop_var_decl) return false;
  if (stmt.loop_var_decl.type.name != "i32") return false;
  return true;
}
const TestCase kLangTests[] = {
  {"lang_lex_keywords_ops", LangLexesKeywordsAndOps},
  {"lang_lex_range_op", LangLexesRangeOp},
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
  {"lang_parse_artifact_decl_capitalized", LangParsesArtifactDeclCapitalized},
  {"lang_parse_module_decl", LangParsesModuleDecl},
  {"lang_parse_module_decl_capitalized", LangParsesModuleDeclCapitalized},
  {"lang_parse_import_decl", LangParsesImportDecl},
  {"lang_parse_import_decl_alias", LangParsesImportDeclAlias},
  {"lang_parse_import_decl_unquoted_path", LangParsesImportDeclUnquotedPath},
  {"lang_validate_system_import_rejects_mixed_case", LangValidateSystemImportRejectsMixedCase},
  {"lang_validate_system_import_implicit_lower_alias", LangValidateSystemImportImplicitLowerAlias},
  {"lang_validate_system_os_capability_constants", LangValidateSystemOsCapabilityConstants},
  {"lang_validate_system_dl_capability_constant", LangValidateSystemDlCapabilityConstant},
  {"lang_validate_unknown_reserved_member_suggests_closest", LangValidateUnknownReservedMemberSuggestsClosest},
  {"lang_validate_system_io_buffer_apis", LangValidateSystemIoBufferApis},
  {"lang_parse_extern_decl", LangParsesExternDecl},
  {"lang_validate_extern_call_ok", LangValidateExternCallOk},
  {"lang_validate_extern_pointer_call_ok", LangValidateExternPointerCallOk},
  {"lang_parse_enum_decl", LangParsesEnumDecl},
  {"lang_parse_enum_decl_capitalized", LangParsesEnumDeclCapitalized},
  {"lang_parse_return_expr", LangParsesReturnExpr},
  {"lang_parse_call_member", LangParsesCallAndMember},
  {"lang_parse_self", LangParsesSelf},
  {"lang_parse_qualified_member", LangParsesQualifiedMember},
  {"lang_parse_reject_double_colon_member", LangRejectsDoubleColonMember},
  {"lang_sir_emit_return_i32", LangSirEmitsReturnI32},
  {"lang_sir_top_level_script_executes", LangSirTopLevelScriptExecutes},
  {"lang_top_level_return_disallowed", LangTopLevelReturnDisallowed},
  {"lang_top_level_io_println_arithmetic", LangTopLevelIoPrintlnArithmetic},
  {"lang_sir_emit_local_assign", LangSirEmitsLocalAssign},
  {"lang_sir_emit_if_else", LangSirEmitsIfElse},
  {"lang_sir_emit_while_loop", LangSirEmitsWhileLoop},
  {"lang_sir_emit_function_call", LangSirEmitsFunctionCall},
  {"lang_sir_emit_io_print_string", LangSirEmitsIoPrintString},
  {"lang_sir_emit_io_print_i32", LangSirEmitsIoPrintI32},
  {"lang_sir_emit_io_print_newline", LangSirEmitsIoPrintNewline},
  {"lang_sir_emit_io_print_format", LangSirEmitsIoPrintFormat},
  {"lang_sir_implicit_main_return", LangSirImplicitMainReturn},
  {"lang_parse_missing_semicolon_same_line", LangParseMissingSemicolonSameLine},
  {"lang_parse_error_includes_location", LangParseErrorIncludesLocation},
  {"lang_parse_artifact_comma_diagnostic_hint", LangParseArtifactCommaDiagnosticHint},
  {"lang_parse_reserved_keyword_parameter_hint", LangParseReservedKeywordParameterDiagnosticHint},
  {"lang_validate_error_includes_location", LangValidateErrorIncludesLocation},
  {"lang_simple_fixture_hello", LangSimpleFixtureHello},
  {"lang_simple_fixture_math", LangSimpleFixtureMath},
  {"lang_simple_fixture_sum_loop", LangSimpleFixtureSumLoop},
  {"lang_simple_fixture_sum_array", LangSimpleFixtureSumArray},
  {"lang_simple_fixture_point_sum", LangSimpleFixturePointSum},
  {"lang_simple_fixture_list_len", LangSimpleFixtureListLen},
  {"lang_simple_fixture_list_nested", LangSimpleFixtureListNested},
  {"lang_simple_fixture_list_methods", LangSimpleFixtureListMethods},
  {"lang_simple_fixture_array_empty", LangSimpleFixtureArrayEmpty},
  {"lang_simple_fixture_list_empty", LangSimpleFixtureListEmpty},
  {"lang_simple_fixture_add_fn", LangSimpleFixtureAddFn},
  {"lang_simple_fixture_if_else", LangSimpleFixtureIfElse},
  {"lang_simple_fixture_for_loop", LangSimpleFixtureForLoop},
  {"lang_simple_fixture_for_range_loop", LangSimpleFixtureForRangeLoop},
  {"lang_simple_fixture_for_range_header_init", LangSimpleFixtureForRangeHeaderInit},
  {"lang_simple_fixture_while_break", LangSimpleFixtureWhileBreak},
  {"lang_simple_fixture_enum_value", LangSimpleFixtureEnumValue},
  {"lang_simple_fixture_enum_explicit", LangSimpleFixtureEnumExplicit},
  {"lang_simple_fixture_module_access", LangSimpleFixtureModuleAccess},
  {"lang_simple_fixture_io_print", LangSimpleFixtureIoPrint},
  {"lang_simple_fixture_fn_literal", LangSimpleFixtureFnLiteral},
  {"lang_simple_fixture_array_assign", LangSimpleFixtureArrayAssign},
  {"lang_simple_fixture_list_index", LangSimpleFixtureListIndex},
  {"lang_simple_fixture_string_len", LangSimpleFixtureStringLen},
  {"lang_simple_fixture_artifact_method", LangSimpleFixtureArtifactMethod},
  {"lang_simple_fixture_artifact_named_init", LangSimpleFixtureArtifactNamedInit},
  {"lang_simple_fixture_array_nested", LangSimpleFixtureArrayNested},
  {"lang_simple_fixture_bool_ops", LangSimpleFixtureBoolOps},
  {"lang_simple_fixture_char_compare", LangSimpleFixtureCharCompare},
  {"lang_simple_fixture_char_escape_hex", LangSimpleFixtureCharEscapeHex},
  {"lang_simple_fixture_string_escape_hex", LangSimpleFixtureStringEscapeHex},
  {"lang_simple_fixture_cast_i8_to_i32", LangSimpleFixtureCastI8ToI32},
  {"lang_simple_fixture_stress_lang_features", LangSimpleFixtureStressLangFeatures},
  {"lang_simple_fixture_stress_raylib_like", LangSimpleFixtureStressRaylibLike},
  {"lang_simple_fixture_module_multi", LangSimpleFixtureModuleMulti},
  {"lang_simple_fixture_module_func_params", LangSimpleFixtureModuleFuncParams},
  {"lang_simple_fixture_import_basic", LangSimpleFixtureImportBasic},
  {"lang_simple_fixture_extern_decl", LangSimpleFixtureExternDecl},
  {"lang_simple_fixture_extern_core_os_args_count", LangSimpleFixtureExternCoreOsArgsCount},
  {"lang_simple_fixture_core_dl_open", LangSimpleFixtureCoreDlOpen},
  {"lang_simple_fixture_core_dl_open_global", LangSimpleFixtureCoreDlOpenGlobal},
  {"lang_simple_fixture_float_literal_context", LangSimpleFixtureFloatLiteralContext},
  {"lang_simple_fixture_reserved_math", LangSimpleFixtureReservedMath},
  {"lang_simple_fixture_reserved_math_pi", LangSimpleFixtureReservedMathPi},
  {"lang_simple_fixture_reserved_time", LangSimpleFixtureReservedTime},
  {"lang_simple_fixture_reserved_io_buffer", LangSimpleFixtureReservedIoBuffer},
  {"lang_simple_fixture_reserved_file", LangSimpleFixtureReservedFile},
  {"lang_stress_enum_as_type_runtime", LangStressEnumAsTypeRuntime},
  {"lang_stress_enum_as_type_reject_scalar_assignment", LangStressEnumAsTypeRejectScalarAssignment},
  {"lang_stress_artifact_method_mutation_runtime", LangStressArtifactMethodMutationRuntime},
  {"lang_stress_artifact_method_type_strict", LangStressArtifactMethodTypeStrict},
  {"lang_stress_procedure_variable_runtime", LangStressProcedureVariableRuntime},
  {"lang_stress_procedure_parameter_runtime", LangStressProcedureParameterRuntime},
  {"lang_stress_procedure_arg_type_strict", LangStressProcedureArgTypeStrict},
  {"lang_stress_procedure_return_type_strict", LangStressProcedureReturnTypeStrict},
  {"lang_stress_enum_artifact_procedure_composition_runtime", LangStressEnumArtifactProcedureCompositionRuntime},
  {"lang_stress_import_chain_cli_run", LangStressImportChainCliRun},
  {"lang_stress_import_missing_cli_check", LangStressImportMissingCliCheck},
  {"lang_stress_import_ambiguous_cli_check", LangStressImportAmbiguousCliCheck},
  {"lang_stress_type_explicit_artifact_field_fail", LangStressTypeExplicitArtifactFieldFail},
  {"lang_stress_parse_call_member_index_precedence", LangStressParseCallMemberIndexPrecedence},
  {"lang_stress_parse_fn_literal_call_in_call_arg", LangStressParseFnLiteralCallInCallArg},
  {"lang_stress_parse_for_loop_complex_step", LangStressParseForLoopComplexStep},
  {"lang_stress_parse_nested_if_else_in_else_branch", LangStressParseNestedIfElseInElseBranch},
  {"lang_stress_import_deep_chain_cli_run", LangStressImportDeepChainCliRun},
  {"lang_stress_import_relative_subdir_cli_run", LangStressImportRelativeSubdirCliRun},
  {"lang_stress_import_cycle_cli_check", LangStressImportCycleCliCheck},
  {"lang_simple_bad_missing_return", LangSimpleBadMissingReturn},
  {"lang_simple_bad_type_mismatch", LangSimpleBadTypeMismatch},
  {"lang_simple_bad_print_array", LangSimpleBadPrintArray},
  {"lang_simple_bad_import_unknown", LangSimpleBadImportUnknown},
  {"lang_simple_bad_enum_unqualified", LangSimpleBadEnumUnqualified},
  {"lang_simple_bad_enum_unknown_member", LangSimpleBadEnumUnknownMember},
  {"lang_simple_bad_break_outside_loop", LangSimpleBadBreakOutsideLoop},
  {"lang_simple_bad_module_var_access", LangSimpleBadModuleVarAccess},
  {"lang_simple_bad_self_outside_artifact", LangSimpleBadSelfOutsideArtifact},
  {"lang_simple_bad_len_invalid_arg", LangSimpleBadLenInvalidArg},
  {"lang_simple_bad_index_non_int", LangSimpleBadIndexNonInt},
  {"lang_simple_bad_assign_to_immutable", LangSimpleBadAssignToImmutable},
  {"lang_simple_bad_unknown_identifier", LangSimpleBadUnknownIdentifier},
  {"lang_simple_bad_call_arg_count", LangSimpleBadCallArgCount},
  {"lang_simple_bad_module_func_return_mismatch", LangSimpleBadModuleFuncReturnMismatch},
  {"lang_simple_bad_unknown_type", LangSimpleBadUnknownType},
  {"lang_simple_bad_enum_type_as_value", LangSimpleBadEnumTypeAsValue},
  {"lang_simple_bad_module_as_type", LangSimpleBadModuleAsType},
  {"lang_simple_bad_function_as_type", LangSimpleBadFunctionAsType},
  {"lang_simple_bad_artifact_member_no_self", LangSimpleBadArtifactMemberNoSelf},
  {"lang_simple_bad_module_unknown_member", LangSimpleBadModuleUnknownMember},
  {"lang_simple_bad_artifact_unknown_member", LangSimpleBadArtifactUnknownMember},
  {"lang_simple_bad_array_size_mismatch", LangSimpleBadArraySizeMismatch},
  {"lang_simple_bad_array_elem_type_mismatch", LangSimpleBadArrayElemTypeMismatch},
  {"lang_simple_bad_list_elem_type_mismatch", LangSimpleBadListElemTypeMismatch},
  {"lang_simple_bad_index_non_container", LangSimpleBadIndexNonContainer},
  {"lang_simple_bad_array_missing_dim", LangSimpleBadArrayMissingDim},
  {"lang_simple_bad_missing_semicolon_same_line", LangSimpleBadMissingSemicolonSameLine},
  {"lang_simple_bad_invalid_string_escape", LangSimpleBadInvalidStringEscape},
  {"lang_simple_bad_invalid_char_escape", LangSimpleBadInvalidCharEscape},
  {"lang_simple_bad_lexer_invalid_char", LangSimpleBadLexerInvalidChar},
  {"lang_simple_bad_parser_unterminated_block", LangSimpleBadParserUnterminatedBlock},
  {"lang_simple_bad_bool_arithmetic", LangSimpleBadBoolArithmetic},
  {"lang_simple_bad_char_compare_int", LangSimpleBadCharCompareInt},
  {"lang_simple_bad_char_arithmetic", LangSimpleBadCharArithmetic},
  {"lang_simple_bad_invalid_hex_escape", LangSimpleBadInvalidHexEscape},
  {"lang_simple_bad_extern_call_arg_count", LangSimpleBadExternCallArgCount},
  {"lang_simple_bad_call_arg_type_mismatch", LangSimpleBadCallArgTypeMismatch},
  {"lang_simple_bad_index_non_int_expr", LangSimpleBadIndexNonIntExpr},
  {"lang_simple_bad_index_negative", LangSimpleBadIndexNegative},
  {"lang_simple_bad_index_oob", LangSimpleBadIndexOutOfBounds},
  {"lang_simple_bad_for_range_missing_end", LangSimpleBadForRangeMissingEnd},
  {"lang_simple_bad_for_missing_init", LangSimpleBadForMissingInit},
  {"lang_cli_emit_ir", LangCliEmitIr},
  {"lang_cli_emit_sbc", LangCliEmitSbc},
  {"lang_cli_check_simple", LangCliCheckSimple},
  {"lang_cli_check_sir", LangCliCheckSir},
  {"lang_cli_check_sbc", LangCliCheckSbc},
  {"lang_cli_build_simple", LangCliBuildSimple},
  {"lang_cli_build_simple_alias_defaults_to_exe", LangCliBuildSimpleAliasDefaultsToExe},
  {"lang_cli_compile_simple_alias_defaults_to_exe", LangCliCompileSimpleAliasDefaultsToExe},
  {"lang_cli_build_dynamic_exe", LangCliBuildDynamicExe},
  {"lang_cli_build_static_exe", LangCliBuildStaticExe},
  {"lang_cli_run_simple", LangCliRunSimple},
  {"lang_cli_run_simple_alias", LangCliRunSimpleAlias},
  {"lang_cli_run_simple_local_import", LangCliRunSimpleLocalImport},
  {"lang_cli_check_simple_alias", LangCliCheckSimpleAlias},
  {"lang_cli_simple_reject_sir", LangCliSimpleRejectSir},
  {"lang_cli_check_simple_error_format", LangCliCheckSimpleErrorFormat},
  {"lang_cli_check_simple_lexer_error_format", LangCliCheckSimpleLexerErrorFormat},
  {"lang_cli_check_simple_parser_error_format", LangCliCheckSimpleParserErrorFormat},
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
  {"lang_sir_emit_fn_shorthand_assign_call", LangSirEmitsFnShorthandAssignAndCall},
  {"lang_sir_emit_callback_param_fn_arg_call", LangSirEmitsCallbackParamWithFnArgCall},
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
  {"lang_validate_io_print_format_ok", LangValidateIoPrintFormatOk},
  {"lang_validate_io_print_format_placeholder_mismatch", LangValidateIoPrintFormatPlaceholderMismatch},
  {"lang_validate_io_print_format_requires_literal", LangValidateIoPrintFormatNeedsStringLiteral},
  {"lang_run_simple_fixtures", LangRunsSimpleFixtures},
  {"lang_validate_call_fn_literal_count", LangValidateCallFnLiteralCount},
  {"lang_validate_call_fn_literal_ok", LangValidateCallFnLiteralOk},
  {"lang_validate_fn_shorthand_assign_call_ok", LangValidateFnShorthandAssignAndCallOk},
  {"lang_validate_callback_param_fn_arg_ok", LangValidateCallbackParamWithFnArgOk},
  {"lang_validate_callback_type_var_rejected", LangValidateCallbackTypeInVarDeclRejected},
  {"lang_validate_callback_type_return_rejected", LangValidateCallbackTypeInReturnRejected},
  {"lang_validate_callback_type_artifact_field_rejected", LangValidateCallbackTypeInArtifactFieldRejected},
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
  {"lang_validate_i32_from_i32_ok", LangValidateI32FromI32Ok},
  {"lang_validate_cast_without_at_fails", LangValidateCastWithoutAtFails},
  {"lang_validate_f64_from_string_ok", LangValidateF64FromStringOk},
  {"lang_validate_f64_from_f64_ok", LangValidateF64FromF64Ok},
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
  {"lang_parse_fn_shorthand_literal_binding", LangParsesFnShorthandLiteralBinding},
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
  {"lang_parse_for_loop_range", LangParsesForLoopRange},
  {"lang_parse_for_loop_range_default_type", LangParsesForLoopRangeDefaultType},
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
