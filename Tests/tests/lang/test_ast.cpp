#include "test_utils.h"

#include "AST/lower_cast.h"
#include "CAST/parser.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitAstLowersCastProgram() {
  Simple::Lang::CAST::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : i32 () { x : i32 = 1; return x; }", &cast_program, &error)) {
    return false;
  }
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (ast_program.decls.size() != 1) return false;
  const auto& decl = ast_program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Function) return false;
  return decl.func.body.size() == 2 &&
         decl.func.body[0].kind == Simple::Lang::StmtKind::VarDecl &&
         decl.func.body[0].var_decl.name == "x" &&
         decl.func.body[1].kind == Simple::Lang::StmtKind::Return;
}


const TestCase kLangAstTests[] = {
  {"lang_split_ast_lowers_cast_program", LangSplitAstLowersCastProgram},
};

const TestSection kLangAstSections[] = {
  {"lang_ast", kLangAstTests, sizeof(kLangAstTests) / sizeof(kLangAstTests[0])},
};

} // namespace

const TestSection* GetLangAstSections(size_t* count) {
  if (count) *count = sizeof(kLangAstSections) / sizeof(kLangAstSections[0]);
  return kLangAstSections;
}

} // namespace Simple::VM::Tests
