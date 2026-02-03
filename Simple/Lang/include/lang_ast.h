#pragma once

#include <cstdint>
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
};

enum class StmtKind : uint8_t {
  Return,
  Expr,
};

struct Stmt {
  StmtKind kind = StmtKind::Expr;
  Expr expr;
};

struct FuncDecl {
  std::string name;
  std::vector<std::string> generics;
  Mutability return_mutability = Mutability::Mutable;
  TypeRef return_type;
  std::vector<ParamDecl> params;
  std::vector<Stmt> body;
};

struct VarDecl {
  std::string name;
  Mutability mutability = Mutability::Mutable;
  TypeRef type;
  std::vector<Token> init_tokens;
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

enum class DeclKind : uint8_t {
  Function,
  Variable,
  Artifact,
  Module,
};

struct Decl {
  DeclKind kind = DeclKind::Variable;
  FuncDecl func;
  VarDecl var;
  ArtifactDecl artifact;
  ModuleDecl module;
};

struct Program {
  std::vector<Decl> decls;
};

} // namespace Simple::Lang
