#pragma once

// Phase-0 AST facade.
//
// The current normalized AST shares the legacy lang_ast.h node types. This
// header establishes the target module boundary while the CAST -> AST lowering
// pass is introduced incrementally.

#include "lang_ast.h"

namespace Simple::Lang::AST {

using TypeDim = Simple::Lang::TypeDim;
using TypeRef = Simple::Lang::TypeRef;
using ParamDecl = Simple::Lang::ParamDecl;
using ExprKind = Simple::Lang::ExprKind;
using LiteralKind = Simple::Lang::LiteralKind;
using Expr = Simple::Lang::Expr;
using VarDecl = Simple::Lang::VarDecl;
using StmtKind = Simple::Lang::StmtKind;
using Stmt = Simple::Lang::Stmt;
using SwitchBranch = Simple::Lang::SwitchBranch;
using FuncDecl = Simple::Lang::FuncDecl;
using ArtifactDecl = Simple::Lang::ArtifactDecl;
using ModuleDecl = Simple::Lang::ModuleDecl;
using EnumMember = Simple::Lang::EnumMember;
using EnumDecl = Simple::Lang::EnumDecl;
using ImportDecl = Simple::Lang::ImportDecl;
using ExternDecl = Simple::Lang::ExternDecl;
using DeclKind = Simple::Lang::DeclKind;
using Decl = Simple::Lang::Decl;
using Program = Simple::Lang::Program;

struct ScriptBody {
  std::vector<Stmt> statements;
};

struct NormalizedFnLiteralDecl {
  std::string binding_name;
  TypeRef signature;
  std::vector<ParamDecl> params;
  std::vector<Token> body_tokens;
  uint32_t line = 0;
  uint32_t column = 0;
};

enum class NormalizedLoopKind : uint8_t {
  While,
  For,
};

struct NormalizedLoop {
  NormalizedLoopKind kind = NormalizedLoopKind::While;
  bool has_initializer = false;
  bool has_loop_var_decl = false;
  VarDecl loop_var_decl;
  Expr initializer;
  Expr condition;
  Expr step;
  std::vector<Stmt> body;
};

struct NormalizedIfBranch {
  Expr condition;
  std::vector<Stmt> body;
};

struct NormalizedIfChain {
  std::vector<NormalizedIfBranch> branches;
  std::vector<Stmt> else_branch;
};

struct NormalizedSwitchBranch {
  bool is_default = false;
  bool is_block = false;
  bool has_inline_value = false;
  bool is_explicit_return = false;
  Expr condition;
  Expr value;
  std::vector<Stmt> block;
};

struct NormalizedSwitch {
  Expr scrutinee;
  std::vector<NormalizedSwitchBranch> branches;
};

struct NormalizedProgram {
  std::vector<Decl> decls;
  ScriptBody script_body;
  std::vector<NormalizedFnLiteralDecl> fn_literals;
  std::vector<NormalizedLoop> loops;
  std::vector<NormalizedIfChain> if_chains;
  std::vector<NormalizedSwitch> switches;
};

} // namespace Simple::Lang::AST
