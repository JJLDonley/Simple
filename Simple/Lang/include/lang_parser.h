#pragma once

#include <string>
#include <vector>

#include "lang_ast.h"
#include "lang_token.h"

namespace Simple::Lang {

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);

  const std::string& Error() const { return error_; }

  bool ParseType(TypeRef* out);
  bool ParseProgram(Program* out);

private:
  const Token& Peek(size_t offset = 0) const;
  const Token& Advance();
  bool Match(TokenKind kind);
  bool IsAtEnd() const;

  bool ParseTypeInner(TypeRef* out);
  bool ParseTypeArgs(std::vector<TypeRef>* out);
  bool ParseTypeDims(std::vector<TypeDim>* out);
  bool ParseDecl(Decl* out);
  bool ParseGenerics(std::vector<std::string>* out);
  bool ParseParamList(std::vector<ParamDecl>* out);
  bool ParseParam(ParamDecl* out);
  bool ParseArtifactDecl(const Token& name_tok,
                         std::vector<std::string> generics,
                         Decl* out);
  bool ParseModuleDecl(const Token& name_tok, Decl* out);
  bool ParseArtifactBody(ArtifactDecl* out);
  bool ParseModuleBody(ModuleDecl* out);
  bool ParseArtifactMember(ArtifactDecl* out);
  bool ParseModuleMember(ModuleDecl* out);
  bool ParseBlockTokens(std::vector<Token>* out);
  bool ParseBlockStmts(std::vector<Stmt>* out);
  bool ParseInitTokens(std::vector<Token>* out);
  bool ParseStmt(Stmt* out);
  bool ParseExpr(Expr* out);
  bool ParseBinaryExpr(int min_prec, Expr* out);
  bool ParseUnaryExpr(Expr* out);
  bool ParsePostfixExpr(Expr* out);
  bool ParsePrimaryExpr(Expr* out);
  bool ParseCallArgs(std::vector<Expr>* out);
  bool ParseBracketExprList(std::vector<Expr>* out);
  int GetBinaryPrecedence(const Token& tok) const;

  std::vector<Token> tokens_;
  size_t index_ = 0;
  std::string error_;
};

bool ParseTypeFromString(const std::string& text, TypeRef* out, std::string* error);
bool ParseProgramFromString(const std::string& text, Program* out, std::string* error);

} // namespace Simple::Lang
