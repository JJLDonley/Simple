#pragma once

#include <string>
#include <vector>

#include "AST/ast.h"
#include "Lexer/token.h"

namespace Simple::Lang::CAST {

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);

  const std::string& Error() const { return error_; }
  std::string ErrorWithLocation() const;

  bool ParseType(TypeRef* out);
  bool ParseProgram(Program* out);
  bool ParseBlock(std::vector<Stmt>* out);

private:
  const Token& Peek(size_t offset = 0) const;
  const Token& Advance();
  bool Match(TokenKind kind);
  bool IsAtEnd() const;

  bool ParseTypeInner(TypeRef* out);
  bool ParseTypeSuffixes(TypeRef* out);
  bool ParseTypeArgs(std::vector<TypeRef>* out);
  bool MatchTypeArgumentClose();
  bool ParseTypeDims(TypeRef* out);
  bool ParseDecl(Decl* out);
  bool ParseGenerics(std::vector<std::string>* out);
  bool ParseParamList(std::vector<ParamDecl>* out);
  bool ParseParam(ParamDecl* out);
  bool ParseArtifactDecl(const Token& name_tok,
                         std::vector<std::string> generics,
                         bool is_data,
                         Decl* out);
  bool ParseModuleDecl(const Token& name_tok, Decl* out);
  bool ParseEnumDecl(const Token& name_tok, Decl* out);
  bool ParseArtifactBody(ArtifactDecl* out);
  bool ParseModuleBody(ModuleDecl* out);
  bool ParseArtifactMember(ArtifactDecl* out);
  bool ParseModuleMember(ModuleDecl* out);
  bool ParseBlockTokens(std::vector<Token>* out);
  bool ParseBlockStmts(std::vector<Stmt>* out);
  bool ParseInitTokens(std::vector<Token>* out);
  bool ParseStmt(Stmt* out);
  bool ParseIfChain(Stmt* out);
  bool ParseIfStmt(Stmt* out);
  bool ParseWhile(Stmt* out);
  bool ParseFor(Stmt* out);
  bool ParseExpr(Expr* out);
  bool ParseNonFormatExpr(Expr* out);
  bool ParseAssignmentExpr(Expr* out);
  bool ParseBinaryExpr(int min_prec, Expr* out);
  bool ParseUnaryExpr(Expr* out);
  bool ParsePostfixExpr(Expr* out);
  bool LooksLikeTypeArgsForCall() const;
  bool ParsePrimaryExpr(Expr* out);
  bool ParseSwitchExpr(Expr* out);
  bool ParseCallArgs(std::vector<Expr>* out);
  bool ParseBracketExprList(std::vector<Expr>* out);
  bool ParseFnLiteral(Expr* out);
  int GetBinaryPrecedence(const Token& tok) const;
  bool ConsumeStmtTerminator(const char* ctx);
  bool IsImplicitStmtTerminator() const;
  uint32_t LastTokenLine() const;
  bool RecoverStatementInBlock();

  std::vector<Token> tokens_;
  size_t index_ = 0;
  std::string error_;
  bool had_error_ = false;
  bool allow_format_expr_ = true;
  uint32_t expression_depth_ = 0;
  size_t expression_budget_ = 0;
  uint32_t pending_type_argument_closes_ = 0;
};

bool ParseTypeFromString(const std::string& text, TypeRef* out, std::string* error);
bool ParseProgramFromString(const std::string& text, Program* out, std::string* error);

} // namespace Simple::Lang::CAST
