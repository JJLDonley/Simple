#include "test_utils.h"

#include "AST/lower_cast.h"
#include "CAST/parser.h"
#include "IRB/ir_builder.h"
#include "IRE/sir_emitter.h"
#include "RAST/resolver.h"
#include "IRE/sir_emitter.h"
#include "TAST/type_checker.h"

namespace Simple::VM::Tests {
namespace {

bool LangDlImportsEmitSystemNamespace() {
  std::string sir;
  std::string error;
  const char* source = R"simple(
import DL

main :: i32 () {
  h : i64 = DL.open("libc.so.6")
  return 0
}
)simple";
  if (!Simple::Lang::IRE::EmitSirFromString(source, &sir, &error)) return false;
  return sir.find(" System.dl open ") != std::string::npos &&
         sir.find("core.dl") == std::string::npos &&
         sir.find("import core.") == std::string::npos;
}

bool LangSplitIreEmitsSirModule() {
  Simple::Lang::CAST::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string sir;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : i32 () { return 4; }", &cast_program, &error)) {
    return false;
  }
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  if (!Simple::Lang::IRE::EmitSirModule(module, &sir, &error)) return false;
  return sir.find("func main") != std::string::npos &&
         sir.find("const.i32 4") != std::string::npos;
}

const TestCase kLangIreTests[] = {
  {"lang_dl_imports_emit_system_namespace", LangDlImportsEmitSystemNamespace},
  {"lang_split_ire_emits_sir_module", LangSplitIreEmitsSirModule},
};

const TestSection kLangIreSections[] = {
  {"lang_ire", kLangIreTests, sizeof(kLangIreTests) / sizeof(kLangIreTests[0])},
};

} // namespace

const TestSection* GetLangIreSections(size_t* count) {
  if (count) *count = sizeof(kLangIreSections) / sizeof(kLangIreSections[0]);
  return kLangIreSections;
}

} // namespace Simple::VM::Tests
