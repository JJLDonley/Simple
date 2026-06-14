#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "AST/lower_cast.h"
#include "CAST/cast.h"
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

bool LangAstLegacyHeaderRemoved() {
  const std::string legacy_header = std::string("lang_") + "ast.h";
  if (std::filesystem::exists(std::filesystem::path("Lang/include") / legacy_header)) return false;
  const std::filesystem::path root = ".";
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    const auto path = entry.path();
    const std::string path_text = path.string();
    if (path_text.find("./build/") == 0 || path_text.find("build/") == 0 ||
        path_text.find("./.git/") == 0 || path_text.find(".git/") == 0) {
      continue;
    }
    const std::string ext = path.extension().string();
    if (ext != ".h" && ext != ".cpp") continue;
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (buffer.str().find(legacy_header) != std::string::npos) return false;
  }
  return true;
}

const TestCase kLangAstTests[] = {
  {"lang_split_ast_lowers_cast_program", LangSplitAstLowersCastProgram},
  {"lang_ast_legacy_header_removed", LangAstLegacyHeaderRemoved},
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
