#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Lexer/token.h"

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
  uint32_t pointer_depth = 0;
  std::vector<TypeRef> type_args;
  std::vector<TypeDim> dims;
  bool is_proc = false;
  Mutability proc_return_mutability = Mutability::Mutable;
  std::vector<TypeRef> proc_params;
  std::unique_ptr<TypeRef> proc_return;
  uint32_t line = 0;
  uint32_t column = 0;

  TypeRef() = default;
  TypeRef(const TypeRef& other)
      : name(other.name),
        pointer_depth(other.pointer_depth),
        type_args(other.type_args),
        dims(other.dims),
        is_proc(other.is_proc),
        proc_return_mutability(other.proc_return_mutability),
        proc_params(other.proc_params),
        line(other.line),
        column(other.column) {
    if (other.proc_return) {
      proc_return = std::make_unique<TypeRef>(*other.proc_return);
    }
  }
  TypeRef& operator=(const TypeRef& other) {
    if (this == &other) return *this;
    name = other.name;
    pointer_depth = other.pointer_depth;
    type_args = other.type_args;
    dims = other.dims;
    is_proc = other.is_proc;
    proc_return_mutability = other.proc_return_mutability;
    proc_params = other.proc_params;
    line = other.line;
    column = other.column;
    if (other.proc_return) {
      proc_return = std::make_unique<TypeRef>(*other.proc_return);
    } else {
      proc_return.reset();
    }
    return *this;
  }
  TypeRef(TypeRef&&) noexcept = default;
  TypeRef& operator=(TypeRef&&) noexcept = default;
};

struct ParamDecl {
  std::string name;
  Mutability mutability = Mutability::Mutable;
  TypeRef type;
};

struct SwitchBranch;

enum class ExprKind : uint8_t {
  Identifier,
  Literal,
  FormatString,
  Binary,
  Unary,
  Call,
  Member,
  Index,
  ArrayLiteral,
  ListLiteral,
  ArtifactLiteral,
  FnLiteral,
  Switch,
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
  std::vector<SwitchBranch> switch_branches;
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
  bool has_loop_var_decl = false;
  VarDecl loop_var_decl;
};

struct SwitchBranch {
  bool is_default = false;
  bool is_block = false;
  bool has_inline_value = false;
  bool is_explicit_return = false;
  Expr condition;
  Expr value;
  std::vector<Stmt> block;
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
  bool is_data = false;
  std::vector<VarDecl> fields;
  std::vector<FuncDecl> methods;
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

struct ModuleHeaderDecl {
  std::string name;
};

struct ImportDecl {
  std::string path;
  std::string alias;
  bool has_alias = false;
  bool is_using = false;
};

struct ExternDecl {
  std::string name;
  std::string module;
  bool has_module = false;
  Mutability return_mutability = Mutability::Immutable;
  TypeRef return_type;
  std::vector<ParamDecl> params;
};

struct ModuleDecl {
  std::string name;
  std::string source_module;
  std::vector<VarDecl> variables;
  std::vector<FuncDecl> functions;
  std::vector<ExternDecl> externs;
};

enum class DeclKind : uint8_t {
  ModuleHeader,
  Import,
  Extern,
  Function,
  Variable,
  Artifact,
  Module,
  Enum,
};

struct Decl {
  DeclKind kind = DeclKind::Variable;
  ModuleHeaderDecl module_header;
  ImportDecl import_decl;
  ExternDecl ext;
  FuncDecl func;
  VarDecl var;
  ArtifactDecl artifact;
  ModuleDecl module;
  EnumDecl enm;
};

struct Program {
  std::vector<Decl> decls;
  std::vector<Stmt> top_level_stmts;
};

} // namespace Simple::Lang

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

enum class NormalizedSwitchBranchResultKind : uint8_t {
  None,
  InlineValue,
  SwitchBranchReturn,
  Block,
};

struct NormalizedBranchFlow {
  bool may_fallthrough = true;
  bool always_returns = false;
  bool may_break = false;
  bool may_skip = false;
};

struct NormalizedSwitchBranch {
  bool is_default = false;
  bool is_block = false;
  bool has_inline_value = false;
  bool is_explicit_return = false;
  bool falls_through_to_next = false;
  NormalizedSwitchBranchResultKind result_kind = NormalizedSwitchBranchResultKind::None;
  NormalizedBranchFlow flow;
  Expr condition;
  Expr value;
  std::vector<Stmt> block;
};

enum class NormalizedSwitchUsage : uint8_t {
  Expression,
  Statement,
  Assignment,
};

struct NormalizedSwitch {
  NormalizedSwitchUsage usage = NormalizedSwitchUsage::Expression;
  Expr scrutinee;
  std::vector<NormalizedSwitchBranch> branches;
};

enum class NormalizedExprShapeKind : uint8_t {
  Call,
  Member,
  Index,
};

struct NormalizedExprShape {
  NormalizedExprShapeKind kind = NormalizedExprShapeKind::Call;
  Expr base;
  std::string member;
  std::string op;
  std::vector<Expr> args;
  std::vector<TypeRef> type_args;
  Expr index;
};

struct NormalizedProgram {
  std::vector<Decl> decls;
  ScriptBody script_body;
  std::vector<NormalizedFnLiteralDecl> fn_literals;
  std::vector<NormalizedLoop> loops;
  std::vector<NormalizedIfChain> if_chains;
  std::vector<NormalizedSwitch> switches;
  std::vector<NormalizedExprShape> expr_shapes;
};

} // namespace Simple::Lang::AST
