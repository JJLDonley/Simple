#pragma once

// Phase-0 CAST facade.
//
// The current parser still produces Simple::Lang::Program from lang_ast.h.
// During the staged split this header is the canonical include point for the
// concrete parser tree. The concrete and normalized trees are aliases until
// the parser and AST normalizer are separated.

#include "lang_ast.h"

namespace Simple::Lang::CAST {

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

} // namespace Simple::Lang::CAST
