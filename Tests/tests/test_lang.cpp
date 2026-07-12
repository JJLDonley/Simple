#include "diagnostic_render.h"
#include "Diagnostics/diagnostic.h"
#include "Lexer/lexer.h"
#include "Lexer/token.h"
#include "CAST/parser.h"
#include "AST/ast.h"
#include "AST/lower_cast.h"
#include "RAST/rast.h"
#include "RAST/import_graph.h"
#include "RAST/import_loader.h"
#include "RAST/member_resolution.h"
#include "RAST/reserved_resolution.h"
#include "RAST/resolver.h"
#include "RAST/symbol_table.h"
#include "TAST/tast.h"
#include "TAST/abi.h"
#include "TAST/calls.h"
#include "TAST/expressions.h"
#include "TAST/generics.h"
#include "TAST/literals.h"
#include "TAST/mutability.h"
#include "TAST/statements.h"
#include "TAST/type_checker.h"
#include "TAST/control_flow.h"
#include "TAST/types.h"
#include "lang_library.h"
#include "IRB/ir_builder.h"
#include "IRE/sir_emitter.h"
#include "Lexer/lexer.h"
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
#include <iterator>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace Simple::VM::Tests {
namespace {

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

std::string RunCommandCaptureStderr(const std::string& command, int* out_exit_code = nullptr) {
  const std::string err_path = TempPath("simple_command_stderr_capture.txt");
  const std::string wrapped = command + " 1>/dev/null 2> " + err_path;
  const int result = std::system(wrapped.c_str());
  if (out_exit_code) *out_exit_code = SystemExitCode(result);
  return ReadFileText(err_path);
}

std::string TempPath(const std::string& name) {
  namespace fs = std::filesystem;
  return (fs::temp_directory_path() / name).string();
}

bool RunSimpleFileExpectExit(const std::string& path, int32_t expected) {
  int exit_code = Simple::VM::Tests::RunSimpleFile(path, true);
  return exit_code == expected;
}

bool LangSirEmitsReturnI32() {
  const char* src = "main : i32 () { return 40 + 2; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangIreSerializesPrecomputedSirLines() {
  Simple::Lang::IRB::Module module;
  module.sir_text = "bad fallback";
  module.sir_lines = {
      "sigs:",
      "  sig main: () -> i32",
      "func main locals=0 stack=1 sig=main",
      "  enter 0",
      "  const i32 42",
      "  ret",
      "end",
      "entry main",
  };
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirModule(module, &sir, &error)) return false;
  if (sir.find("bad fallback") != std::string::npos) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangPhaseHeadersCompileAndPreserveBehavior() {
  Simple::Lang::Lexer lexer("main : i32 () { return 0; }");
  if (!lexer.Lex()) return false;
  if (lexer.Tokens().empty()) return false;
  if (lexer.Tokens().front().kind != Simple::Lang::TokenKind::Identifier) return false;

  Simple::Lang::CAST::Parser parser(lexer.Tokens());
  Simple::Lang::Program cast_program;
  if (!parser.ParseProgram(&cast_program)) return false;

  Simple::Lang::AST::Program* ast_program = &cast_program;
  Simple::Lang::RAST::ResolvedProgramView resolved{ast_program};
  Simple::Lang::TAST::TypedProgramView typed{resolved.program};
  if (typed.program == nullptr) return false;

  Simple::Lang::IRB::Module module;
  std::string error;
  if (!Simple::Lang::IRE::EmitSir(*typed.program, &module.sir_text, &error)) return false;
  return module.sir_text.find("entry main") != std::string::npos;
}

bool LangNestedArtifactMethodSwitchIfChainRuntime() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 () {\n"
      "    tmp : i32 = 0;\n"
      "    out : i32 = switch (self.v) {\n"
      "      self.v > 0 => {\n"
      "        |> (self.v == 1) { tmp = 10; }\n"
      "        |> default { tmp = 20; }\n"
      "        return tmp\n"
      "      }\n"
      "      default => return 3\n"
      "    };\n"
      "    return out;\n"
      "  }\n"
      "}\n"
      "main : i32 () {\n"
      "  b : Box = { 2 };\n"
      "  return b.score();\n"
      "}\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 20);
}

bool LangNestedArtifactMethodSwitchIfChainBadCondition() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 () {\n"
      "    tmp : i32 = 0;\n"
      "    out : i32 = switch (self.v) {\n"
      "      self.v > 0 => {\n"
      "        |> (self.v) { tmp = 10; }\n"
      "        |> default { tmp = 20; }\n"
      "        return tmp\n"
      "      }\n"
      "      default => return 3\n"
      "    };\n"
      "    return out;\n"
      "  }\n"
      "}\n";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("condition must be bool") != std::string::npos;
}

bool LangNestedSwitchBranchBlockLocalRuntime() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 () {\n"
      "    return switch (self.v) {\n"
      "      self.v > 0 => {\n"
      "        local : i32 = self.v + 40;\n"
      "        return local\n"
      "      }\n"
      "      default => return 1\n"
      "    };\n"
      "  }\n"
      "}\n"
      "main : i32 () { b : Box = { 2 }; return b.score(); }\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 42);
}

bool LangNestedSwitchBranchPreservesLoopContextRuntime() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : i32 () {\n"
      "    while (true) {\n"
      "      out : i32 = switch (self.v) {\n"
      "        self.v > 0 => {\n"
      "          |> (self.v == 2) { break }\n"
      "          |> default { self.v = 3; }\n"
      "          return 1\n"
      "        }\n"
      "        default => return 0\n"
      "      };\n"
      "      return out;\n"
      "    }\n"
      "    return 9;\n"
      "  }\n"
      "}\n"
      "main : i32 () { b : Box = { 2 }; return b.score(); }\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 9);
}

bool LangNestedSwitchBranchSiblingLocalsRuntime() {
  const char* src =
      "main : i32 () {\n"
      "  mode : i32 = 2;\n"
      "  value : i32 = switch (mode) {\n"
      "    mode == 1 => { local : i32 = 10; return local }\n"
      "    mode == 2 => { local : i32 = 40; return local + 2 }\n"
      "    default => return 0\n"
      "  };\n"
      "  local : i32 = value;\n"
      "  return local;\n"
      "}\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 42);
}

bool LangNestedIfSiblingLocalsRuntime() {
  const char* src =
      "main : i32 () {\n"
      "  out : i32 = 0;\n"
      "  if (true) { local : i32 = 42; out = local; }\n"
      "  else { local : i32 = 1; out = local; }\n"
      "  local : i32 = out;\n"
      "  return local;\n"
      "}\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 42);
}

bool LangConditionalReturnMainImplicitFallbackRuntime() {
  const char* src =
      "main : i32 () {\n"
      "  if (false) { return 7; }\n"
      "}\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 0);
}

bool LangIfChainAllBranchesReturnNoFallbackRuntime() {
  const char* src =
      "main : i32 () {\n"
      "  x : i32 = 2;\n"
      "  |> (x == 1) { return 1; }\n"
      "  |> (x == 2) { return 42; }\n"
      "  |> default { return 3; }\n"
      "}\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 42);
}

bool LangSwitchExprStmtBranchLocalRuntime() {
  const char* src =
      "main : i32 () {\n"
      "  mode : i32 = 1;\n"
      "  switch (mode) {\n"
      "    mode == 1 => { local : i32 = 42; return local }\n"
      "    default => return 0\n"
      "  };\n"
      "  return 7;\n"
      "}\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) {
    std::cerr << error << "\n";
    return false;
  }
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirTopLevelScriptExecutes() {
  const char* src =
      "add : i32 (a : i32, b : i32) { return a + b; }\n"
      "x : i32 = add(40, 2);\n"
      "x = x + 1;\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  if (sir.find("entry __script_entry") == std::string::npos) return false;
  return RunSirTextExpectExit(sir, 0);
}

bool LangSirMainOverridesTopLevel() {
  const char* src =
      "x : i32 = 1;\n"
      "main : i32 () { return 7; }\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  if (sir.find("entry main") == std::string::npos) return false;
  if (sir.find("entry __script_entry") != std::string::npos) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangTopLevelReturnDisallowed() {
  const char* src = "return 1;";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("top-level return is not allowed") != std::string::npos;
}

bool LangTopLevelIoPrintlnArithmetic() {
  const char* src =
      "import Standard.IO\n"
      "Standard.IO.println(\"Hello World\");\n"
      "Standard.IO.println(10 + 20 + 60 / 3);\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
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
  return RunSimpleFileExpectExit("Tests/simple/extern_System_os_args_count.simple", 0);
}

bool LangSimpleFixtureCoreDlOpen() {
  return RunSimpleFileExpectExit("Tests/simple/System_dl_open.simple", 1);
}

bool LangSimpleFixtureCoreDlOpenGlobal() {
  return RunSimpleFileExpectExit("Tests/simple/System_dl_open_global.simple", 1);
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

bool LangGcRefTracingStress() {
  return RunSimpleFileExpectExit("Tests/simple/gc_ref_tracing_stress.simple", 0);
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

bool LangSimpleFixtureStringEscape() {
  return RunSimpleFileExpectExit("Tests/simple/string_escape.simple", 0);
}

bool LangSimpleFixtureStringEquality() {
  return RunSimpleFileExpectExit("Tests/simple/string_equality.simple", 0);
}

bool LangSimpleFixtureSemicolonsSameLine() {
  return RunSimpleFileExpectExit("Tests/simple/semicolons_same_line.simple", 3);
}

bool LangSimpleFixtureMainImplicitReturn() {
  return RunSimpleFileExpectExit("Tests/simple/main_implicit_return.simple", 0);
}

bool LangSimpleFixtureCastI8ToI32() {
  return RunSimpleFileExpectExit("Tests/simple/cast_i8_to_i32.simple", 42);
}

bool LangSimpleFixtureStressLangFeatures() {
  return RunSimpleFileExpectExit("Tests/simple_modules/stress_lang_features_main.simple", 41);
}

bool LangSimpleFixtureStressRaylibLike() {
  int exit_code = 0;
  RunCommandCaptureStderr("bin/svm run Tests/simple_modules/stress_raylib_like_main.simple",
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
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
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
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
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
      "  f : fn i32 (a : i32, b : i32) = (a, b) { return a + b }\n"
      "  g : fn i32 (x : i32) = (x) { return x + 2 }\n"
      "  h : fn i32 (a : i32, b : i32) = f\n"
      "  return 42\n"
      "}";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangStressProcedureParameterRuntime() {
  const char* src =
      "accept : void (f : fn i32 (x : i32, y : i32)) { return }\n"
      "main : i32 () {\n"
      "  accept((x, y) { return x + y })\n"
      "  return 0\n"
      "}";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangStressProcedureSwitchExprRuntime() {
  const char* src =
      "main : i32 () {\n"
      "  f : fn i32 () = switch (1) {\n"
      "    1 == 1 => return () { return 42 }\n"
      "    default => return () { return 0 }\n"
      "  }\n"
      "  return f()\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangStressProcedureMemberCallRuntime() {
  const char* src =
      "Box :: artifact { f : fn i32 () }\n"
      "main : i32 () {\n"
      "  b : Box = { () { return 42 } }\n"
      "  return b.f()\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangStressProcedureClosureCaptureRejected() {
  const char* src =
      "main : i32 () {\n"
      "  x : i32 = 41\n"
      "  f : fn i32 () = () { return x + 1 }\n"
      "  return f()\n"
      "}";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("unknown local 'x'") != std::string::npos;
}

bool LangStressProcedureNestedClosureRejected() {
  const char* src =
      "main : i32 () {\n"
      "  outer : fn i32 () = () {\n"
      "    inner : fn i32 () = () { return 42 }\n"
      "    return inner()\n"
      "  }\n"
      "  return outer()\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("nested fn literals are not supported") != std::string::npos;
}

bool LangStressProcedureListArrayRejected() {
  const char* src =
      "main : i32 () {\n"
      "  fs : fn i32 ()[] = []\n"
      "  return 0\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("procedure types cannot have array/list dimensions") != std::string::npos;
}

bool LangStressProcedureExternBoundaryRejected() {
  const char* src =
      "extern C.call : void (f : fn i32 ())\n"
      "main : void () { return }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("extern ABI parameter type is not supported") != std::string::npos;
}

bool LangStressProcedureGenericEmissionRejected() {
  const char* src =
      "id<T> : T (x : T) { return x }\n"
      "main : i32 () {\n"
      "  f : fn i32 () = id<fn i32 ()>(() { return 42 })\n"
      "  return f()\n"
      "}";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("generic functions not supported") != std::string::npos;
}

bool LangGenericFunctionEmissionRejected() {
  const char* src =
      "id<T> : T (x : T) { return x }\n"
      "main : i32 () { return id<i32>(42) }";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("generic functions not supported") != std::string::npos;
}

bool LangGenericArtifactEmissionRejected() {
  const char* src =
      "Box<T> :: artifact { v : T }\n"
      "main : i32 () { b : Box<i32> = { 42 }; return b.v }";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("unsupported type for local 'b'") != std::string::npos;
}

bool LangGenericMethodParseRejected() {
  const char* src =
      "Box :: artifact { get<T> : T (x : T) { return x } }\n"
      "main : i32 () { return 0 }";
  Simple::Lang::Program program;
  std::string error;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  return error.find("expected ':' or '::' after member name") != std::string::npos;
}

bool LangGenericTypeArgInferenceEmissionRejected() {
  const char* src =
      "id<T> : T (x : T) { return x }\n"
      "main : i32 () { return id(42) }";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("generic functions not supported") != std::string::npos;
}

bool LangGenericSpecializationNamingRejected() {
  const char* src =
      "id<T> : T (x : T) { return x }\n"
      "main : i32 () { a : i32 = id<i32>(1); b : bool = id<bool>(true); return a }";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("generic functions not supported") != std::string::npos;
}

bool LangGenericDuplicateSpecializationRejected() {
  const char* src =
      "id<T> : T (x : T) { return x }\n"
      "main : i32 () { a : i32 = id<i32>(1); b : i32 = id<i32>(2); return a + b }";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("generic functions not supported") != std::string::npos;
}

bool LangStressProcedureArgTypeStrict() {
  const char* src =
      "main : i32 () {\n"
      "  f : fn i32 (x : i32) = (x) { return x }\n"
      "  return f(\"oops\")\n"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("call argument type mismatch") != std::string::npos;
}

bool LangStressProcedureReturnTypeStrict() {
  const char* src =
      "main : i32 () {\n"
      "  f : fn i32 (x : i32) = (x) { return true }\n"
      "  g : fn string (x : i32) = f\n"
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
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
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
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
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
      "apply : i32 (f : fn i32 (x : i32), x : i32) { return x; }"
      "main : i32 () { return apply((x) { return x + 1; }, 41); }";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
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
      "  for (i : i32 = 0; i < 10; i += 2) { skip; }"
      "  return i;"
      "}";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
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
      "  if (false) { return 0; }"
      "  else { if (true) { return 1; } else { return 2; } }"
      "}";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  const auto& stmt = program.decls[0].func.body[0];
  if (stmt.kind != Simple::Lang::StmtKind::IfStmt) return false;
  if (stmt.if_else.size() != 1) return false;
  if (stmt.if_else[0].kind != Simple::Lang::StmtKind::IfStmt) return false;
  return true;
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
      "undeclared identifier: IO");
}

bool LangSimpleBadImportUnknown() {
  int exit_code = 0;
  const std::string err = RunCommandCaptureStderr("./build/bin/svm check Tests/simple_bad/import_unknown.simple", &exit_code);
  return exit_code != 0 && err.find("import not found") != std::string::npos;
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

bool LangSimpleModuleVarAccess() {
  return RunSimpleFileExpectExit("Tests/simple/module_var_access.simple", 5);
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
      "return type mismatch");
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
      "list literal element type mismatch");
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

bool LangReservedThreadApisRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_thread.simple", 1);
}

bool LangReservedThreadUsingApisRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_thread_using.simple", 1);
}

bool LangReservedChannelI32ApisRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_i32.simple", 49);
}

bool LangReservedChannelI32UsingApisRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_i32_using.simple", 9);
}

bool LangReservedChannelScalarsRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_scalars.simple", 43);
}

bool LangReservedChannelScalarsUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_scalars_using.simple", 18);
}

bool LangReservedChannelStringRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_string.simple", 5);
}

bool LangReservedChannelStringUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_string_using.simple", 6);
}

bool LangReservedChannelTrySendRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_try_send.simple", 15);
}

bool LangReservedChannelBytesRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_bytes.simple", 9);
}

bool LangReservedChannelBytesUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_bytes_using.simple", 13);
}

bool LangReservedChannelPendingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_channel_pending.simple", 0);
}

bool LangReservedRandomRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_random.simple", 1);
}

bool LangReservedRandomUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_random_using.simple", 1);
}

bool LangReservedEnvRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_env.simple", 1);
}

bool LangReservedEnvUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_env_using.simple", 1);
}

bool LangReservedPathRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_path.simple", 1);
}

bool LangReservedPathUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_path_using.simple", 1);
}

bool LangReservedFsRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_fs.simple", 1);
}

bool LangReservedFsUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_fs_using.simple", 1);
}

bool LangReservedFsFdRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_fs_fd.simple", 1);
}

bool LangReservedJsonRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_json.simple", 0);
}

bool LangReservedJsonUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_json_using.simple", 0);
}

bool LangReservedBufferRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_buffer.simple", 0);
}

bool LangReservedBufferUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_buffer_using.simple", 0);
}

bool LangReservedLogRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_log.simple", 0);
}

bool LangReservedLogUsingRun() {
  return RunSimpleFileExpectExit("Tests/simple/reserved_log_using.simple", 1);
}

bool LangSirEmitsLocalAssign() {
  const char* src = "main : i32 () { x : i32 = 1; x = x + 2; return x; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsIfElse() {
  const char* src = "main : i32 () { x : i32 = 1; if (x == 1) { return 7; } else { return 9; } }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsWhileLoop() {
  const char* src =
      "main : i32 () { i : i32 = 0; sum : i32 = 0; while (i < 5) { sum = sum + i; i = i + 1; } return sum; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 10);
}

bool LangSirEmitsFunctionCall() {
  const char* src =
      "add : i32 (a : i32, b : i32) { return a + b; }"
      "main : i32 () { return add(20, 22); }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangSirEmitsIoPrintString() {
  const char* src =
      "import Standard.IO\n"
      "main : i32 () { Standard.IO.print(\"hi\"); return 1; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 1);
}

bool LangSirEmitsIoPrintI32() {
  const char* src =
      "import Standard.IO\n"
      "main : i32 () { Standard.IO.print(42); return 2; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 2);
}

bool LangSirEmitsIoPrintNewline() {
  const char* src =
      "import Standard.IO\n"
      "main : i32 () { Standard.IO.print(\"hello\\n\"); return 3; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsIoPrintFormat() {
  const char* src =
      "import Standard.IO\n"
      "main : i32 () { x : i32 = 7; Standard.IO.println(\"value={}\", x); return x; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsExternAbiFlatten() {
  const char* src =
      "Tex :: artifact { id : u32; width : i32; }\n"
      "RT :: artifact { id : u32; tex : Tex; }\n"
      "extern ffi.Use : void (t : RT)\n"
      "main : void () { rt : RT = { .id = 1, .tex = { .id = 2, .width = 3 } }; ffi.Use(rt); }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return sir.find("type __abi_RT") != std::string::npos;
}

bool LangSirImplicitMainReturn() {
  const char* src =
      "import Standard.IO\n"
      "main : i32 () { Standard.IO.print(\"hi\") }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 0);
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
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
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
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
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
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 9);
}

bool LangSirEmitsIndexCompoundAssign() {
  const char* src =
      "main : i32 () {"
      "  values : i32{2} = {1, 2};"
      "  values[1] += 5;"
      "  return values[1];"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsMemberCompoundAssign() {
  const char* src =
      "Point :: artifact { x : i32; y : i32 }"
      "main : i32 () {"
      "  p : Point = { 1, 2 };"
      "  p.x *= 3;"
      "  return p.x;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsIndexIncDec() {
  const char* src =
      "main : i32 () {"
      "  values : i32{1} = {1};"
      "  x : i32 = values[0]++;"
      "  y : i32 = ++values[0];"
      "  return x + y + values[0];"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
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
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsArrayLiteralIndex() {
  const char* src = "main : i32 () { values : i32{3} = {1, 2, 3}; return values[1]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 2);
}

bool LangSirEmitsArrayAssign() {
  const char* src = "main : i32 () { values : i32{2} = {1, 2}; values[1] = 7; return values[1]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsListLiteralIndex() {
  const char* src = "main : i32 () { values : i32[] = [1, 2, 3]; return values[2]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsListAssign() {
  const char* src = "main : i32 () { values : i32[] = [1, 2, 3]; values[0] = 9; return values[0]; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 9);
}

bool LangSirEmitsLen() {
  const char* src = "main : i32 () { values : i32[] = [1, 2, 3, 4]; return len(values); }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 4);
}

bool LangSirEmitsArtifactLiteralAndMember() {
  const char* src =
      "Point :: artifact { x : i32; y : i32 }"
      "main : i32 () { p : Point = { 1, 2 }; return p.x + p.y; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangSirEmitsArtifactMemberAssign() {
  const char* src =
      "Point :: artifact { x : i32; y : i32 }"
      "main : i32 () { p : Point = { 1, 2 }; p.y = 7; return p.y; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSirEmitsEnumValue() {
  const char* src =
      "Color :: enum { Red = 1, Green = 2, Blue = 3 }"
      "main : i32 () { return Color.Green; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 2);
}

bool LangSirEmitsFnLiteralCall() {
  const char* src =
      "main : i32 () {"
      "  f : fn i32 (a : i32, b : i32) = (a, b) { return a + b; };"
      "  return f(20, 22);"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangValidateSystemImportMixedCaseOk() {
  const char* src =
      "import Standard.IO\n"
      "main : void () { Standard.IO.println(1); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateSystemImportImplicitLowerAlias() {
  const char* src =
      "import Standard.IO\n"
      "using Standard.IO\n"
      "main : void () { println(1); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateProgramReturnsStructuredDiagnostic() {
  Simple::Lang::Diagnostics::Diagnostic diagnostic;
  if (Simple::Lang::ValidateProgramFromStringDiagnostic("main : i32 () { return true }", &diagnostic)) {
    return false;
  }
  return diagnostic.code == "E4001" &&
         diagnostic.phase == Simple::Lang::Diagnostics::DiagnosticPhase::TAST &&
         diagnostic.message.find("return type mismatch") != std::string::npos;
}

bool CliDiagnosticRendererClassifiesAndFormats() {
  const std::string line = Simple::CLI::RenderErrorLine(" missing input file ");
  return line == "error[E8001]: missing input file" &&
         Simple::CLI::DiagnosticHelpFor("undeclared identifier 'x'").find("declare the symbol") != std::string::npos;
}

bool DocsCanonicalPagesDescribeBehavior() {
  struct RequiredDocText {
    const char* path;
    const char* required_a;
    const char* required_b;
  } docs[] = {
      {"Docs/Language.md", "## Table of contents", "skip` is the loop-continue statement"},
      {"Docs/Language.md", "x : i32 = 1", "limit :: i32 = 10"},
      {"Docs/Language.md", "## File/module headers", "Name :: namespace { ... }"},
      {"Docs/Byte.md", "## Table of contents", "## Verifier contract"},
      {"Docs/VM.md", "## Table of contents", "## Dynamic libraries / FFI"},
      {"Docs/JIT.md", "## Table of contents", "## Correctness rule"},
      {"Docs/CLI.md", "## Table of contents", "## Input types"},
      {"Docs/Standards.md", "# Simple Project Coding Standards", "## 10. Documentation Required"},
  };
  for (const auto& doc : docs) {
    std::ifstream in(doc.path);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find(doc.required_a) == std::string::npos) return false;
    if (text.find(doc.required_b) == std::string::npos) return false;
  }
  return true;
}

bool LangDiagnosticsFormatStructuredDiagnostic() {
  Simple::Lang::Diagnostics::Diagnostic diagnostic;
  diagnostic.code = "E1234";
  diagnostic.phase = Simple::Lang::Diagnostics::DiagnosticPhase::RAST;
  diagnostic.span.line = 7;
  diagnostic.span.column = 3;
  diagnostic.message = "bad member";
  diagnostic.help = "check the import";
  const std::string formatted = Simple::Lang::Diagnostics::FormatDiagnostic(diagnostic);
  return formatted.find("E1234[rast] 7:3: bad member") != std::string::npos &&
         formatted.find("help: check the import") != std::string::npos;
}

bool LangValidateSystemOsHostFacts() {
  const char* src =
      "import System.OS\n"
      "main : void () { platform : string = System.OS.platform(); arch : string = System.OS.arch(); linux : bool = System.OS.isLinux(); macos : bool = System.OS.isMacos(); windows : bool = System.OS.isWindows(); pid : i32 = System.OS.pid(); cpus : i32 = System.OS.cpuCount(); page : i32 = System.OS.pageSize(); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectSystemOsForeignDomains() {
  const char* src =
      "import System.OS\n"
      "main : void () { count : i32 = System.OS.args_count(); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateSystemDlCapabilityConstant() {
  const char* src =
      "import System.FFI\n"
      "main : i32 () { if (System.FFI.supported) { return 1 } return 0 }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateUnknownReservedMemberSuggestsClosest() {
  const char* src =
      "import Standard.IO\n"
      "main : void () { Standard.IO.printlnn(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("did you mean 'println'") != std::string::npos;
}

bool LangValidateNativeMetadataReservedFsFdApis() {
  const char* src =
      "import System.FS\n"
      "main : void () { fd : i32 = System.FS.open(\"/tmp/missing\", 0); System.FS.close(fd); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateNativeMetadataReservedFsSuggestion() {
  const char* src =
      "import System.FS\n"
      "main : void () { fd : i32 = System.FS.opne(\"/tmp/missing\", 0); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("did you mean 'open'") != std::string::npos;
}

bool LangRejectStandardFsHandleApis() {
  const char* src =
      "import Standard.FS\n"
      "main : void () { fd : i32 = Standard.FS.open(\"/tmp/missing\", 0); Standard.FS.close(fd); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangRejectStandardPathPlatformFacts() {
  const char* src =
      "import Standard.Path\n"
      "main : void () { separator : string = Standard.Path.separator(); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateSystemPathPlatformApis() {
  const char* src =
      "import System.Path\nimport Standard.Path\n"
      "main : void () { separator : string = System.Path.separator(); delimiter : string = System.Path.delimiter(); absolute : bool = System.Path.isAbsolute(\"/tmp\"); stem : string = Standard.Path.stem(\"a.txt\"); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectStandardPathFsProbeApis() {
  const char* src =
      "import Standard.Path\n"
      "main : void () { ok : bool = Standard.Path.exists(\".\"); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangRejectSystemTimeFormattingApi() {
  const char* src =
      "import System.Time\n"
      "main : void () { text : string = System.Time.formatWallNs(0); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateStandardTimeFormattingApi() {
  const char* src =
      "import Standard.Time\n"
      "main : void () { text : string = Standard.Time.formatWallNs(0); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateSystemRandomFillBytesApi() {
  const char* src =
      "import System.Random\nimport System.Bytes\n"
      "main : void () { bytes : i32[] = System.Bytes.new(8); ok : bool = System.Random.fillBytes(bytes); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectStandardRandomFillBytesApi() {
  const char* src =
      "import Standard.Random\nimport System.Bytes\n"
      "main : void () { bytes : i32[] = System.Bytes.new(8); ok : bool = Standard.Random.fillBytes(bytes); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateRandomI64Apis() {
  const char* src =
      "import System.Random\nimport Standard.Random\n"
      "main : void () { raw : i64 = System.Random.i64(); value : i64 = Standard.Random.i64(); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectSystemRandomRangeApi() {
  const char* src =
      "import System.Random\n"
      "main : void () { value : i32 = System.Random.range(1, 3); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateStandardRandomRangeApi() {
  const char* src =
      "import Standard.Random\n"
      "main : void () { value : i32 = Standard.Random.range(1, 3); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectUnimplementedStandardJson() {
  const char* src =
      "import Standard.Json\n"
      "main : void () { value : i64 = Standard.Json.parse(\"{}\"); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateSystemJsonHandleApi() {
  const char* src =
      "import System.Json\n"
      "main : void () { value : i64 = System.Json.parse(\"{}\"); text : string = System.Json.stringify(value); ok : bool = System.Json.free(value); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectStandardBytesLowLevelApis() {
  const char* src =
      "import Standard.Bytes\n"
      "main : void () { data : i32[] = Standard.Bytes.new(4); size : i32 = Standard.Bytes.len(data); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateSplitBytesApis() {
  const char* src =
      "import System.Bytes\nimport Standard.Bytes\n"
      "main : void () { data : i32[] = Standard.Bytes.new(4); size : i32 = System.Bytes.len(data); part : i32[] = Standard.Bytes.slice(data, 0, 2); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateSystemBufferImport() {
  const char* src = "import System.Buffer\nmain : void () { b : i32[] = System.Buffer.new(4); size : i32 = System.Buffer.len(b); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectSystemLogConvenienceApis() {
  const char* src =
      "import System.Log\n"
      "main : void () { System.Log.info(\"no\"); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangRejectStandardLogRawApi() {
  const char* src =
      "import Standard.Log\n"
      "main : void () { Standard.Log.log(\"no\", 1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangRejectLegacySystemLogArgumentOrder() {
  const char* src =
      "import System.Log\n"
      "main : void () { System.Log.log(\"message\", 1); }";
  std::string error;
  return !Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateSplitLogApis() {
  const char* src =
      "import System.Log\nimport Standard.Log\n"
      "main : void () { System.Log.log(1, \"raw\"); ok : bool = System.Log.flush(); Standard.Log.info(\"hi\"); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateStandardFsProbeApis() {
  const char* src =
      "import Standard.FS\n"
      "main : void () { ok : bool = Standard.FS.exists(\".\"); file :: bool = Standard.FS.isFile(\".\"); dir :: bool = Standard.FS.isDir(\".\"); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectStandardIoBufferApis() {
  const char* src =
      "import Standard.IO\n"
      "main : i32 () { Standard.IO.buffer_new(4); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos;
}

bool LangValidateSystemBytesBufferApis() {
  const char* src =
      "import System.Bytes\n"
      "main : i32 () {\n"
      "  a : i32[] = System.Bytes.new(4);\n"
      "  b : i32[] = System.Bytes.new(4);\n"
      "  System.Bytes.writeU16LE(a, 0, 7);\n"
      "  System.Bytes.copy(b, 0, a, 0, 4);\n"
      "  return System.Bytes.len(b);\n"
      "}";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateExternCallOk() {
  const char* src =
      "extern Ray.InitWindow : void (w : i32, h : i32)\n"
      "main : i32 () { Ray.InitWindow(1, 2); return 0; }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateExternRecursiveArtifactRejected() {
  const char* src =
      "Node :: artifact { next : Node }\n"
      "extern C.walk : void (head : Node)\n";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("extern ABI") != std::string::npos;
}

bool LangValidateExternPointerCallOk() {
  const char* src =
      "Node :: artifact { next: Node* }\n"
      "extern C.walk : Node* (head : Node*)\n"
      "main : i32 () { return 0; }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidatePointerMemberAccessOk() {
  const char* src =
      "Node :: artifact { value : i32 }\n"
      "main : i32 () {"
      "  n : Node = { 1 };"
      "  p : Node* = &n;"
      "  p->value = 2;"
      "  return n.value;"
      "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidatePointerMemberRequiresPointer() {
  const char* src =
      "Node :: artifact { value : i32 }\n"
      "main : void () {"
      "  n : Node = { 1 };"
      "  n->value = 2;"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("pointer member access requires a pointer type") != std::string::npos;
}

bool LangValidatePointerToImmutableRejectsMutation() {
  const char* src =
      "Node :: artifact { value : i32 }\n"
      "main : void () {"
      "  n :: Node = { 1 };"
      "  p : Node* = &n;"
      "  p->value = 2;"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("cannot assign through immutable value") != std::string::npos;
}

bool LangValidatePointerToMutableAllowsMutation() {
  const char* src =
      "Node :: artifact { value : i32 }\n"
      "main : i32 () {"
      "  n : Node = { 1 };"
      "  p : Node* = &n;"
      "  p->value = 2;"
      "  return n.value;"
      "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateAddressOfRequiresLValue() {
  const char* src =
      "main : void () {"
      "  p : i32* = &(1 + 2);"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("address-of requires assignable expression") != std::string::npos;
}

bool LangPointerStorageEmissionRejected() {
  const char* src = "main : i32 () { x : i32 = 1; p : i32* = &x; return 0 }";
  std::string sir;
  std::string error;
  if (Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return error.find("unsupported unary operator '&'") != std::string::npos;
}

bool LangPointerDerefParseRejected() {
  const char* src = "main : i32 () { x : i32 = 1; p : i32* = &x; return *p }";
  std::string error;
  Simple::Lang::Program program;
  if (Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  return error.find("expected expression") != std::string::npos;
}

bool LangPointerNullInitRejected() {
  const char* src = "main : i32 () { p : i32* = 0; return 0 }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("initializer type mismatch") != std::string::npos;
}

bool LangPointerToRefShapesValidate() {
  const char* src =
      "Node :: artifact { v : i32 }"
      "main : i32 () {"
      "  n : Node = { 1 }; pn : Node* = &n;"
      "  values : i32[] = []; pv : i32[]* = &values;"
      "  text : string = \"x\"; ps : string* = &text;"
      "  return 0;"
      "}";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateArtifactDefaultFieldOk() {
  const char* src =
      "Point :: artifact { x : i32 = 1; y : i32 }\n"
      "main : i32 () {"
      "  p : Point = { .y = 3 };"
      "  return p.x + p.y;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 4);
}

bool LangValidateModuleDefaultFieldOk() {
  const char* src =
      "Config :: namespace { width : i32 = 10; height : i32 = 20 }\n"
      "main : i32 () { return Config.width + Config.height; }";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 30);
}

bool LangValidateSwitchMissingDefaultRejected() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 1;"
      "  return switch (x) { x == 1 => 2; };"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("exactly one default branch") != std::string::npos;
}

bool LangValidateSwitchMultipleDefaultRejected() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 1;"
      "  return switch (x) { default => 1; default => 2; };"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("exactly one default branch") != std::string::npos;
}

bool LangValidateProcReturnProcOk() {
  const char* src =
      "make_adder : fn i32 (a : i32, b : i32) () {\n"
      "  adder : fn i32 (a : i32, b : i32) = (a, b) { return a + b; };\n"
      "  return adder;\n"
      "}\n"
      "main : i32 () {\n"
      "  adder : fn i32 (a : i32, b : i32) = make_adder();\n"
      "  return adder(2, 3);\n"
      "}\n";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 5);
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
  const char* src = "Math :: namespace { } main : void () { x : i32 = Math; }";
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
  const char* src = "main : void () { x : i32 = 1; if (true) { x : i32 = 2; } }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateForLoopScope() {
  const char* src =
    "main : void () {"
    "  x : i32 = 0;"
    "  for (x : i32 = x; x < 1; x = x + 1) { x : i32 = 2; }"
    "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactDuplicateMember() {
  const char* src = "Thing :: artifact { x : i32; x : i32 }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateModuleDuplicateMember() {
  const char* src = "Math :: namespace { x : i32 = 1; x : i32 = 2; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateModuleVarNoInit() {
  const char* src =
    "Math :: namespace { x : i32; }"
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
    "  if (true) { return 1; } else { return 2; }"
    "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateNonVoidMissingPath() {
  const char* src =
    "foo : i32 () {"
    "  if (true) { return 1; }"
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
    "Math :: namespace { PI :: f64 = 3.14; }"
    "main : void () { Standard.Math.PI = 0.0; }";
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
    "Math :: namespace { add : i32 (a : i32, b : i32) { return a + b; } }"
    "main : void () { Standard.Math.add = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangLibraryCatalogCoversAllModulesAndMembers() {
  using namespace Simple::Lang;
  if (kSystemModules.size() != 23 || kStandardModules.size() != 22) return false;
  if (AllLibraryImportPaths().size() != kSystemModules.size() + kStandardModules.size()) return false;
  if (ToImportPath(SystemModule::Buffer) != "System.Buffer") return false;
  if (ToImportPath(StandardModule::Buffer) != "Standard.Buffer") return false;
  if (ToNativeModule(SystemModule::Buffer) != "System.Buffer") return false;
  if (ToCanonicalName(SystemModule::Buffer) != "System.Buffer") return false;
  if (ToCanonicalName(StandardModule::Buffer) != "Standard.Buffer") return false;
  if (ToMember(SystemBufferMember::ReadU32LE) != "readU32LE") return false;
  if (ToMember(SystemFSMember::NextDirEntry) != "nextDirEntry") return false;
  if (ToMember(SystemFFIMember::LastError) != "lastError") return false;
  if (ToMember(StandardBytesMember::ToBase64) != "toBase64") return false;
  if (ToMember(StandardBufferMember::WithCapacity) != "withCapacity") return false;
  if (MemberNames(SystemModule::Buffer).size() != 12) return false;
  if (MemberNames(StandardModule::Buffer).size() != 15) return false;
  const auto buffer_module = ParseCanonicalLibraryModule("System.Buffer");
  if (!buffer_module || buffer_module->root != LibraryRoot::System ||
      static_cast<SystemModule>(buffer_module->module_index) != SystemModule::Buffer) return false;
  const auto buffer_symbol = ParseLibrarySymbol("System.Buffer", "readU32LE");
  if (!buffer_symbol || buffer_symbol->member_name != "readU32LE") return false;
  const auto std_bytes_symbol = ParseLibrarySymbol("Standard.Bytes", "toBase64");
  if (!std_bytes_symbol || std_bytes_symbol->member_name != "toBase64") return false;
  const auto parsed_system_member = ParseMember(SystemModule::Buffer, "readU32LE");
  if (!parsed_system_member || !std::holds_alternative<SystemBufferMember>(*parsed_system_member) ||
      std::get<SystemBufferMember>(*parsed_system_member) != SystemBufferMember::ReadU32LE) return false;
  const auto parsed_standard_member = ParseMember(StandardModule::Bytes, "toBase64");
  if (!parsed_standard_member || !std::holds_alternative<StandardBytesMember>(*parsed_standard_member) ||
      std::get<StandardBytesMember>(*parsed_standard_member) != StandardBytesMember::ToBase64) return false;
  if (ParseMember(SystemModule::Channel, "tryRecvBytes") == std::nullopt) return false;
  if (ParseMember(StandardModule::HTTP, "serve") == std::nullopt) return false;
  if (ParseMember(StandardModule::Bytes, "definitelyMissing")) return false;
  if (ParseLibrarySymbol("Standard.Bytes", "definitelyMissing")) return false;
  const auto implemented_meta = GetLibraryMemberMetadata(ToLibraryModuleId(SystemModule::Buffer), "readU32LE");
  if (implemented_meta.availability != LibraryApiAvailability::Implemented ||
      implemented_meta.level != LibraryApiLevel::LowLevelSystem ||
      !implemented_meta.signature || implemented_meta.signature->params.size() != 2 ||
      implemented_meta.signature->params[0].name != "buffer" ||
      implemented_meta.signature->return_type.name != "i32") return false;
  const auto print_sig = GetLibrarySignature(ToLibraryModuleId(StandardModule::IO), "println");
  if (!print_sig || print_sig->params.size() != 1 || print_sig->params[0].type.name != "T" ||
      print_sig->return_type.name != "void" || print_sig->type_params.size() != 1) return false;
  const auto ffi_open_sig = GetLibrarySignature(ToLibraryModuleId(SystemModule::FFI), "Open");
  if (!ffi_open_sig || ffi_open_sig->params.size() != 1 ||
      ffi_open_sig->params[0].type.name != "string" || ffi_open_sig->return_type.name != "i64") return false;
  const auto planned_meta = GetLibraryMemberMetadata(ToLibraryModuleId(StandardModule::Bytes), "toBase64");
  if (planned_meta.availability != LibraryApiAvailability::Planned ||
      planned_meta.level != LibraryApiLevel::HighLevelStandard) return false;
  for (SystemModule module : kSystemModules) {
    const auto id = ToLibraryModuleId(module);
    const auto import_info = ParseLibraryImportPath(ToImportPath(module));
    if (!import_info || import_info->root != LibraryRoot::System || import_info->module_index != id.module_index) return false;
    const auto canonical = ParseCanonicalLibraryModule(ToCanonicalName(module));
    if (!canonical || !(*canonical == id)) return false;
    for (std::string_view member : MemberNames(module)) {
      const auto parsed = ParseMember(module, member);
      if (!parsed) return false;
      const std::string_view emitted = std::visit([](auto value) { return ToMember(value); }, *parsed);
      if (emitted != member) return false;
    }
  }
  for (StandardModule module : kStandardModules) {
    const auto id = ToLibraryModuleId(module);
    const auto import_info = ParseLibraryImportPath(ToImportPath(module));
    if (!import_info || import_info->root != LibraryRoot::Standard || import_info->module_index != id.module_index) return false;
    const auto canonical = ParseCanonicalLibraryModule(ToCanonicalName(module));
    if (!canonical || !(*canonical == id)) return false;
    for (std::string_view member : MemberNames(module)) {
      const auto parsed = ParseMember(module, member);
      if (!parsed) return false;
      const std::string_view emitted = std::visit([](auto value) { return ToMember(value); }, *parsed);
      if (emitted != member) return false;
    }
  }
  if (!ParseLibraryImportPath("System.Buffer")) return false;
  if (!ParseLibraryImportPath("Standard.Buffer")) return false;
  if (ParseLibraryImportPath("Buffer")) return false;
  const auto stale_fs = StaleLowercaseRuntimeModuleReplacement("System.fs");
  if (!stale_fs || *stale_fs != "System.FS") return false;
  const auto stale_ffi = StaleLowercaseRuntimeModuleReplacement("System.ffi");
  if (!stale_ffi || *stale_ffi != "System.FFI") return false;
  if (StaleLowercaseRuntimeModuleReplacement("System.FS")) return false;
  if (StaleLowercaseRuntimeModuleReplacement("Standard.fs")) return false;
  const auto replacement = LegacyReservedImportReplacementView("Buffer");
  return replacement && replacement->find("System.Buffer") != std::string_view::npos;
}

bool LangRejectLegacyReservedImports() {
  const char* src = "import IO\nmain : void () { }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("use Standard.IO") != std::string::npos;
}

bool LangValidateCanonicalSystemStandardImports() {
  const char* src =
    "import Standard.IO\n"
    "import Standard.Math\n"
    "import System.Time\n"
    "import System.FFI\n"
    "main : void () { Standard.IO.println(\"ok\"); best : i32 = Standard.Math.max(1, 2); now : i64 = System.Time.mono_ns(); ok :: bool = System.FFI.supported; }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateAllPlannedSystemStandardImports() {
  const char* src =
    "import System.IO\nimport System.FS\nimport System.Path\nimport System.Env\nimport System.OS\n"
    "import System.Time\nimport System.FFI\nimport System.ASM\nimport System.Buffer\nimport System.Bytes\n"
    "import System.Json\nimport System.Log\nimport System.Random\nimport System.Thread\nimport System.Job\n"
    "import System.Channel\nimport System.Process\nimport System.Net\nimport System.HTTP\nimport System.Terminal\n"
    "import System.Capability\nimport System.Runtime\nimport System.Debug\n"
    "import Standard.IO\nimport Standard.Console\nimport Standard.FS\nimport Standard.Path\nimport Standard.Buffer\nimport Standard.Bytes\n"
    "import Standard.Text\nimport Standard.Json\nimport Standard.Math\nimport Standard.Random\nimport Standard.Time\n"
    "import Standard.Log\nimport Standard.Process\nimport Standard.Net\nimport Standard.HTTP\nimport Standard.HTTPS\n"
    "import Standard.Terminal\nimport Standard.Promise\nimport Standard.Channel\nimport Standard.Collections\n"
    "import Standard.Result\nimport Standard.Option\n"
    "main : void () { }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangRejectSystemIoPrintln() {
  const char* src = "import System.IO\nmain : void () { System.IO.println(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos ||
         error.find("not callable") != std::string::npos ||
         error.find("undeclared identifier") != std::string::npos;
}

bool LangRejectUnplannedSystemMathImport() {
  int exit_code = 0;
  const std::string err = RunCommandCaptureStderr("./build/bin/svm check Tests/simple_bad/system_math_import.simple", &exit_code);
  return exit_code != 0 && err.find("import not found") != std::string::npos;
}

bool LangRejectUnimplementedStandardDuplicateRootMembers() {
  const char* src =
      "import Standard.Console\n"
      "import Standard.Process\n"
      "import Standard.Json\n"
      "main : void () { Standard.Console.println(1); Standard.Process.sleep_ms(1); Standard.Json.parse(\"{}\"); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("unknown module member") != std::string::npos ||
         error.find("not callable") != std::string::npos;
}

bool LangValidateNamespaceExternManifestAndCall() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "simple_ns_extern_manifest_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  if (ec) return false;

  {
    std::ofstream raylib(dir / "raylib.simple", std::ios::binary);
    raylib << "module Raylib\n"
           << "import System.FFI\n"
           << "Raylib :: namespace { extern InitWindow : void (w : i32, h : i32, title : string) }\n"
           << "lib :: i64 = System.FFI.Open(\"libraylib.so\", Raylib)\n";
  }
  {
    std::ofstream main_file(dir / "main.simple", std::ios::binary);
    main_file << "import Raylib\n"
              << "using Raylib.Raylib\n"
              << "main : void () { Raylib.Raylib.InitWindow(1, 2, \"ok\"); InitWindow(1, 2, \"ok\"); }\n";
  }

  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::RAST::LoadProgramWithImports(dir / "main.simple", &program, &error)) return false;
  return Simple::Lang::ValidateProgram(program, &error);
}

bool LangValidateAssignToArtifactMethodFail() {
  const char* src =
    "Point :: artifact { x : i32 get : i32 () { return x; } }"
    "main : void () { p : Point = { 1 }; p.get = 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateProcValueRejectsArtifactMethod() {
  const char* src =
    "Point :: artifact { x : i32; get : i32 () { return self.x; } }"
    "main : void () { p : Point = { 1 }; f : fn i32 () = p.get; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("initializer type mismatch") != std::string::npos;
}

bool LangValidateProcValueRejectsModuleFunction() {
  const char* src =
    "Math :: namespace { add : i32 (a : i32, b : i32) { return a + b; } }"
    "main : void () { f : fn i32 (i32, i32) = Math.add; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("initializer type mismatch") != std::string::npos;
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
    "Math :: namespace { x : i32 = 1; }"
    "main : i32 () { return Standard.Math.y; }";
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
    "Point :: artifact { x : i32; y : i32 }"
    "main : void () { p : Point = { .z = 1 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralPositionalThenNamedDuplicate() {
  const char* src =
    "Point :: artifact { x : i32; y : i32 }"
    "main : void () { p : Point = { 1, .x = 2 }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralNamedOk() {
  const char* src =
    "Point :: artifact { x : i32; y : i32 }"
    "main : void () { p : Point = { .x = 1, .y = 2 }; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralTypeMismatchPositional() {
  const char* src =
    "Point :: artifact { x : i32; y : i32 }"
    "main : void () { p : Point = { 1, true }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactLiteralTypeMismatchNamed() {
  const char* src =
    "Point :: artifact { x : i32; y : i32 }"
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
    "Math :: namespace { add : i32 (a : i32, b : i32) { return a; } }"
    "main : i32 () { return Standard.Math.add(1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallModuleVar() {
  const char* src =
    "Math :: namespace { PI :: f64 = 3.14; }"
    "main : i32 () { return Standard.Math.PI(1); }";
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
  const char* src = "import Standard.IO\nmain : void () { Standard.IO.print(); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIoPrintTypeArgsOk() {
  const char* src = "import Standard.IO\nmain : void () { Standard.IO.print<i32>(1); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIoPrintRejectsArray() {
  const char* src = "import Standard.IO\nmain : void () { a : i32[] = [1,2]; Standard.IO.print(a); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIoPrintFormatOk() {
  const char* src = "import Standard.IO\nmain : void () { x : i32 = 42; Standard.IO.println(\"x={}\", x); }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateIoPrintFormatPlaceholderMismatch() {
  const char* src = "import Standard.IO\nmain : void () { Standard.IO.println(\"x={}, y={}\", 1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("format placeholder count mismatch") != std::string::npos;
}

bool LangValidateIoPrintFormatNeedsStringLiteral() {
  const char* src = "import Standard.IO\nmain : void () { fmt : string = \"x={}\"; Standard.IO.println(fmt, 1); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("format call expects string literal") != std::string::npos;
}

bool LangValidateFormatExprOk() {
  const char* src = "main : string () { x : i32 = 42; return \"x={}\", x; }";
  std::string error;
  return Simple::Lang::ValidateProgramFromString(src, &error);
}

bool LangValidateFormatExprPlaceholderMismatch() {
  const char* src = "main : string () { return \"x={} y={}\", 1; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("format placeholder count mismatch") != std::string::npos;
}

bool LangValidateFormatExprRejectsList() {
  const char* src = "main : string () { a : i32[] = [1,2]; return \"{}\", a; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("format expects scalar") != std::string::npos ||
         error.find("format supports numeric") != std::string::npos;
}

bool LangRunsSimpleFixtures() {
  const std::string dir = "Tests/simple";
  return Simple::VM::Tests::RunSimplePerfDir(dir, 1, true) == 0;
}

bool LangValidateCallFnLiteralCount() {
  const char* src =
    "main : i32 () { f : fn i32 (x : i32) = (x) { return x; }; return f(1, 2); }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateCallFnLiteralOk() {
  const char* src =
    "main : i32 () { f : fn i32 (x : i32) = (x) { return x; }; return f(1); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArtifactMemberRequiresSelfField() {
  const char* src =
    "Point :: artifact { x : i32; get : i32 () { return x; } }";
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
    "Point :: artifact { x : i32; get : i32 () { return self.x; } use : i32 () { return self.get(); } }";
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
  const char* src = "main : void () { f : fn i32 (a : i32) = (a) { return a; }; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateFnLiteralAssignTypeMismatch() {
  const char* src = "main : void () { f : fn i32 (a : i32, b : i32) = (a) { return a; }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateFnLiteralAssignNotProcType() {
  const char* src = "main : void () { f : i32 = (a) { return a; }; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateFnShorthandAssignAndCallOk() {
  const char* src =
      "Player :: artifact { position : i32; velocity : i32 }\n"
      "main : i32 () {\n"
      "  update : fn void (p : Player) = (p) { p.position += p.velocity }\n"
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
      "  f : fn i32 (a : i32, b : i32) = (a, b) { return a + b }\n"
      "  return f(20, 22)\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}

bool LangValidateFnParamWithFnArgOk() {
  const char* src =
      "invoke : void (cb : fn void (x : i32), x : i32) { cb(x) }\n"
      "main : i32 () {\n"
      "  printv : fn void (v : i32) = (v) { Standard.IO.println(v) }\n"
      "  invoke(printv, 42)\n"
      "  return 0\n"
      "}";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangSirEmitsFnParamWithFnArgCall() {
  const char* src =
      "invoke : void (cb : fn void (x : i32), x : i32) { cb(x) }\n"
      "main : i32 () {\n"
      "  noop : fn void (v : i32) = (v) { return; }\n"
      "  invoke(noop, 7)\n"
      "  return 0\n"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 0);
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
  const char* src = "main : void () { arr : i32{2} = {1,2}; x : i32 = arr[0]; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexTypeMismatch() {
  const char* src = "main : void () { arr : i32{2} = {1,2}; x : f64 = arr[0]; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIndexNestedArrayTypeOk() {
  const char* src = "main : void () { arr : i32{2}{2} = {{1,2},{3,4}}; row : i32{2} = arr[0]; }";
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
  const char* src = "Math :: namespace { pi : i32 = 3; } main : void () { x : Math = 1; }";
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
  const char* src = "main : void () { a : i32{2}{2} = {{1,2},{3,4}}; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralShapeMismatch() {
  const char* src = "main : void () { a : i32{2} = {1,2,3}; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralNestedMismatch() {
  const char* src = "main : void () { a : i32{2}{2} = {{1,2,3},{4,5,6}}; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralNonArrayChild() {
  const char* src = "main : void () { a : i32{2}{2} = {1,2}; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralEmptyMismatch() {
  const char* src = "main : void () { a : i32{2} = {}; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralElementMismatch() {
  const char* src = "main : void () { a : i32{2} = {1, true}; }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateArrayLiteralNestedElementMismatch() {
  const char* src = "main : void () { a : i32{2}{2} = {{1,2},{3,4}}; b : i32{2}{2} = {{1,2},{3,true}}; }";
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

bool LangValidateArtifactArrayLiteralOk() {
  const char* src =
      "Bullet :: artifact { x : i32 } "
      "main : void () { bullets : Bullet{2} = {{1}, {2}}; }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateSwitchAssignRequiresReturn() {
  const char* src =
      "main : void () {"
      "  x : i32 = 1;"
      "  y : i32 = switch (x) { x == 1 => 10; default => 20 };"
      "}";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return error.find("assigning switch branches must use 'return'") != std::string::npos;
}

bool LangSwitchAssignRuntime() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 1;"
      "  y : i32 = switch (x) { x == 1 => return 10; default => return 20 };"
      "  return y;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 10);
}

bool LangSwitchExprRuntime() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 2;"
      "  return switch (x) { x == 1 => 5; default => 7 };"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 7);
}

bool LangSwitchBlockBranchRuntime() {
  const char* src =
      "main : i32 () {"
      "  x : i32 = 0;"
      "  y : i32 = switch (x) { x == 0 => { return 3 }; default => return 9 };"
      "  return y;"
      "}";
  std::string sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 3);
}

bool LangValidateIfConditionTypeMismatch() {
  const char* src = "main : void () { if (1) { return; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateIfChainConditionTypeMismatch() {
  const char* src = "main : void () { |> (1) { return; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateWhileConditionTypeMismatch() {
  const char* src = "main : void () { while (1) { break; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateForConditionTypeMismatch() {
  const char* src = "main : void () { for (i : i32 = 0; 1; i = i + 1) { break; } }";
  std::string error;
  if (Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateLenArrayOk() {
  const char* src = "main : i32 () { a : i32{3} = {1,2,3}; return len(a); }";
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
  const char* src = "main : string () { x : i32 = 1; return @string(x); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateStrFromBoolOk() {
  const char* src = "main : string () { return @string(true); }";
  std::string error;
  if (!Simple::Lang::ValidateProgramFromString(src, &error)) return false;
  return true;
}

bool LangValidateStrFromStringFail() {
  const char* src = "main : string () { s : string = \"hi\"; return @string(s); }";
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

const TestCase kLangTests[] = {
  {"lang_validate_system_import_mixed_case_ok", LangValidateSystemImportMixedCaseOk},
  {"lang_validate_system_import_implicit_lower_alias", LangValidateSystemImportImplicitLowerAlias},
  {"docs_canonical_pages_describe_behavior", DocsCanonicalPagesDescribeBehavior},
  {"cli_diagnostic_renderer_classifies_and_formats", CliDiagnosticRendererClassifiesAndFormats},
  {"lang_validate_program_returns_structured_diagnostic", LangValidateProgramReturnsStructuredDiagnostic},
  {"lang_diagnostics_format_structured_diagnostic", LangDiagnosticsFormatStructuredDiagnostic},
  {"lang_validate_system_os_host_facts", LangValidateSystemOsHostFacts},
  {"lang_reject_system_os_foreign_domains", LangRejectSystemOsForeignDomains},
  {"lang_validate_system_dl_capability_constant", LangValidateSystemDlCapabilityConstant},
  {"lang_validate_unknown_reserved_member_suggests_closest", LangValidateUnknownReservedMemberSuggestsClosest},
  {"lang_validate_native_metadata_reserved_fs_fd_apis", LangValidateNativeMetadataReservedFsFdApis},
  {"lang_validate_native_metadata_reserved_fs_suggestion", LangValidateNativeMetadataReservedFsSuggestion},
  {"lang_reject_standard_fs_handle_apis", LangRejectStandardFsHandleApis},
  {"lang_reject_standard_path_platform_facts", LangRejectStandardPathPlatformFacts},
  {"lang_validate_system_path_platform_apis", LangValidateSystemPathPlatformApis},
  {"lang_reject_standard_path_fs_probe_apis", LangRejectStandardPathFsProbeApis},
  {"lang_reject_system_time_formatting_api", LangRejectSystemTimeFormattingApi},
  {"lang_validate_standard_time_formatting_api", LangValidateStandardTimeFormattingApi},
  {"lang_validate_system_random_fill_bytes_api", LangValidateSystemRandomFillBytesApi},
  {"lang_reject_standard_random_fill_bytes_api", LangRejectStandardRandomFillBytesApi},
  {"lang_validate_random_i64_apis", LangValidateRandomI64Apis},
  {"lang_reject_system_random_range_api", LangRejectSystemRandomRangeApi},
  {"lang_validate_standard_random_range_api", LangValidateStandardRandomRangeApi},
  {"lang_reject_unimplemented_standard_json", LangRejectUnimplementedStandardJson},
  {"lang_validate_system_json_handle_api", LangValidateSystemJsonHandleApi},
  {"lang_reject_standard_bytes_low_level_apis", LangRejectStandardBytesLowLevelApis},
  {"lang_validate_split_bytes_apis", LangValidateSplitBytesApis},
  {"lang_validate_system_buffer_import", LangValidateSystemBufferImport},
  {"lang_reject_system_log_convenience_apis", LangRejectSystemLogConvenienceApis},
  {"lang_reject_standard_log_raw_api", LangRejectStandardLogRawApi},
  {"lang_reject_legacy_system_log_argument_order", LangRejectLegacySystemLogArgumentOrder},
  {"lang_validate_split_log_apis", LangValidateSplitLogApis},
  {"lang_validate_standard_fs_probe_apis", LangValidateStandardFsProbeApis},
  {"lang_reject_standard_io_buffer_apis", LangRejectStandardIoBufferApis},
  {"lang_validate_system_bytes_buffer_apis", LangValidateSystemBytesBufferApis},
  {"lang_validate_extern_call_ok", LangValidateExternCallOk},
  {"lang_validate_extern_recursive_artifact_rejected", LangValidateExternRecursiveArtifactRejected},
  {"lang_validate_extern_pointer_call_ok", LangValidateExternPointerCallOk},
  {"lang_validate_pointer_member_access_ok", LangValidatePointerMemberAccessOk},
  {"lang_validate_pointer_member_requires_pointer", LangValidatePointerMemberRequiresPointer},
  {"lang_validate_pointer_to_immutable_rejects_mutation", LangValidatePointerToImmutableRejectsMutation},
  {"lang_validate_pointer_to_mutable_allows_mutation", LangValidatePointerToMutableAllowsMutation},
  {"lang_pointer_storage_emission_rejected", LangPointerStorageEmissionRejected},
  {"lang_pointer_deref_parse_rejected", LangPointerDerefParseRejected},
  {"lang_pointer_null_init_rejected", LangPointerNullInitRejected},
  {"lang_pointer_to_ref_shapes_validate", LangPointerToRefShapesValidate},
  {"lang_validate_address_of_requires_lvalue", LangValidateAddressOfRequiresLValue},
  {"lang_validate_artifact_default_field_ok", LangValidateArtifactDefaultFieldOk},
  {"lang_validate_module_default_field_ok", LangValidateModuleDefaultFieldOk},
  {"lang_validate_switch_missing_default_rejected", LangValidateSwitchMissingDefaultRejected},
  {"lang_validate_switch_multiple_default_rejected", LangValidateSwitchMultipleDefaultRejected},
  {"lang_validate_proc_return_proc_ok", LangValidateProcReturnProcOk},
  {"lang_sir_emit_return_i32", LangSirEmitsReturnI32},
  {"lang_ire_serializes_precomputed_sir_lines", LangIreSerializesPrecomputedSirLines},
  {"lang_phase_headers_compile_and_preserve_behavior", LangPhaseHeadersCompileAndPreserveBehavior},
  {"lang_nested_artifact_method_switch_if_chain_runtime", LangNestedArtifactMethodSwitchIfChainRuntime},
  {"lang_nested_artifact_method_switch_if_chain_bad_condition", LangNestedArtifactMethodSwitchIfChainBadCondition},
  {"lang_nested_switch_branch_block_local_runtime", LangNestedSwitchBranchBlockLocalRuntime},
  {"lang_nested_switch_branch_preserves_loop_context_runtime", LangNestedSwitchBranchPreservesLoopContextRuntime},
  {"lang_nested_switch_branch_sibling_locals_runtime", LangNestedSwitchBranchSiblingLocalsRuntime},
  {"lang_nested_if_sibling_locals_runtime", LangNestedIfSiblingLocalsRuntime},
  {"lang_conditional_return_main_implicit_fallback_runtime", LangConditionalReturnMainImplicitFallbackRuntime},
  {"lang_if_chain_all_branches_return_no_fallback_runtime", LangIfChainAllBranchesReturnNoFallbackRuntime},
  {"lang_switch_expr_stmt_branch_local_runtime", LangSwitchExprStmtBranchLocalRuntime},
  {"lang_sir_top_level_script_executes", LangSirTopLevelScriptExecutes},
  {"lang_sir_main_overrides_top_level", LangSirMainOverridesTopLevel},
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
  {"lang_sir_emit_extern_abi_flatten", LangSirEmitsExternAbiFlatten},
  {"lang_sir_implicit_main_return", LangSirImplicitMainReturn},
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
  {"lang_simple_fixture_string_escape", LangSimpleFixtureStringEscape},
  {"lang_simple_fixture_string_equality", LangSimpleFixtureStringEquality},
  {"lang_simple_fixture_semicolons_same_line", LangSimpleFixtureSemicolonsSameLine},
  {"lang_simple_fixture_main_implicit_return", LangSimpleFixtureMainImplicitReturn},
  {"lang_simple_fixture_cast_i8_to_i32", LangSimpleFixtureCastI8ToI32},
  {"lang_simple_fixture_stress_lang_features", LangSimpleFixtureStressLangFeatures},
  {"lang_simple_fixture_stress_raylib_like", LangSimpleFixtureStressRaylibLike},
  {"lang_simple_fixture_module_multi", LangSimpleFixtureModuleMulti},
  {"lang_simple_fixture_module_func_params", LangSimpleFixtureModuleFuncParams},
  {"lang_simple_fixture_import_basic", LangSimpleFixtureImportBasic},
  {"lang_simple_fixture_extern_decl", LangSimpleFixtureExternDecl},
  {"lang_simple_fixture_extern_System_os_args_count", LangSimpleFixtureExternCoreOsArgsCount},
  {"lang_simple_fixture_System_dl_open", LangSimpleFixtureCoreDlOpen},
  {"lang_simple_fixture_System_dl_open_global", LangSimpleFixtureCoreDlOpenGlobal},
  {"lang_simple_fixture_float_literal_context", LangSimpleFixtureFloatLiteralContext},
  {"lang_simple_fixture_reserved_math", LangSimpleFixtureReservedMath},
  {"lang_simple_fixture_reserved_math_pi", LangSimpleFixtureReservedMathPi},
  {"lang_simple_fixture_reserved_time", LangSimpleFixtureReservedTime},
  {"lang_simple_fixture_reserved_io_buffer", LangSimpleFixtureReservedIoBuffer},
  {"lang_simple_fixture_reserved_file", LangSimpleFixtureReservedFile},
  {"lang_gc_ref_tracing_stress", LangGcRefTracingStress},
  {"lang_stress_enum_as_type_runtime", LangStressEnumAsTypeRuntime},
  {"lang_stress_enum_as_type_reject_scalar_assignment", LangStressEnumAsTypeRejectScalarAssignment},
  {"lang_stress_artifact_method_mutation_runtime", LangStressArtifactMethodMutationRuntime},
  {"lang_stress_artifact_method_type_strict", LangStressArtifactMethodTypeStrict},
  {"lang_stress_procedure_variable_runtime", LangStressProcedureVariableRuntime},
  {"lang_stress_procedure_parameter_runtime", LangStressProcedureParameterRuntime},
  {"lang_stress_procedure_switch_expr_runtime", LangStressProcedureSwitchExprRuntime},
  {"lang_stress_procedure_member_call_runtime", LangStressProcedureMemberCallRuntime},
  {"lang_stress_procedure_closure_capture_rejected", LangStressProcedureClosureCaptureRejected},
  {"lang_stress_procedure_nested_closure_rejected", LangStressProcedureNestedClosureRejected},
  {"lang_stress_procedure_list_array_rejected", LangStressProcedureListArrayRejected},
  {"lang_stress_procedure_extern_boundary_rejected", LangStressProcedureExternBoundaryRejected},
  {"lang_stress_procedure_generic_emission_rejected", LangStressProcedureGenericEmissionRejected},
  {"lang_generic_function_emission_rejected", LangGenericFunctionEmissionRejected},
  {"lang_generic_artifact_emission_rejected", LangGenericArtifactEmissionRejected},
  {"lang_generic_method_parse_rejected", LangGenericMethodParseRejected},
  {"lang_generic_type_arg_inference_emission_rejected", LangGenericTypeArgInferenceEmissionRejected},
  {"lang_generic_specialization_naming_rejected", LangGenericSpecializationNamingRejected},
  {"lang_generic_duplicate_specialization_rejected", LangGenericDuplicateSpecializationRejected},
  {"lang_stress_procedure_arg_type_strict", LangStressProcedureArgTypeStrict},
  {"lang_stress_procedure_return_type_strict", LangStressProcedureReturnTypeStrict},
  {"lang_stress_enum_artifact_procedure_composition_runtime", LangStressEnumArtifactProcedureCompositionRuntime},
  {"lang_stress_type_explicit_artifact_field_fail", LangStressTypeExplicitArtifactFieldFail},
  {"lang_stress_parse_call_member_index_precedence", LangStressParseCallMemberIndexPrecedence},
  {"lang_stress_parse_fn_literal_call_in_call_arg", LangStressParseFnLiteralCallInCallArg},
  {"lang_stress_parse_for_loop_complex_step", LangStressParseForLoopComplexStep},
  {"lang_stress_parse_nested_if_else_in_else_branch", LangStressParseNestedIfElseInElseBranch},
  {"lang_simple_bad_missing_return", LangSimpleBadMissingReturn},
  {"lang_simple_bad_type_mismatch", LangSimpleBadTypeMismatch},
  {"lang_simple_bad_print_array", LangSimpleBadPrintArray},
  {"lang_simple_bad_import_unknown", LangSimpleBadImportUnknown},
  {"lang_simple_bad_enum_unqualified", LangSimpleBadEnumUnqualified},
  {"lang_simple_bad_enum_unknown_member", LangSimpleBadEnumUnknownMember},
  {"lang_simple_bad_break_outside_loop", LangSimpleBadBreakOutsideLoop},
  {"lang_simple_module_var_access", LangSimpleModuleVarAccess},
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
  {"lang_reserved_thread_apis_run", LangReservedThreadApisRun},
  {"lang_reserved_thread_using_apis_run", LangReservedThreadUsingApisRun},
  {"lang_reserved_channel_i32_apis_run", LangReservedChannelI32ApisRun},
  {"lang_reserved_channel_i32_using_apis_run", LangReservedChannelI32UsingApisRun},
  {"lang_reserved_channel_scalars_run", LangReservedChannelScalarsRun},
  {"lang_reserved_channel_scalars_using_run", LangReservedChannelScalarsUsingRun},
  {"lang_reserved_channel_string_run", LangReservedChannelStringRun},
  {"lang_reserved_channel_string_using_run", LangReservedChannelStringUsingRun},
  {"lang_reserved_channel_try_send_run", LangReservedChannelTrySendRun},
  {"lang_reserved_channel_bytes_run", LangReservedChannelBytesRun},
  {"lang_reserved_channel_bytes_using_run", LangReservedChannelBytesUsingRun},
  {"lang_reserved_channel_pending_run", LangReservedChannelPendingRun},
  {"lang_reserved_random_run", LangReservedRandomRun},
  {"lang_reserved_random_using_run", LangReservedRandomUsingRun},
  {"lang_reserved_env_run", LangReservedEnvRun},
  {"lang_reserved_env_using_run", LangReservedEnvUsingRun},
  {"lang_reserved_path_run", LangReservedPathRun},
  {"lang_reserved_path_using_run", LangReservedPathUsingRun},
  {"lang_reserved_fs_run", LangReservedFsRun},
  {"lang_reserved_fs_using_run", LangReservedFsUsingRun},
  {"lang_reserved_fs_fd_run", LangReservedFsFdRun},
  {"lang_reserved_json_run", LangReservedJsonRun},
  {"lang_reserved_json_using_run", LangReservedJsonUsingRun},
  {"lang_reserved_buffer_run", LangReservedBufferRun},
  {"lang_reserved_buffer_using_run", LangReservedBufferUsingRun},
  {"lang_reserved_log_run", LangReservedLogRun},
  {"lang_reserved_log_using_run", LangReservedLogUsingRun},
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
  {"lang_sir_emit_fn_param_fn_arg_call", LangSirEmitsFnParamWithFnArgCall},
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
  {"lang_library_catalog_covers_all_modules_and_members", LangLibraryCatalogCoversAllModulesAndMembers},
  {"lang_reject_legacy_reserved_imports", LangRejectLegacyReservedImports},
  {"lang_validate_canonical_system_standard_imports", LangValidateCanonicalSystemStandardImports},
  {"lang_validate_all_planned_system_standard_imports", LangValidateAllPlannedSystemStandardImports},
  {"lang_reject_system_io_println", LangRejectSystemIoPrintln},
  {"lang_reject_unplanned_system_math_import", LangRejectUnplannedSystemMathImport},
  {"lang_reject_unimplemented_standard_duplicate_root_members", LangRejectUnimplementedStandardDuplicateRootMembers},
  {"lang_validate_namespace_extern_manifest_and_call", LangValidateNamespaceExternManifestAndCall},
  {"lang_validate_assign_to_artifact_method_fail", LangValidateAssignToArtifactMethodFail},
  {"lang_validate_proc_value_rejects_artifact_method", LangValidateProcValueRejectsArtifactMethod},
  {"lang_validate_proc_value_rejects_module_function", LangValidateProcValueRejectsModuleFunction},
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
  {"lang_validate_format_expr_ok", LangValidateFormatExprOk},
  {"lang_validate_format_expr_placeholder_mismatch", LangValidateFormatExprPlaceholderMismatch},
  {"lang_validate_format_expr_rejects_list", LangValidateFormatExprRejectsList},
  {"lang_run_simple_fixtures", LangRunsSimpleFixtures},
  {"lang_validate_call_fn_literal_count", LangValidateCallFnLiteralCount},
  {"lang_validate_call_fn_literal_ok", LangValidateCallFnLiteralOk},
  {"lang_validate_fn_shorthand_assign_call_ok", LangValidateFnShorthandAssignAndCallOk},
  {"lang_validate_fn_param_fn_arg_ok", LangValidateFnParamWithFnArgOk},
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
  {"lang_validate_artifact_array_literal_ok", LangValidateArtifactArrayLiteralOk},
  {"lang_validate_switch_assign_requires_return", LangValidateSwitchAssignRequiresReturn},
  {"lang_switch_assign_runtime", LangSwitchAssignRuntime},
  {"lang_switch_expr_runtime", LangSwitchExprRuntime},
  {"lang_switch_block_branch_runtime", LangSwitchBlockBranchRuntime},
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
