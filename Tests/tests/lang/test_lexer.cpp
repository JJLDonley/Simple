#include "test_utils.h"

#include "Lexer/lexer.h"
#include "Lexer/token.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitLexerTokenizesBasicProgram() {
  Simple::Lang::Lexer lexer("main : i32 () { return 1; }");
  if (!lexer.Lex()) return false;
  const auto& tokens = lexer.Tokens();
  bool saw_main = false;
  bool saw_return = false;
  bool saw_integer = false;
  for (const auto& token : tokens) {
    if (token.kind == Simple::Lang::TokenKind::Identifier && token.text == "main") saw_main = true;
    if (token.kind == Simple::Lang::TokenKind::KwReturn) saw_return = true;
    if (token.kind == Simple::Lang::TokenKind::Integer && token.text == "1") saw_integer = true;
  }
  return saw_main && saw_return && saw_integer;
}

const TestCase kLangLexerTests[] = {
  {"lang_split_lexer_tokenizes_basic_program", LangSplitLexerTokenizesBasicProgram},
};

const TestSection kLangLexerSections[] = {
  {"lang_lexer", kLangLexerTests, sizeof(kLangLexerTests) / sizeof(kLangLexerTests[0])},
};

} // namespace

const TestSection* GetLangLexerSections(size_t* count) {
  if (count) *count = sizeof(kLangLexerSections) / sizeof(kLangLexerSections[0]);
  return kLangLexerSections;
}

} // namespace Simple::VM::Tests
