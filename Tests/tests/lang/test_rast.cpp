#include "test_utils.h"

#include "AST/lower_cast.h"
#include "CAST/cast.h"
#include "CAST/parser.h"
#include "RAST/resolver.h"
#include "RAST/symbol_table.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitRastResolvesFunctionSymbol() {
  Simple::Lang::CAST::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : i32 () { return 1; }", &cast_program, &error)) {
    return false;
  }
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  const auto* symbol = Simple::Lang::RAST::LookupQualifiedSymbol(&resolved, "main");
  return symbol && symbol->kind == Simple::Lang::RAST::SymbolKind::Function;
}

const TestCase kLangRastTests[] = {
  {"lang_split_rast_resolves_function_symbol", LangSplitRastResolvesFunctionSymbol},
};

const TestSection kLangRastSections[] = {
  {"lang_rast", kLangRastTests, sizeof(kLangRastTests) / sizeof(kLangRastTests[0])},
};

} // namespace

const TestSection* GetLangRastSections(size_t* count) {
  if (count) *count = sizeof(kLangRastSections) / sizeof(kLangRastSections[0]);
  return kLangRastSections;
}

} // namespace Simple::VM::Tests
