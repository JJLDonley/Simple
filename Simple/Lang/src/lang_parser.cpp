#include "lang_parser.h"

#include "lang_lexer.h"

namespace Simple::Lang {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::Peek(size_t offset) const {
  if (index_ + offset >= tokens_.size()) return tokens_.back();
  return tokens_[index_ + offset];
}

const Token& Parser::Advance() {
  if (!IsAtEnd()) {
    ++index_;
  }
  return tokens_[index_ - 1];
}

bool Parser::Match(TokenKind kind) {
  if (Peek().kind == kind) {
    Advance();
    return true;
  }
  return false;
}

bool Parser::IsAtEnd() const {
  return Peek().kind == TokenKind::End;
}

bool Parser::ParseType(TypeRef* out) {
  if (!out) return false;
  if (!ParseTypeInner(out)) return false;
  if (!IsAtEnd()) {
    error_ = "unexpected token after type: " + Peek().text;
    return false;
  }
  return true;
}

bool Parser::ParseProgram(Program* out) {
  if (!out) return false;
  out->decls.clear();
  while (!IsAtEnd()) {
    Decl decl;
    if (!ParseDecl(&decl)) return false;
    out->decls.push_back(std::move(decl));
  }
  return true;
}

bool Parser::ParseTypeInner(TypeRef* out) {
  if (Match(TokenKind::LParen)) {
    TypeRef proc;
    proc.is_proc = true;
    if (!Match(TokenKind::RParen)) {
      for (;;) {
        TypeRef param;
        if (!ParseTypeInner(&param)) return false;
        proc.proc_params.push_back(std::move(param));
        if (Match(TokenKind::Comma)) continue;
        if (Match(TokenKind::RParen)) break;
        error_ = "expected ',' or ')' in procedure type";
        return false;
      }
    }
    if (Match(TokenKind::Colon)) {
      proc.proc_return_mutability = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      proc.proc_return_mutability = Mutability::Immutable;
    } else {
      error_ = "expected ':' or '::' before procedure return type";
      return false;
    }
    TypeRef ret;
    if (!ParseTypeInner(&ret)) return false;
    proc.proc_return = std::make_unique<TypeRef>(std::move(ret));
    if (!ParseTypeDims(&proc)) return false;
    if (out) *out = std::move(proc);
    return true;
  }

  const Token& tok = Peek();
  if (tok.kind != TokenKind::Identifier) {
    error_ = "expected type name";
    return false;
  }
  out->name = tok.text;
  Advance();

  if (Match(TokenKind::Lt)) {
    if (!ParseTypeArgs(&out->type_args)) return false;
  }

  if (!ParseTypeDims(out)) return false;
  return true;
}

bool Parser::ParseDecl(Decl* out) {
  if (Match(TokenKind::KwFn)) {
    const Token& name_tok = Peek();
    if (name_tok.kind != TokenKind::Identifier) {
      error_ = "expected function name after 'fn'";
      return false;
    }
    Advance();

    std::vector<std::string> generics;
    if (Match(TokenKind::Lt)) {
      if (!ParseGenerics(&generics)) return false;
    }

    Mutability mut = Mutability::Immutable;
    if (Match(TokenKind::Colon)) {
      mut = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      mut = Mutability::Immutable;
    } else {
      error_ = "expected ':' or '::' after function name";
      return false;
    }

    TypeRef return_type;
    if (!ParseTypeInner(&return_type)) return false;
    if (!Match(TokenKind::LParen)) {
      error_ = "expected '(' after function return type";
      return false;
    }
    if (out) {
      out->kind = DeclKind::Function;
      out->func.name = name_tok.text;
      out->func.generics = std::move(generics);
      out->func.return_mutability = mut;
      out->func.return_type = std::move(return_type);
    }
    if (!ParseParamList(&out->func.params)) return false;
    if (!ParseBlockStmts(&out->func.body)) return false;
    return true;
  }

  const Token& name_tok = Peek();
  if (name_tok.kind == TokenKind::End) return false;
  if (name_tok.kind != TokenKind::Identifier) {
    error_ = "expected identifier at top level";
    return false;
  }
  Advance();

  std::vector<std::string> generics;
  if (Match(TokenKind::Lt)) {
    if (!ParseGenerics(&generics)) return false;
  }

  if (Match(TokenKind::DoubleColon)) {
    if (Match(TokenKind::KwArtifact)) {
      return ParseArtifactDecl(name_tok, std::move(generics), out);
    }
    if (Match(TokenKind::KwModule)) {
      return ParseModuleDecl(name_tok, out);
    }
    if (Match(TokenKind::KwEnum)) {
      if (!generics.empty()) {
        error_ = "enum declarations do not support generics";
        return false;
      }
      return ParseEnumDecl(name_tok, out);
    }
    Mutability mut = Mutability::Immutable;
    TypeRef return_or_type;
    if (!ParseTypeInner(&return_or_type)) return false;
    if (Match(TokenKind::LParen)) {
      if (out) {
        out->kind = DeclKind::Function;
        out->func.name = name_tok.text;
        out->func.generics = std::move(generics);
        out->func.return_mutability = mut;
        out->func.return_type = std::move(return_or_type);
      }
      if (!ParseParamList(&out->func.params)) return false;
      if (!ParseBlockStmts(&out->func.body)) return false;
      return true;
    }
    if (out) {
      out->kind = DeclKind::Variable;
      out->var.name = name_tok.text;
      out->var.mutability = mut;
      out->var.type = std::move(return_or_type);
    }
    if (!Match(TokenKind::Assign)) {
      error_ = "expected '=' in variable declaration";
      return false;
    }
    if (!ParseInitTokens(&out->var.init_tokens)) return false;
    return true;
  }

  Mutability mut = Mutability::Mutable;
  if (!Match(TokenKind::Colon)) {
    error_ = "expected ':' or '::' after identifier";
    return false;
  }

  TypeRef return_or_type;
  if (!ParseTypeInner(&return_or_type)) return false;

  if (Match(TokenKind::LParen)) {
    if (out) {
      out->kind = DeclKind::Function;
      out->func.name = name_tok.text;
      out->func.generics = std::move(generics);
      out->func.return_mutability = mut;
      out->func.return_type = std::move(return_or_type);
    }
    if (!ParseParamList(&out->func.params)) return false;
    if (!ParseBlockStmts(&out->func.body)) return false;
    return true;
  }

  if (out) {
    out->kind = DeclKind::Variable;
    out->var.name = name_tok.text;
    out->var.mutability = mut;
    out->var.type = std::move(return_or_type);
  }
  if (!Match(TokenKind::Assign)) {
    error_ = "expected '=' in variable declaration";
    return false;
  }
  if (!ParseInitTokens(&out->var.init_tokens)) return false;
  return true;
}

bool Parser::ParseArtifactDecl(const Token& name_tok,
                               std::vector<std::string> generics,
                               Decl* out) {
  if (out) {
    out->kind = DeclKind::Artifact;
    out->artifact.name = name_tok.text;
    out->artifact.generics = std::move(generics);
  }
  if (!ParseArtifactBody(&out->artifact)) return false;
  return true;
}

bool Parser::ParseModuleDecl(const Token& name_tok, Decl* out) {
  if (out) {
    out->kind = DeclKind::Module;
    out->module.name = name_tok.text;
  }
  if (!ParseModuleBody(&out->module)) return false;
  return true;
}

bool Parser::ParseEnumDecl(const Token& name_tok, Decl* out) {
  if (out) {
    out->kind = DeclKind::Enum;
    out->enm.name = name_tok.text;
  }
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start enum body";
    return false;
  }
  if (Match(TokenKind::RBrace)) return true;
  while (!IsAtEnd()) {
    const Token& member_tok = Peek();
    if (member_tok.kind != TokenKind::Identifier) {
      error_ = "expected enum member name";
      return false;
    }
    Advance();
    EnumMember member;
    member.name = member_tok.text;
    if (Match(TokenKind::Assign)) {
      const Token& value_tok = Peek();
      if (value_tok.kind != TokenKind::Integer) {
        error_ = "expected integer literal for enum value";
        return false;
      }
      member.has_value = true;
      member.value_text = value_tok.text;
      Advance();
    }
    if (out) out->enm.members.push_back(std::move(member));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RBrace)) return true;
    error_ = "expected ',' or '}' after enum member";
    return false;
  }
  error_ = "unterminated enum body";
  return false;
}

bool Parser::ParseArtifactBody(ArtifactDecl* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start artifact body";
    return false;
  }
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) return true;
    if (!ParseArtifactMember(out)) return false;
  }
  error_ = "unterminated artifact body";
  return false;
}

bool Parser::ParseModuleBody(ModuleDecl* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start module body";
    return false;
  }
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) return true;
    if (!ParseModuleMember(out)) return false;
  }
  error_ = "unterminated module body";
  return false;
}

bool Parser::ParseArtifactMember(ArtifactDecl* out) {
  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    error_ = "expected artifact member name";
    return false;
  }
  Advance();

  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after member name";
    return false;
  }

  TypeRef type;
  if (!ParseTypeInner(&type)) return false;

  if (Match(TokenKind::LParen)) {
    FuncDecl fn;
    fn.name = name_tok.text;
    fn.return_mutability = mut;
    fn.return_type = std::move(type);
    if (!ParseParamList(&fn.params)) return false;
    if (!ParseBlockStmts(&fn.body)) return false;
    if (out) out->methods.push_back(std::move(fn));
    return true;
  }

  VarDecl field;
  field.name = name_tok.text;
  field.mutability = mut;
  field.type = std::move(type);
  if (Match(TokenKind::Semicolon)) {
    // optional
  }
  if (out) out->fields.push_back(std::move(field));
  return true;
}

bool Parser::ParseModuleMember(ModuleDecl* out) {
  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    error_ = "expected module member name";
    return false;
  }
  Advance();

  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after member name";
    return false;
  }

  TypeRef type;
  if (!ParseTypeInner(&type)) return false;

  if (Match(TokenKind::LParen)) {
    FuncDecl fn;
    fn.name = name_tok.text;
    fn.return_mutability = mut;
    fn.return_type = std::move(type);
    if (!ParseParamList(&fn.params)) return false;
    if (!ParseBlockStmts(&fn.body)) return false;
    if (out) out->functions.push_back(std::move(fn));
    return true;
  }

  VarDecl var;
  var.name = name_tok.text;
  var.mutability = mut;
  var.type = std::move(type);
  if (!Match(TokenKind::Assign)) {
    error_ = "expected '=' in module variable declaration";
    return false;
  }
  if (!ParseInitTokens(&var.init_tokens)) return false;
  if (out) out->variables.push_back(std::move(var));
  return true;
}

bool Parser::ParseGenerics(std::vector<std::string>* out) {
  if (!out) return false;
  if (Match(TokenKind::Gt)) {
    error_ = "empty generic parameter list";
    return false;
  }
  for (;;) {
    const Token& tok = Peek();
    if (tok.kind != TokenKind::Identifier) {
      error_ = "expected generic parameter name";
      return false;
    }
    out->push_back(tok.text);
    Advance();
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::Gt)) break;
    error_ = "expected ',' or '>' in generic parameter list";
    return false;
  }
  return true;
}

bool Parser::ParseParamList(std::vector<ParamDecl>* out) {
  if (!out) return false;
  if (Match(TokenKind::RParen)) return true;
  for (;;) {
    ParamDecl param;
    if (!ParseParam(&param)) return false;
    out->push_back(std::move(param));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RParen)) break;
    error_ = "expected ',' or ')' after parameter";
    return false;
  }
  return true;
}

bool Parser::ParseParam(ParamDecl* out) {
  const Token& name_tok = Peek();
  if (name_tok.kind != TokenKind::Identifier) {
    error_ = "expected parameter name";
    return false;
  }
  Advance();
  Mutability mut = Mutability::Mutable;
  if (Match(TokenKind::Colon)) {
    mut = Mutability::Mutable;
  } else if (Match(TokenKind::DoubleColon)) {
    mut = Mutability::Immutable;
  } else {
    error_ = "expected ':' or '::' after parameter name";
    return false;
  }
  TypeRef type;
  if (!ParseTypeInner(&type)) return false;
  out->name = name_tok.text;
  out->mutability = mut;
  out->type = std::move(type);
  return true;
}

bool Parser::ParseBlockTokens(std::vector<Token>* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start block";
    return false;
  }
  int depth = 1;
  while (!IsAtEnd()) {
    Token tok = Advance();
    if (tok.kind == TokenKind::LBrace) depth++;
    if (tok.kind == TokenKind::RBrace) {
      depth--;
      if (depth == 0) return true;
    }
    if (out) out->push_back(tok);
  }
  error_ = "unterminated block";
  return false;
}

bool Parser::ParseBlockStmts(std::vector<Stmt>* out) {
  if (!Match(TokenKind::LBrace)) {
    error_ = "expected '{' to start block";
    return false;
  }
  while (!IsAtEnd()) {
    if (Match(TokenKind::RBrace)) return true;
    Stmt stmt;
    if (!ParseStmt(&stmt)) return false;
    if (out) out->push_back(std::move(stmt));
  }
  error_ = "unterminated block";
  return false;
}

bool Parser::ParseInitTokens(std::vector<Token>* out) {
  while (!IsAtEnd()) {
    if (Peek().kind == TokenKind::Semicolon) {
      Advance();
      return true;
    }
    if (out) out->push_back(Advance());
  }
  error_ = "unterminated variable declaration";
  return false;
}

bool Parser::ParseStmt(Stmt* out) {
  if (Match(TokenKind::KwReturn)) {
    if (Match(TokenKind::Semicolon)) {
      if (out) {
        out->kind = StmtKind::Return;
        out->has_return_expr = false;
      }
      return true;
    }
    Expr expr;
    if (!ParseExpr(&expr)) return false;
    if (!Match(TokenKind::Semicolon)) {
      error_ = "expected ';' after return";
      return false;
    }
    if (out) {
      out->kind = StmtKind::Return;
      out->has_return_expr = true;
      out->expr = std::move(expr);
    }
    return true;
  }

  if (Peek().kind == TokenKind::PipeGt) {
    return ParseIfChain(out);
  }

  if (Peek().kind == TokenKind::KwIf) {
    return ParseIfStmt(out);
  }

  if (Peek().kind == TokenKind::KwWhile) {
    return ParseWhile(out);
  }

  if (Peek().kind == TokenKind::KwFor) {
    return ParseFor(out);
  }

  if (Match(TokenKind::KwBreak)) {
    if (!Match(TokenKind::Semicolon)) {
      error_ = "expected ';' after break";
      return false;
    }
    if (out) {
      out->kind = StmtKind::Break;
    }
    return true;
  }

  if (Match(TokenKind::KwSkip)) {
    if (!Match(TokenKind::Semicolon)) {
      error_ = "expected ';' after skip";
      return false;
    }
    if (out) {
      out->kind = StmtKind::Skip;
    }
    return true;
  }

  if (Peek().kind == TokenKind::Identifier &&
      (Peek(1).kind == TokenKind::Colon || Peek(1).kind == TokenKind::DoubleColon)) {
    Token name_tok = Advance();
    Mutability mut = Mutability::Mutable;
    if (Match(TokenKind::Colon)) {
      mut = Mutability::Mutable;
    } else if (Match(TokenKind::DoubleColon)) {
      mut = Mutability::Immutable;
    }
    TypeRef type;
    if (!ParseTypeInner(&type)) return false;
    if (!Match(TokenKind::Assign)) {
      error_ = "expected '=' in variable declaration";
      return false;
    }
    Expr init;
    if (!ParseExpr(&init)) return false;
    if (!Match(TokenKind::Semicolon)) {
      error_ = "expected ';' after variable declaration";
      return false;
    }
    if (out) {
      out->kind = StmtKind::VarDecl;
      out->var_decl.name = name_tok.text;
      out->var_decl.mutability = mut;
      out->var_decl.type = std::move(type);
      out->var_decl.has_init_expr = true;
      out->var_decl.init_expr = std::move(init);
    }
    return true;
  }

  size_t save = index_;
  Expr target;
  if (ParsePostfixExpr(&target)) {
    const Token& op = Peek();
    bool is_assign = false;
    switch (op.kind) {
      case TokenKind::Assign:
      case TokenKind::PlusEq:
      case TokenKind::MinusEq:
      case TokenKind::StarEq:
      case TokenKind::SlashEq:
      case TokenKind::PercentEq:
      case TokenKind::AmpEq:
      case TokenKind::PipeEq:
      case TokenKind::CaretEq:
      case TokenKind::ShlEq:
      case TokenKind::ShrEq:
        is_assign = true;
        break;
      default:
        break;
    }
    if (is_assign) {
      Advance();
      Expr value;
      if (!ParseExpr(&value)) return false;
      if (!Match(TokenKind::Semicolon)) {
        error_ = "expected ';' after assignment";
        return false;
      }
      if (out) {
        out->kind = StmtKind::Assign;
        out->target = std::move(target);
        out->assign_op = op.text;
        out->expr = std::move(value);
      }
      return true;
    }
  }
  index_ = save;

  Expr expr;
  if (!ParseExpr(&expr)) return false;
  if (!Match(TokenKind::Semicolon)) {
    error_ = "expected ';' after expression";
    return false;
  }
  if (out) {
    out->kind = StmtKind::Expr;
    out->expr = std::move(expr);
  }
  return true;
}

bool Parser::ParseFor(Stmt* out) {
  if (!Match(TokenKind::KwFor)) {
    error_ = "expected 'for'";
    return false;
  }
  Expr iter;
  if (!ParseAssignmentExpr(&iter)) return false;
  if (!Match(TokenKind::Semicolon)) {
    error_ = "expected ';' after for iterator";
    return false;
  }
  Expr cond;
  if (!ParseExpr(&cond)) return false;
  if (!Match(TokenKind::Semicolon)) {
    error_ = "expected ';' after for condition";
    return false;
  }
  Expr step;
  if (!ParseAssignmentExpr(&step)) return false;
  if (!ParseBlockStmts(&out->loop_body)) return false;
  if (out) {
    out->kind = StmtKind::ForLoop;
    out->loop_iter = std::move(iter);
    out->loop_cond = std::move(cond);
    out->loop_step = std::move(step);
  }
  return true;
}

bool Parser::ParseIfChain(Stmt* out) {
  if (!Match(TokenKind::PipeGt)) {
    error_ = "expected '|>' to start if chain";
    return false;
  }
  Expr first_cond;
  if (!ParseExpr(&first_cond)) return false;
  std::vector<Stmt> then_body;
  if (!ParseBlockStmts(&then_body)) return false;
  if (out) {
    out->kind = StmtKind::IfChain;
    out->if_branches.push_back({std::move(first_cond), std::move(then_body)});
  }
  while (Match(TokenKind::PipeGt)) {
    if (Match(TokenKind::KwDefault)) {
      std::vector<Stmt> else_body;
      if (!ParseBlockStmts(&else_body)) return false;
      if (out) out->else_branch = std::move(else_body);
      break;
    }
    Expr cond;
    if (!ParseExpr(&cond)) return false;
    std::vector<Stmt> body;
    if (!ParseBlockStmts(&body)) return false;
    if (out) out->if_branches.push_back({std::move(cond), std::move(body)});
  }
  return true;
}

bool Parser::ParseIfStmt(Stmt* out) {
  if (!Match(TokenKind::KwIf)) {
    error_ = "expected 'if'";
    return false;
  }
  Expr cond;
  if (!ParseExpr(&cond)) return false;
  std::vector<Stmt> then_body;
  if (!ParseBlockStmts(&then_body)) return false;
  std::vector<Stmt> else_body;
  if (Match(TokenKind::KwElse)) {
    if (Peek().kind == TokenKind::KwIf) {
      Stmt nested;
      if (!ParseIfStmt(&nested)) return false;
      else_body.push_back(std::move(nested));
    } else {
      if (!ParseBlockStmts(&else_body)) return false;
    }
  }
  if (out) {
    out->kind = StmtKind::IfStmt;
    out->if_cond = std::move(cond);
    out->if_then = std::move(then_body);
    out->if_else = std::move(else_body);
  }
  return true;
}

bool Parser::ParseWhile(Stmt* out) {
  if (!Match(TokenKind::KwWhile)) {
    error_ = "expected 'while'";
    return false;
  }
  Expr cond;
  if (!ParseExpr(&cond)) return false;
  std::vector<Stmt> body;
  if (!ParseBlockStmts(&body)) return false;
  if (out) {
    out->kind = StmtKind::WhileLoop;
    out->loop_cond = std::move(cond);
    out->loop_body = std::move(body);
  }
  return true;
}

bool Parser::ParseExpr(Expr* out) {
  return ParseBinaryExpr(0, out);
}

bool Parser::ParseAssignmentExpr(Expr* out) {
  size_t save = index_;
  Expr target;
  if (ParsePostfixExpr(&target)) {
    const Token& op = Peek();
    bool is_assign = false;
    switch (op.kind) {
      case TokenKind::Assign:
      case TokenKind::PlusEq:
      case TokenKind::MinusEq:
      case TokenKind::StarEq:
      case TokenKind::SlashEq:
      case TokenKind::PercentEq:
      case TokenKind::AmpEq:
      case TokenKind::PipeEq:
      case TokenKind::CaretEq:
      case TokenKind::ShlEq:
      case TokenKind::ShrEq:
        is_assign = true;
        break;
      default:
        break;
    }
    if (is_assign) {
      Advance();
      Expr value;
      if (!ParseExpr(&value)) return false;
      Expr expr;
      expr.kind = ExprKind::Binary;
      expr.op = op.text;
      expr.children.push_back(std::move(target));
      expr.children.push_back(std::move(value));
      if (out) *out = std::move(expr);
      return true;
    }
  }
  index_ = save;
  return ParseExpr(out);
}

int Parser::GetBinaryPrecedence(const Token& tok) const {
  switch (tok.kind) {
    case TokenKind::OrOr:
      return 1;
    case TokenKind::AndAnd:
      return 2;
    case TokenKind::EqEq:
    case TokenKind::NotEq:
      return 3;
    case TokenKind::Lt:
    case TokenKind::Le:
    case TokenKind::Gt:
    case TokenKind::Ge:
      return 4;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
      return 20;
    case TokenKind::Plus:
    case TokenKind::Minus:
      return 10;
    default:
      return -1;
  }
}

bool Parser::ParseBinaryExpr(int min_prec, Expr* out) {
  Expr lhs;
  if (!ParseUnaryExpr(&lhs)) return false;

  while (true) {
    const Token& op = Peek();
    int prec = GetBinaryPrecedence(op);
    if (prec < min_prec) break;
    Advance();
    Expr rhs;
    if (!ParseBinaryExpr(prec + 1, &rhs)) return false;
    Expr combined;
    combined.kind = ExprKind::Binary;
    combined.op = op.text;
    combined.children.push_back(std::move(lhs));
    combined.children.push_back(std::move(rhs));
    lhs = std::move(combined);
  }

  if (out) *out = std::move(lhs);
  return true;
}

bool Parser::ParseUnaryExpr(Expr* out) {
  const Token& tok = Peek();
  if (tok.kind == TokenKind::Bang || tok.kind == TokenKind::Minus ||
      tok.kind == TokenKind::PlusPlus || tok.kind == TokenKind::MinusMinus) {
    Advance();
    Expr operand;
    if (!ParseUnaryExpr(&operand)) return false;
    Expr expr;
    expr.kind = ExprKind::Unary;
    expr.op = tok.text;
    expr.children.push_back(std::move(operand));
    if (out) *out = std::move(expr);
    return true;
  }
  return ParsePostfixExpr(out);
}

bool Parser::ParsePostfixExpr(Expr* out) {
  Expr expr;
  if (!ParsePrimaryExpr(&expr)) return false;
  for (;;) {
    if (Match(TokenKind::LParen)) {
      Expr call;
      call.kind = ExprKind::Call;
      call.children.push_back(std::move(expr));
      if (!ParseCallArgs(&call.args)) return false;
      expr = std::move(call);
      continue;
    }
    if (Match(TokenKind::LBracket)) {
      Expr index;
      index.kind = ExprKind::Index;
      index.children.push_back(std::move(expr));
      Expr idx_expr;
      if (!ParseExpr(&idx_expr)) return false;
      if (!Match(TokenKind::RBracket)) {
        error_ = "expected ']' after index expression";
        return false;
      }
      index.children.push_back(std::move(idx_expr));
      expr = std::move(index);
      continue;
    }
    if (Match(TokenKind::Dot)) {
      const Token& name = Peek();
      if (name.kind != TokenKind::Identifier) {
        error_ = "expected member name after '.'";
        return false;
      }
      Advance();
      Expr member;
      member.kind = ExprKind::Member;
      member.op = ".";
      member.text = name.text;
      member.children.push_back(std::move(expr));
      expr = std::move(member);
      continue;
    }
    if (Match(TokenKind::DoubleColon)) {
      const Token& name = Peek();
      if (name.kind != TokenKind::Identifier) {
        error_ = "expected member name after '::'";
        return false;
      }
      Advance();
      Expr member;
      member.kind = ExprKind::Member;
      member.op = "::";
      member.text = name.text;
      member.children.push_back(std::move(expr));
      expr = std::move(member);
      continue;
    }
    if (Match(TokenKind::PlusPlus) || Match(TokenKind::MinusMinus)) {
      Expr unary;
      unary.kind = ExprKind::Unary;
      unary.op = "post" + tokens_[index_ - 1].text;
      unary.children.push_back(std::move(expr));
      expr = std::move(unary);
      continue;
    }
    break;
  }
  if (out) *out = std::move(expr);
  return true;
}

bool Parser::ParsePrimaryExpr(Expr* out) {
  const Token& tok = Peek();
  if (tok.kind == TokenKind::Integer || tok.kind == TokenKind::Float ||
      tok.kind == TokenKind::String || tok.kind == TokenKind::Char ||
      tok.kind == TokenKind::KwTrue || tok.kind == TokenKind::KwFalse) {
    Expr expr;
    expr.kind = ExprKind::Literal;
    expr.text = tok.text;
    if (tok.kind == TokenKind::Integer) expr.literal_kind = LiteralKind::Integer;
    else if (tok.kind == TokenKind::Float) expr.literal_kind = LiteralKind::Float;
    else if (tok.kind == TokenKind::String) expr.literal_kind = LiteralKind::String;
    else if (tok.kind == TokenKind::Char) expr.literal_kind = LiteralKind::Char;
    else expr.literal_kind = LiteralKind::Bool;
    Advance();
    if (out) *out = std::move(expr);
    return true;
  }
  if (tok.kind == TokenKind::LParen) {
    size_t save = index_;
    if (ParseFnLiteral(out)) return true;
    index_ = save;
  }
  if (tok.kind == TokenKind::Identifier) {
    Expr expr;
    expr.kind = ExprKind::Identifier;
    expr.text = tok.text;
    Advance();
    if (out) *out = std::move(expr);
    return true;
  }
  if (Match(TokenKind::LParen)) {
    Expr expr;
    if (!ParseExpr(&expr)) return false;
    if (!Match(TokenKind::RParen)) {
      error_ = "expected ')' after expression";
      return false;
    }
    if (out) *out = std::move(expr);
    return true;
  }
  if (Match(TokenKind::LBracket)) {
    std::vector<Expr> elements;
    if (!ParseBracketExprList(&elements)) return false;
    Expr expr;
    if (elements.empty()) {
      expr.kind = ExprKind::ListLiteral;
    } else {
      expr.kind = ExprKind::ArrayLiteral;
    }
    expr.children = std::move(elements);
    if (out) *out = std::move(expr);
    return true;
  }
  if (Match(TokenKind::LBrace)) {
    Expr expr;
    expr.kind = ExprKind::ArtifactLiteral;
    bool seen_named = false;
    if (Match(TokenKind::RBrace)) {
      if (out) *out = std::move(expr);
      return true;
    }
    while (!IsAtEnd()) {
      if (Match(TokenKind::Dot)) {
        const Token& field_tok = Peek();
        if (field_tok.kind != TokenKind::Identifier) {
          error_ = "expected field name after '.' in artifact literal";
          return false;
        }
        Advance();
        if (!Match(TokenKind::Assign)) {
          error_ = "expected '=' after artifact field name";
          return false;
        }
        Expr value;
        if (!ParseExpr(&value)) return false;
        expr.field_names.push_back(field_tok.text);
        expr.field_values.push_back(std::move(value));
        seen_named = true;
      } else {
        if (seen_named) {
          error_ = "positional values must come before named fields in artifact literal";
          return false;
        }
        Expr value;
        if (!ParseExpr(&value)) return false;
        expr.children.push_back(std::move(value));
      }
      if (Match(TokenKind::Comma)) continue;
      if (Match(TokenKind::RBrace)) break;
      error_ = "expected ',' or '}' in artifact literal";
      return false;
    }
    if (out) *out = std::move(expr);
    return true;
  }
  error_ = "expected expression";
  return false;
}

bool Parser::ParseFnLiteral(Expr* out) {
  if (!Match(TokenKind::LParen)) return false;
  size_t start_index = index_ - 1;
  std::vector<ParamDecl> params;
  if (!Match(TokenKind::RParen)) {
    for (;;) {
      ParamDecl param;
      if (!ParseParam(&param)) return false;
      params.push_back(std::move(param));
      if (Match(TokenKind::Comma)) continue;
      if (Match(TokenKind::RParen)) break;
      error_ = "expected ',' or ')' after parameter";
      return false;
    }
  }
  std::vector<Token> body_tokens;
  if (!ParseBlockTokens(&body_tokens)) return false;
  body_tokens.insert(body_tokens.begin(), tokens_[start_index]);
  if (out) {
    out->kind = ExprKind::FnLiteral;
    out->fn_params = std::move(params);
    out->fn_body_tokens = std::move(body_tokens);
  }
  return true;
}

bool Parser::ParseCallArgs(std::vector<Expr>* out) {
  if (Match(TokenKind::RParen)) return true;
  for (;;) {
    Expr arg;
    if (!ParseExpr(&arg)) return false;
    if (out) out->push_back(std::move(arg));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RParen)) break;
    error_ = "expected ',' or ')' in call arguments";
    return false;
  }
  return true;
}

bool Parser::ParseBracketExprList(std::vector<Expr>* out) {
  if (Match(TokenKind::RBracket)) return true;
  for (;;) {
    Expr element;
    if (!ParseExpr(&element)) return false;
    if (out) out->push_back(std::move(element));
    if (Match(TokenKind::Comma)) continue;
    if (Match(TokenKind::RBracket)) break;
    error_ = "expected ',' or ']' in list";
    return false;
  }
  return true;
}

bool Parser::ParseTypeArgs(std::vector<TypeRef>* out) {
  if (!out) return false;
  if (Match(TokenKind::Gt)) {
    error_ = "empty type argument list";
    return false;
  }
  for (;;) {
    TypeRef arg;
    if (!ParseTypeInner(&arg)) return false;
    out->push_back(std::move(arg));
    if (Match(TokenKind::Comma)) {
      continue;
    }
    if (Match(TokenKind::Gt)) {
      break;
    }
    error_ = "expected ',' or '>' in type arguments";
    return false;
  }
  return true;
}

bool Parser::ParseTypeDims(TypeRef* out) {
  if (!out) return false;
  while (Match(TokenKind::LBracket)) {
    if (out->is_proc) {
      error_ = "procedure types cannot have array/list dimensions";
      return false;
    }
    TypeDim dim;
    if (Match(TokenKind::RBracket)) {
      dim.is_list = true;
      dim.has_size = false;
      out->dims.push_back(dim);
      continue;
    }
    const Token& size_tok = Peek();
    if (size_tok.kind != TokenKind::Integer) {
      error_ = "expected array size literal";
      return false;
    }
    dim.has_size = true;
    dim.is_list = false;
    dim.size = static_cast<uint64_t>(std::stoull(size_tok.text));
    Advance();
    if (!Match(TokenKind::RBracket)) {
      error_ = "expected ']' after array size";
      return false;
    }
    out->dims.push_back(dim);
  }
  return true;
}

bool ParseTypeFromString(const std::string& text, TypeRef* out, std::string* error) {
  Lexer lexer(text);
  if (!lexer.Lex()) {
    if (error) *error = lexer.Error();
    return false;
  }
  Parser parser(lexer.Tokens());
  if (!parser.ParseType(out)) {
    if (error) *error = parser.Error();
    return false;
  }
  return true;
}

bool ParseProgramFromString(const std::string& text, Program* out, std::string* error) {
  Lexer lexer(text);
  if (!lexer.Lex()) {
    if (error) *error = lexer.Error();
    return false;
  }
  Parser parser(lexer.Tokens());
  if (!parser.ParseProgram(out)) {
    if (error) *error = parser.Error();
    return false;
  }
  return true;
}

} // namespace Simple::Lang
