#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lang_token.h"

namespace Simple::Lang {

enum class Mutability : uint8_t {
  Mutable,
  Immutable,
};

struct TypeDim {
  bool is_list = false;
  bool has_size = false;
  uint64_t size = 0;
};

struct TypeRef {
  std::string name;
  std::vector<TypeRef> type_args;
  std::vector<TypeDim> dims;
  bool is_proc = false;
  Mutability proc_return_mutability = Mutability::Mutable;
  std::vector<TypeRef> proc_params;
  std::unique_ptr<TypeRef> proc_return;
  uint32_t line = 0;
  uint32_t column = 0;
};

struct ParamDecl {
  std::string name;
  Mutability mutability = Mutability::Mutable;
  TypeRef type;
};

enum class ExprKind : uint8_t {
  Identifier,
  Literal,
  Binary,
  Unary,
  Call,
  Member,
  Index,
  ArrayLiteral,
  ListLiteral,
  ArtifactLiteral,
  FnLiteral,
};

enum class LiteralKind : uint8_t {
  Integer,
  Float,
  String,
  Char,
  Bool,
};

struct Expr {
  ExprKind kind = ExprKind::Identifier;
  std::string text;
  LiteralKind literal_kind = LiteralKind::Integer;
  std::string op;
  std::vector<Expr> children;
  std::vector<Expr> args;
  std::vector<TypeRef> type_args;
  std::vector<std::string> field_names;
  std::vector<Expr> field_values;
  std::vector<ParamDecl> fn_params;
  std::vector<Token> fn_body_tokens;
  uint32_t line = 0;
  uint32_t column = 0;
};

struct VarDecl {
  std::string name;
  Mutability mutability = Mutability::Mutable;
  TypeRef type;
  std::vector<Token> init_tokens;
  bool has_init_expr = false;
  Expr init_expr;
};

enum class StmtKind : uint8_t {
  Return,
  Expr,
  Assign,
  VarDecl,
  IfChain,
  IfStmt,
  WhileLoop,
  ForLoop,
  Break,
  Skip,
};

struct Stmt {
  StmtKind kind = StmtKind::Expr;
  bool has_return_expr = false;
  Expr expr;
  Expr target;
  std::string assign_op;
  VarDecl var_decl;
  std::vector<std::pair<Expr, std::vector<Stmt>>> if_branches;
  std::vector<Stmt> else_branch;
  Expr if_cond;
  std::vector<Stmt> if_then;
  std::vector<Stmt> if_else;
  Expr loop_cond;
  std::vector<Stmt> loop_body;
  Expr loop_iter;
  Expr loop_step;
};

struct FuncDecl {
  std::string name;
  std::vector<std::string> generics;
  Mutability return_mutability = Mutability::Mutable;
  TypeRef return_type;
  std::vector<ParamDecl> params;
  std::vector<Stmt> body;
};

struct ArtifactDecl {
  std::string name;
  std::vector<std::string> generics;
  std::vector<VarDecl> fields;
  std::vector<FuncDecl> methods;
};

struct ModuleDecl {
  std::string name;
  std::vector<VarDecl> variables;
  std::vector<FuncDecl> functions;
};

struct EnumMember {
  std::string name;
  bool has_value = false;
  std::string value_text;
};

struct EnumDecl {
  std::string name;
  std::vector<EnumMember> members;
};

enum class DeclKind : uint8_t {
  Function,
  Variable,
  Artifact,
  Module,
  Enum,
};

struct Decl {
  DeclKind kind = DeclKind::Variable;
  FuncDecl func;
  VarDecl var;
  ArtifactDecl artifact;
  ModuleDecl module;
  EnumDecl enm;
};

struct Program {
  std::vector<Decl> decls;
};

} // namespace Simple::Lang
