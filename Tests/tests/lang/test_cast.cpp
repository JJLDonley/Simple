#include "test_utils.h"

#include "CAST/cast.h"
#include "CAST/parser.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitCastParsesFunctionDecl() {
  Simple::Lang::CAST::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : i32 () { return 7; }", &program, &error)) {
    return false;
  }
  if (program.decls.size() != 1) return false;
  const auto& decl = program.decls[0];
  return decl.kind == Simple::Lang::DeclKind::Function &&
         decl.func.name == "main" &&
         decl.func.return_type.name == "i32" &&
         decl.func.body.size() == 1 &&
         decl.func.body[0].kind == Simple::Lang::StmtKind::Return;
}

const TestCase kLangCastTests[] = {
  {"lang_split_cast_parses_function_decl", LangSplitCastParsesFunctionDecl},
};

const TestSection kLangCastSections[] = {
  {"lang_cast", kLangCastTests, sizeof(kLangCastTests) / sizeof(kLangCastTests[0])},
};

} // namespace

const TestSection* GetLangCastSections(size_t* count) {
  if (count) *count = sizeof(kLangCastSections) / sizeof(kLangCastSections[0]);
  return kLangCastSections;
}

} // namespace Simple::VM::Tests
