#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::RAST {

using Program = Simple::Lang::AST::Program;
using Decl = Simple::Lang::AST::Decl;
using Stmt = Simple::Lang::AST::Stmt;
using Expr = Simple::Lang::AST::Expr;
using TypeRef = Simple::Lang::AST::TypeRef;

using SymbolId = uint32_t;

constexpr SymbolId kInvalidSymbolId = 0xFFFFFFFFu;

enum class SymbolKind : uint8_t {
  Import,
  Extern,
  Function,
  Global,
  Artifact,
  ArtifactField,
  ArtifactMethod,
  Module,
  ModuleVariable,
  ModuleFunction,
  Enum,
  EnumMember,
  Parameter,
  Local,
  Self,
};

struct Symbol {
  SymbolId id = kInvalidSymbolId;
  SymbolKind kind = SymbolKind::Global;
  std::string name;
  std::string qualified_name;
  SymbolId parent = kInvalidSymbolId;
};

enum class MemberRefKind : uint8_t {
  Unknown,
  StaticMember,
  SelfMember,
  ArtifactMember,
  ModuleMember,
  ArtifactField,
  ArtifactMethod,
  EnumMember,
  ExternSymbol,
  ReservedModuleFunction,
  DLManifestCall,
};

struct MemberRef {
  MemberRefKind kind = MemberRefKind::Unknown;
  std::string base;
  std::string member;
  std::string qualified_name;
  SymbolId symbol = kInvalidSymbolId;
};

struct ResolvedProgram {
  const Program* program = nullptr;
  std::vector<Symbol> symbols;
  std::vector<MemberRef> member_refs;
  std::unordered_map<std::string, SymbolId> by_qualified_name;
};

struct ResolvedProgramView {
  const Program* program = nullptr;
};

} // namespace Simple::Lang::RAST
