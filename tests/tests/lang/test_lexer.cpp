#include "test_utils.h"

#include "Lexer/lexer.h"
#include "Lexer/token.h"

namespace Simple::VM::Tests {
namespace {

bool ExpectTokenKinds(const std::vector<Simple::Lang::Token>& tokens,
                      const std::vector<Simple::Lang::TokenKind>& kinds) {
  if (tokens.size() < kinds.size()) return false;
  for (size_t i = 0; i < kinds.size(); ++i) {
    if (tokens[i].kind != kinds[i]) return false;
  }
  return true;
}

bool LangSplitLexerTokenizesBasicProgram() {
  Simple::Lang::Lexer lexer("main : () -> i32 { return 1; }");
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

bool LangLexerModuleTokenizesSwitchArrow() {
  Simple::Lang::Lexer lexer("switch (node->value) { default => return 1 }");
  if (!lexer.Lex()) return false;
  return ExpectTokenKinds(lexer.Tokens(), {
      Simple::Lang::TokenKind::KwSwitch,
      Simple::Lang::TokenKind::LParen,
      Simple::Lang::TokenKind::Identifier,
      Simple::Lang::TokenKind::Arrow,
      Simple::Lang::TokenKind::Identifier,
      Simple::Lang::TokenKind::RParen,
      Simple::Lang::TokenKind::LBrace,
      Simple::Lang::TokenKind::KwDefault,
      Simple::Lang::TokenKind::FatArrow,
      Simple::Lang::TokenKind::KwReturn,
      Simple::Lang::TokenKind::Integer,
      Simple::Lang::TokenKind::RBrace,
  });
}


bool LangLexesKeywordsAndOps() {
  const char* src = "main :: () -> void { return; }";
  Simple::Lang::Lexer lex(src);
  if (!lex.Lex()) return false;
  const auto& toks = lex.Tokens();
  std::vector<Simple::Lang::TokenKind> kinds = {
    Simple::Lang::TokenKind::Identifier,
    Simple::Lang::TokenKind::DoubleColon,
    Simple::Lang::TokenKind::LParen,
    Simple::Lang::TokenKind::RParen,
    Simple::Lang::TokenKind::Arrow,
    Simple::Lang::TokenKind::Identifier,
    Simple::Lang::TokenKind::LBrace,
    Simple::Lang::TokenKind::KwReturn,
    Simple::Lang::TokenKind::Semicolon,
    Simple::Lang::TokenKind::RBrace,
  };
  return ExpectTokenKinds(toks, kinds);
}


bool LangLexesAsyncTokens() {
  Simple::Lang::Lexer lex("work :: async () -> i32 { return await next() }");
  if (!lex.Lex()) return false;
  const auto& tokens = lex.Tokens();
  bool saw_async = false;
  bool saw_await = false;
  for (const auto& token : tokens) {
    saw_async = saw_async || token.kind == Simple::Lang::TokenKind::KwAsync;
    saw_await = saw_await || token.kind == Simple::Lang::TokenKind::KwAwait;
  }
  return saw_async && saw_await;
}

bool LangLexesOptionalAndPropagationTokens() {
  Simple::Lang::Lexer lexer("value : i32? = source?");
  if (!lexer.Lex()) return false;
  return ExpectTokenKinds(lexer.Tokens(), {
      Simple::Lang::TokenKind::Identifier,
      Simple::Lang::TokenKind::Colon,
      Simple::Lang::TokenKind::Identifier,
      Simple::Lang::TokenKind::Question,
      Simple::Lang::TokenKind::Assign,
      Simple::Lang::TokenKind::Identifier,
      Simple::Lang::TokenKind::Question,
  });
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


bool LangLexesSwitchArrow() {
  const char* src = "switch(x){ default => return 1 }";
  Simple::Lang::Lexer lex(src);
  if (!lex.Lex()) return false;
  const auto& toks = lex.Tokens();
  std::vector<Simple::Lang::TokenKind> kinds = {
    Simple::Lang::TokenKind::KwSwitch,
    Simple::Lang::TokenKind::LParen,
    Simple::Lang::TokenKind::Identifier,
    Simple::Lang::TokenKind::RParen,
    Simple::Lang::TokenKind::LBrace,
    Simple::Lang::TokenKind::KwDefault,
    Simple::Lang::TokenKind::FatArrow,
    Simple::Lang::TokenKind::KwReturn,
    Simple::Lang::TokenKind::Integer,
    Simple::Lang::TokenKind::RBrace,
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



const TestCase kLangLexerTests[] = {
  {"lang_split_lexer_tokenizes_basic_program", LangSplitLexerTokenizesBasicProgram},
  {"lang_lex_keywords_ops", LangLexesKeywordsAndOps},
  {"lang_lex_async_tokens", LangLexesAsyncTokens},
  {"lang_lex_optional_and_propagation_tokens", LangLexesOptionalAndPropagationTokens},
  {"lang_lex_range_op", LangLexesRangeOp},
  {"lang_lex_switch_arrow", LangLexesSwitchArrow},
  {"lang_lex_literals", LangLexesLiterals},
  {"lang_lex_reject_invalid_hex", LangLexRejectsInvalidHex},
  {"lang_lex_reject_invalid_binary", LangLexRejectsInvalidBinary},
  {"lang_lex_reject_invalid_string_escape", LangLexRejectsInvalidStringEscape},
  {"lang_lex_reject_invalid_char_escape", LangLexRejectsInvalidCharEscape},
  {"lang_lexer_module_tokenizes_switch_arrow", LangLexerModuleTokenizesSwitchArrow},
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
