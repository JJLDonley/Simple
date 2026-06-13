#include "test_utils.h"

#include "AST/lower_cast.h"
#include "CAST/cast.h"
#include "CAST/parser.h"
#include "IRB/ir_builder.h"
#include "RAST/resolver.h"
#include "TAST/type_checker.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitIrbBuildsModule() {
  Simple::Lang::CAST::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : i32 () { return 3; }", &cast_program, &error)) {
    return false;
  }
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  return !module.sir_text.empty() && !module.ir.functions.empty() && module.ir.functions[0].name == "main";
}

const TestCase kLangIrbTests[] = {
  {"lang_split_irb_builds_module", LangSplitIrbBuildsModule},
};

const TestSection kLangIrbSections[] = {
  {"lang_irb", kLangIrbTests, sizeof(kLangIrbTests) / sizeof(kLangIrbTests[0])},
};

} // namespace

const TestSection* GetLangIrbSections(size_t* count) {
  if (count) *count = sizeof(kLangIrbSections) / sizeof(kLangIrbSections[0]);
  return kLangIrbSections;
}

} // namespace Simple::VM::Tests
