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

struct FuncDecl {
  std::string name;
  std::vector<std::string> generics;
  Mutability return_mutability = Mutability::Mutable;
  TypeRef return_type;
  std::vector<ParamDecl> params;
  std::vector<Token> body_tokens;
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
  std::vector<Token> body_tokens;
};

struct ModuleDecl {
  std::string name;
  std::vector<Token> body_tokens;
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
