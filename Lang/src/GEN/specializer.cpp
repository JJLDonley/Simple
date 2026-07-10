#include "GEN/specializer.h"

#include <utility>

namespace Simple::Lang::GEN {
namespace {

std::string JoinTypeArgs(const std::vector<Simple::Lang::AST::TypeRef>& args) {
  std::string out;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) out += ",";
    out += TypeRefIdentity(args[i]);
  }
  return out;
}

void AppendDims(const Simple::Lang::AST::TypeRef& type, std::string* out) {
  if (!out) return;
  for (const auto& dim : type.dims) {
    if (dim.is_list) {
      *out += "[]";
    } else if (dim.has_size) {
      *out += "[" + std::to_string(dim.size) + "]";
    } else {
      *out += "[?]";
    }
  }
}

bool CollectFromStmt(const Simple::Lang::AST::Stmt& stmt,
                     std::vector<GenericInstantiationRequest>* out);

bool CollectFromTypeList(const std::vector<Simple::Lang::AST::TypeRef>& types,
                         std::vector<GenericInstantiationRequest>* out) {
  for (const auto& type : types) {
    if (!CollectInstantiationRequestsFromType(type, out)) return false;
  }
  return true;
}

bool CollectFromStmtList(const std::vector<Simple::Lang::AST::Stmt>& stmts,
                         std::vector<GenericInstantiationRequest>* out) {
  for (const auto& stmt : stmts) {
    if (!CollectFromStmt(stmt, out)) return false;
  }
  return true;
}

bool CollectFromStmt(const Simple::Lang::AST::Stmt& stmt,
                     std::vector<GenericInstantiationRequest>* out) {
  using Simple::Lang::AST::StmtKind;
  switch (stmt.kind) {
    case StmtKind::VarDecl:
      return CollectInstantiationRequestsFromType(stmt.var_decl.type, out);
    case StmtKind::IfStmt:
      return CollectFromStmtList(stmt.if_then, out) && CollectFromStmtList(stmt.if_else, out);
    case StmtKind::IfChain:
      for (const auto& branch : stmt.if_branches) {
        if (!CollectFromStmtList(branch.second, out)) return false;
      }
      return CollectFromStmtList(stmt.else_branch, out);
    case StmtKind::WhileLoop:
      return CollectFromStmtList(stmt.loop_body, out);
    case StmtKind::ForLoop:
      return (!stmt.has_loop_var_decl || CollectInstantiationRequestsFromType(stmt.loop_var_decl.type, out)) &&
             CollectFromStmtList(stmt.loop_body, out);
    default:
      return true;
  }
}

bool CollectFromFunction(const Simple::Lang::AST::FuncDecl& fn,
                         std::vector<GenericInstantiationRequest>* out) {
  if (!CollectInstantiationRequestsFromType(fn.return_type, out)) return false;
  for (const auto& param : fn.params) {
    if (!CollectInstantiationRequestsFromType(param.type, out)) return false;
  }
  return CollectFromStmtList(fn.body, out);
}

} // namespace

std::string TypeRefIdentity(const Simple::Lang::AST::TypeRef& type) {
  if (type.is_proc) {
    std::string out = "fn(";
    for (size_t i = 0; i < type.proc_params.size(); ++i) {
      if (i != 0) out += ",";
      out += TypeRefIdentity(type.proc_params[i]);
    }
    out += ")->";
    out += type.proc_return ? TypeRefIdentity(*type.proc_return) : "void";
    AppendDims(type, &out);
    return out;
  }
  std::string out;
  for (uint32_t i = 0; i < type.pointer_depth; ++i) out += "ptr<";
  out += type.name;
  if (!type.type_args.empty()) {
    out += "<";
    out += JoinTypeArgs(type.type_args);
    out += ">";
  }
  for (uint32_t i = 0; i < type.pointer_depth; ++i) out += ">";
  AppendDims(type, &out);
  return out;
}

bool CollectInstantiationRequestsFromType(const Simple::Lang::AST::TypeRef& type,
                                          std::vector<GenericInstantiationRequest>* out) {
  if (!out) return false;
  for (const auto& arg : type.type_args) {
    if (!CollectInstantiationRequestsFromType(arg, out)) return false;
  }
  if (type.is_proc && !CollectFromTypeList(type.proc_params, out)) return false;
  if (type.proc_return && !CollectInstantiationRequestsFromType(*type.proc_return, out)) return false;
  if (!type.name.empty() && !type.type_args.empty()) {
    GenericInstantiationRequest request;
    request.base_name = type.name;
    request.line = type.line;
    request.column = type.column;
    request.argument_identities.reserve(type.type_args.size());
    for (const auto& arg : type.type_args) request.argument_identities.push_back(TypeRefIdentity(arg));
    out->push_back(std::move(request));
  }
  return true;
}

bool CollectInstantiationRequestsFromProgram(const Simple::Lang::AST::Program& program,
                                             std::vector<GenericInstantiationRequest>* out) {
  if (!out) return false;
  out->clear();
  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case Simple::Lang::AST::DeclKind::Variable:
        if (!CollectInstantiationRequestsFromType(decl.var.type, out)) return false;
        break;
      case Simple::Lang::AST::DeclKind::Function:
        if (!CollectFromFunction(decl.func, out)) return false;
        break;
      case Simple::Lang::AST::DeclKind::Artifact:
        for (const auto& field : decl.artifact.fields) {
          if (!CollectInstantiationRequestsFromType(field.type, out)) return false;
        }
        for (const auto& method : decl.artifact.methods) {
          if (!CollectFromFunction(method, out)) return false;
        }
        break;
      case Simple::Lang::AST::DeclKind::Module:
        for (const auto& var : decl.module.variables) {
          if (!CollectInstantiationRequestsFromType(var.type, out)) return false;
        }
        for (const auto& fn : decl.module.functions) {
          if (!CollectFromFunction(fn, out)) return false;
        }
        break;
      default:
        break;
    }
  }
  return CollectFromStmtList(program.top_level_stmts, out);
}

} // namespace Simple::Lang::GEN
