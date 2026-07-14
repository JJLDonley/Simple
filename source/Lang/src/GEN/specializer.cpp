#include "GEN/specializer.h"

#include "TAST/type_checker.h"
#include "TAST/types.h"

#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Simple::Lang::GEN {
namespace {

uint64_t Fnv1a64(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char ch : value) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string Hex64(uint64_t value) {
  std::ostringstream out;
  out << std::hex << value;
  return out.str();
}

bool IsTypeIdentityTokenChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '_';
}

bool ContainsTypeParameterToken(const std::string& identity,
                                const std::vector<std::string>& type_params) {
  size_t pos = 0;
  while (pos < identity.size()) {
    while (pos < identity.size() && !IsTypeIdentityTokenChar(identity[pos])) ++pos;
    const size_t start = pos;
    while (pos < identity.size() && IsTypeIdentityTokenChar(identity[pos])) ++pos;
    if (start == pos) continue;
    const std::string token = identity.substr(start, pos - start);
    for (const std::string& param : type_params) {
      if (token == param) return true;
    }
  }
  return false;
}

std::string EscapeSymbolSegment(const std::string& value) {
  std::ostringstream out;
  for (const unsigned char ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '$') {
      out << static_cast<char>(ch);
    } else {
      out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<uint32_t>(ch) << std::nouppercase << std::dec;
    }
  }
  return out.str();
}

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

bool CollectFromExpr(const Simple::Lang::AST::Expr& expr,
                     std::vector<GenericInstantiationRequest>* out);
bool CollectFromStmt(const Simple::Lang::AST::Stmt& stmt,
                     std::vector<GenericInstantiationRequest>* out);

std::string CalleeName(const Simple::Lang::AST::Expr& expr) {
  if (expr.kind == Simple::Lang::AST::ExprKind::Identifier) return expr.text;
  if (expr.kind == Simple::Lang::AST::ExprKind::Member && !expr.children.empty()) {
    const std::string base = CalleeName(expr.children[0]);
    return base.empty() ? expr.text : base + "." + expr.text;
  }
  return expr.text;
}

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

bool CollectFromExpr(const Simple::Lang::AST::Expr& expr,
                     std::vector<GenericInstantiationRequest>* out) {
  if (!CollectFromTypeList(expr.type_args, out)) return false;
  if (expr.kind == Simple::Lang::AST::ExprKind::Call && !expr.type_args.empty() &&
      !expr.children.empty()) {
    GenericInstantiationRequest request;
    request.base_name = CalleeName(expr.children[0]);
    request.line = expr.line;
    request.column = expr.column;
    request.argument_identities.reserve(expr.type_args.size());
    request.argument_types.reserve(expr.type_args.size());
    for (const auto& arg : expr.type_args) {
      request.argument_identities.push_back(TypeRefIdentity(arg));
      request.argument_types.push_back(arg);
    }
    out->push_back(std::move(request));
  }
  for (const auto& child : expr.children) {
    if (!CollectFromExpr(child, out)) return false;
  }
  for (const auto& arg : expr.args) {
    if (!CollectFromExpr(arg, out)) return false;
  }
  for (const auto& value : expr.field_values) {
    if (!CollectFromExpr(value, out)) return false;
  }
  for (const auto& branch : expr.switch_branches) {
    if (!branch.is_default && !CollectFromExpr(branch.condition, out)) return false;
    if (branch.has_inline_value && !CollectFromExpr(branch.value, out)) return false;
    if (!CollectFromStmtList(branch.block, out)) return false;
  }
  return true;
}

bool CollectFromStmt(const Simple::Lang::AST::Stmt& stmt,
                     std::vector<GenericInstantiationRequest>* out) {
  using Simple::Lang::AST::StmtKind;
  switch (stmt.kind) {
    case StmtKind::VarDecl:
      return CollectInstantiationRequestsFromType(stmt.var_decl.type, out) &&
             (!stmt.var_decl.has_init_expr || CollectFromExpr(stmt.var_decl.init_expr, out));
    case StmtKind::IfStmt:
      return CollectFromExpr(stmt.if_cond, out) && CollectFromStmtList(stmt.if_then, out) &&
             CollectFromStmtList(stmt.if_else, out);
    case StmtKind::IfChain:
      for (const auto& branch : stmt.if_branches) {
        if (!CollectFromExpr(branch.first, out) || !CollectFromStmtList(branch.second, out)) return false;
      }
      return CollectFromStmtList(stmt.else_branch, out);
    case StmtKind::WhileLoop:
      return CollectFromExpr(stmt.loop_cond, out) && CollectFromStmtList(stmt.loop_body, out);
    case StmtKind::ForLoop:
      return (!stmt.has_loop_var_decl || CollectInstantiationRequestsFromType(stmt.loop_var_decl.type, out)) &&
             CollectFromExpr(stmt.loop_iter, out) && CollectFromExpr(stmt.loop_cond, out) &&
             CollectFromExpr(stmt.loop_step, out) && CollectFromStmtList(stmt.loop_body, out);
    case StmtKind::Return:
    case StmtKind::Expr:
      return CollectFromExpr(stmt.expr, out);
    case StmtKind::Assign:
      return CollectFromExpr(stmt.target, out) && CollectFromExpr(stmt.expr, out);
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

bool CollectRootInstantiationRequests(const Simple::Lang::AST::Program& program,
                                      std::vector<GenericInstantiationRequest>* out) {
  if (!out) return false;
  out->clear();
  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case Simple::Lang::AST::DeclKind::Variable:
        if (!CollectInstantiationRequestsFromType(decl.var.type, out)) return false;
        break;
      case Simple::Lang::AST::DeclKind::Extern:
        if (!CollectInstantiationRequestsFromType(decl.ext.return_type, out)) return false;
        for (const auto& param : decl.ext.params) {
          if (!CollectInstantiationRequestsFromType(param.type, out)) return false;
        }
        break;
      case Simple::Lang::AST::DeclKind::Function:
        if (decl.func.generics.empty() && !CollectFromFunction(decl.func, out)) return false;
        break;
      case Simple::Lang::AST::DeclKind::Artifact:
        if (!decl.artifact.generics.empty()) break;
        for (const auto& field : decl.artifact.fields) {
          if (!CollectInstantiationRequestsFromType(field.type, out)) return false;
        }
        for (const auto& method : decl.artifact.methods) {
          if (method.generics.empty() && !CollectFromFunction(method, out)) return false;
        }
        break;
      case Simple::Lang::AST::DeclKind::Module:
        for (const auto& var : decl.module.variables) {
          if (!CollectInstantiationRequestsFromType(var.type, out)) return false;
        }
        for (const auto& fn : decl.module.functions) {
          if (fn.generics.empty() && !CollectFromFunction(fn, out)) return false;
        }
        break;
      default:
        break;
    }
  }
  return CollectFromStmtList(program.top_level_stmts, out);
}

bool ApplySubstitutionToExpr(Simple::Lang::AST::Expr* expr,
                             const Simple::Lang::TAST::GenericSubstitutionMap& substitutions);
bool ApplySubstitutionToStmt(Simple::Lang::AST::Stmt* stmt,
                             const Simple::Lang::TAST::GenericSubstitutionMap& substitutions);

bool ApplySubstitutionToTypeList(std::vector<Simple::Lang::AST::TypeRef>* types,
                                 const Simple::Lang::TAST::GenericSubstitutionMap& substitutions) {
  if (!types) return false;
  for (auto& type : *types) {
    if (!Simple::Lang::TAST::ApplyTypeSubstitution(&type, substitutions)) return false;
  }
  return true;
}

bool ApplySubstitutionToExpr(Simple::Lang::AST::Expr* expr,
                             const Simple::Lang::TAST::GenericSubstitutionMap& substitutions) {
  if (!expr) return false;
  if (!ApplySubstitutionToTypeList(&expr->type_args, substitutions)) return false;
  for (auto& param : expr->fn_params) {
    if (!Simple::Lang::TAST::ApplyTypeSubstitution(&param.type, substitutions)) return false;
  }
  for (auto& child : expr->children) {
    if (!ApplySubstitutionToExpr(&child, substitutions)) return false;
  }
  for (auto& arg : expr->args) {
    if (!ApplySubstitutionToExpr(&arg, substitutions)) return false;
  }
  for (auto& value : expr->field_values) {
    if (!ApplySubstitutionToExpr(&value, substitutions)) return false;
  }
  for (auto& branch : expr->switch_branches) {
    if (!branch.is_default && !ApplySubstitutionToExpr(&branch.condition, substitutions)) return false;
    if (branch.has_inline_value && !ApplySubstitutionToExpr(&branch.value, substitutions)) return false;
    for (auto& stmt : branch.block) {
      if (!ApplySubstitutionToStmt(&stmt, substitutions)) return false;
    }
  }
  return true;
}

bool ApplySubstitutionToVar(Simple::Lang::AST::VarDecl* var,
                            const Simple::Lang::TAST::GenericSubstitutionMap& substitutions) {
  if (!var) return false;
  if (!Simple::Lang::TAST::ApplyTypeSubstitution(&var->type, substitutions)) return false;
  return !var->has_init_expr || ApplySubstitutionToExpr(&var->init_expr, substitutions);
}

bool ApplySubstitutionToStmt(Simple::Lang::AST::Stmt* stmt,
                             const Simple::Lang::TAST::GenericSubstitutionMap& substitutions) {
  if (!stmt) return false;
  if (!ApplySubstitutionToExpr(&stmt->expr, substitutions)) return false;
  if (!ApplySubstitutionToExpr(&stmt->target, substitutions)) return false;
  if (!ApplySubstitutionToVar(&stmt->var_decl, substitutions)) return false;
  for (auto& branch : stmt->if_branches) {
    if (!ApplySubstitutionToExpr(&branch.first, substitutions)) return false;
    for (auto& nested : branch.second) {
      if (!ApplySubstitutionToStmt(&nested, substitutions)) return false;
    }
  }
  for (auto& nested : stmt->else_branch) {
    if (!ApplySubstitutionToStmt(&nested, substitutions)) return false;
  }
  if (!ApplySubstitutionToExpr(&stmt->if_cond, substitutions)) return false;
  for (auto& nested : stmt->if_then) {
    if (!ApplySubstitutionToStmt(&nested, substitutions)) return false;
  }
  for (auto& nested : stmt->if_else) {
    if (!ApplySubstitutionToStmt(&nested, substitutions)) return false;
  }
  if (!ApplySubstitutionToExpr(&stmt->loop_cond, substitutions)) return false;
  for (auto& nested : stmt->loop_body) {
    if (!ApplySubstitutionToStmt(&nested, substitutions)) return false;
  }
  if (!ApplySubstitutionToExpr(&stmt->loop_iter, substitutions)) return false;
  if (!ApplySubstitutionToExpr(&stmt->loop_step, substitutions)) return false;
  return !stmt->has_loop_var_decl || ApplySubstitutionToVar(&stmt->loop_var_decl, substitutions);
}

bool ApplySubstitutionToFunction(Simple::Lang::AST::FuncDecl* fn,
                                 const Simple::Lang::TAST::GenericSubstitutionMap& substitutions) {
  if (!fn) return false;
  if (!Simple::Lang::TAST::ApplyTypeSubstitution(&fn->return_type, substitutions)) return false;
  for (auto& param : fn->params) {
    if (!Simple::Lang::TAST::ApplyTypeSubstitution(&param.type, substitutions)) return false;
  }
  for (auto& stmt : fn->body) {
    if (!ApplySubstitutionToStmt(&stmt, substitutions)) return false;
  }
  return true;
}

bool ArtifactHasGenericMethods(const Simple::Lang::AST::ArtifactDecl& artifact) {
  for (const auto& method : artifact.methods) {
    if (!method.generics.empty()) return true;
  }
  return false;
}

bool IsConcreteDeclForMaterialization(const Simple::Lang::AST::Decl& decl) {
  switch (decl.kind) {
    case Simple::Lang::AST::DeclKind::Function:
      return decl.func.generics.empty();
    case Simple::Lang::AST::DeclKind::Artifact:
      return decl.artifact.generics.empty() && !ArtifactHasGenericMethods(decl.artifact);
    default:
      return true;
  }
}

const std::string* TopLevelDeclName(const Simple::Lang::AST::Decl& decl) {
  switch (decl.kind) {
    case Simple::Lang::AST::DeclKind::ModuleHeader:
      return &decl.module_header.name;
    case Simple::Lang::AST::DeclKind::Extern:
      return &decl.ext.name;
    case Simple::Lang::AST::DeclKind::Function:
      return &decl.func.name;
    case Simple::Lang::AST::DeclKind::Variable:
      return &decl.var.name;
    case Simple::Lang::AST::DeclKind::Artifact:
      return &decl.artifact.name;
    case Simple::Lang::AST::DeclKind::Module:
      return &decl.module.name;
    case Simple::Lang::AST::DeclKind::Enum:
      return &decl.enm.name;
    case Simple::Lang::AST::DeclKind::Import:
      return nullptr;
  }
  return nullptr;
}

bool ValidateMaterializedTopLevelNames(const Simple::Lang::AST::Program& program,
                                       std::string* error) {
  std::unordered_set<std::string> names;
  for (const auto& decl : program.decls) {
    const std::string* name = TopLevelDeclName(decl);
    if (!name || name->empty()) continue;
    if (!names.insert(*name).second) {
      if (error) *error = "duplicate top-level declaration: " + *name;
      return false;
    }
  }
  return true;
}

const Simple::Lang::AST::FuncDecl* FindFunctionDecl(const Simple::Lang::AST::Program& program,
                                                    const std::string& name) {
  for (const auto& decl : program.decls) {
    if (decl.kind == Simple::Lang::AST::DeclKind::Function && decl.func.name == name) {
      return &decl.func;
    }
  }
  return nullptr;
}

const Simple::Lang::AST::ArtifactDecl* FindArtifactDecl(const Simple::Lang::AST::Program& program,
                                                        const std::string& name) {
  for (const auto& decl : program.decls) {
    if (decl.kind == Simple::Lang::AST::DeclKind::Artifact && decl.artifact.name == name) {
      return &decl.artifact;
    }
  }
  return nullptr;
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
    request.argument_types.reserve(type.type_args.size());
    for (const auto& arg : type.type_args) {
      request.argument_identities.push_back(TypeRefIdentity(arg));
      request.argument_types.push_back(arg);
    }
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
      case Simple::Lang::AST::DeclKind::Extern:
        if (!CollectInstantiationRequestsFromType(decl.ext.return_type, out)) return false;
        for (const auto& param : decl.ext.params) {
          if (!CollectInstantiationRequestsFromType(param.type, out)) return false;
        }
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

std::string InstantiationRequestKey(const GenericInstantiationRequest& request) {
  std::string key = request.base_name + "<";
  for (size_t i = 0; i < request.argument_identities.size(); ++i) {
    if (i != 0) key += ",";
    key += request.argument_identities[i];
  }
  key += ">";
  return key;
}

std::string SpecializedSymbolName(const GenericInstantiationRequest& request) {
  const std::string key = InstantiationRequestKey(request);
  return EscapeSymbolSegment(request.base_name) + "__g_" + Hex64(Fnv1a64(key));
}

bool NormalizeInstantiationRequests(const std::vector<GenericInstantiationRequest>& requests,
                                    std::vector<GenericInstantiationRequest>* unique_requests) {
  if (!unique_requests) return false;
  unique_requests->clear();
  std::unordered_map<std::string, size_t> by_key;
  for (const auto& request : requests) {
    const std::string key = InstantiationRequestKey(request);
    auto [it, inserted] = by_key.emplace(key, unique_requests->size());
    if (inserted) {
      unique_requests->push_back(request);
      continue;
    }
    GenericInstantiationRequest& existing = (*unique_requests)[it->second];
    if (existing.argument_types.empty() && !request.argument_types.empty()) {
      existing.argument_types = request.argument_types;
      continue;
    }
    if (!existing.argument_types.empty() && !request.argument_types.empty()) {
      if (existing.argument_types.size() != request.argument_types.size()) return false;
      for (size_t i = 0; i < existing.argument_types.size(); ++i) {
        if (!Simple::Lang::TAST::TypeEquals(existing.argument_types[i], request.argument_types[i])) {
          return false;
        }
      }
    }
  }
  return true;
}

bool ResolveInstantiationOrder(const std::vector<GenericInstantiationNode>& nodes,
                               std::vector<GenericInstantiationRequest>* ordered_requests,
                               std::string* error) {
  if (!ordered_requests) return false;
  ordered_requests->clear();
  enum class VisitState : uint8_t { Unvisited, Visiting, Done };
  std::unordered_map<std::string, const GenericInstantiationNode*> by_key;
  std::unordered_map<std::string, VisitState> states;
  for (const auto& node : nodes) {
    const std::string key = InstantiationRequestKey(node.request);
    if (!by_key.emplace(key, &node).second) {
      if (error) *error = "duplicate instantiation node: " + key;
      return false;
    }
    states.emplace(key, VisitState::Unvisited);
  }

  auto visit = [&](auto&& self, const GenericInstantiationRequest& request) -> bool {
    const std::string key = InstantiationRequestKey(request);
    auto state_it = states.find(key);
    if (state_it == states.end()) {
      if (error) *error = "missing instantiation dependency: " + key;
      return false;
    }
    if (state_it->second == VisitState::Visiting) {
      if (error) *error = "generic instantiation cycle at " + key;
      return false;
    }
    if (state_it->second == VisitState::Done) return true;
    state_it->second = VisitState::Visiting;
    const GenericInstantiationNode* node = by_key[key];
    for (const auto& dependency : node->dependencies) {
      if (!self(self, dependency)) return false;
    }
    state_it->second = VisitState::Done;
    ordered_requests->push_back(node->request);
    return true;
  };

  for (const auto& node : nodes) {
    if (!visit(visit, node.request)) return false;
  }
  if (error) error->clear();
  return true;
}

bool BuildSpecializationPlan(const std::vector<Simple::Lang::TAST::GenericDeclarationMetadata>& declarations,
                             const std::vector<GenericInstantiationRequest>& requests,
                             std::vector<GenericSpecializationPlan>* out,
                             std::string* error) {
  if (!out) return false;
  out->clear();
  std::unordered_map<std::string, const Simple::Lang::TAST::GenericDeclarationMetadata*> by_name;
  for (const auto& declaration : declarations) {
    const std::string key = declaration.owner_name.empty()
        ? declaration.name
        : declaration.owner_name + "." + declaration.name;
    if (!by_name.emplace(key, &declaration).second) {
      if (error) *error = "duplicate generic declaration: " + key;
      return false;
    }
  }
  for (const auto& request : requests) {
    auto it = by_name.find(request.base_name);
    if (it == by_name.end()) {
      if (error) *error = "missing generic declaration: " + request.base_name;
      return false;
    }
    if (it->second->type_params.size() != request.argument_identities.size()) {
      if (error) {
        *error = "generic specialization argument count mismatch for " + request.base_name;
      }
      return false;
    }
    if (!request.argument_types.empty() &&
        request.argument_types.size() != request.argument_identities.size()) {
      if (error) {
        *error = "generic specialization type metadata mismatch for " + request.base_name;
      }
      return false;
    }
    for (const std::string& argument : request.argument_identities) {
      if (ContainsTypeParameterToken(argument, it->second->type_params)) {
        if (error) {
          *error = "generic specialization is not concrete for " + request.base_name;
        }
        return false;
      }
    }
    GenericSpecializationPlan plan;
    plan.request = request;
    plan.declaration = *it->second;
    plan.specialized_symbol = SpecializedSymbolName(request);
    plan.bindings.reserve(it->second->type_params.size());
    for (size_t i = 0; i < it->second->type_params.size(); ++i) {
      GenericSpecializationBinding binding;
      binding.parameter_name = it->second->type_params[i];
      binding.type_identity = request.argument_identities[i];
      if (!request.argument_types.empty()) {
        binding.has_concrete_type = true;
        binding.concrete_type = request.argument_types[i];
      }
      plan.bindings.push_back(std::move(binding));
    }
    out->push_back(std::move(plan));
  }
  std::unordered_map<std::string, std::string> symbol_to_key;
  for (const auto& plan : *out) {
    const std::string request_key = InstantiationRequestKey(plan.request);
    const auto [it, inserted] = symbol_to_key.emplace(plan.specialized_symbol, request_key);
    if (!inserted && it->second != request_key) {
      if (error) {
        *error = "generic specialization symbol collision for " + plan.specialized_symbol;
      }
      return false;
    }
  }
  if (error) error->clear();
  return true;
}

bool BuildSpecializationPlanFromProgram(const Simple::Lang::AST::Program& program,
                                        std::vector<GenericSpecializationPlan>* out,
                                        std::string* error) {
  if (!out) return false;
  std::vector<Simple::Lang::TAST::GenericDeclarationMetadata> declarations;
  if (!Simple::Lang::TAST::CollectGenericDeclarationMetadata(program, &declarations, error)) {
    return false;
  }
  std::unordered_set<std::string> declared_generics;
  for (const auto& declaration : declarations) {
    declared_generics.insert(declaration.owner_name.empty()
                                 ? declaration.name
                                 : declaration.owner_name + "." + declaration.name);
  }

  auto append_declared_requests =
      [&](const std::vector<GenericInstantiationRequest>& candidates,
          std::vector<GenericInstantiationRequest>* requests) {
        for (const auto& request : candidates) {
          if (declared_generics.find(request.base_name) != declared_generics.end()) {
            requests->push_back(request);
          }
        }
      };

  std::vector<GenericInstantiationRequest> collected;
  if (!CollectRootInstantiationRequests(program, &collected)) return false;
  std::vector<GenericInstantiationRequest> requests;
  append_declared_requests(collected, &requests);

  constexpr size_t kMaximumSpecializations = 4096;
  for (;;) {
    std::vector<GenericInstantiationRequest> unique_requests;
    if (!NormalizeInstantiationRequests(requests, &unique_requests)) {
      if (error) *error = "conflicting generic instantiation metadata";
      return false;
    }
    if (unique_requests.size() > kMaximumSpecializations) {
      if (error) *error = "generic specialization expansion exceeds safety limit";
      return false;
    }
    std::vector<GenericSpecializationPlan> plans;
    if (!BuildSpecializationPlan(declarations, unique_requests, &plans, error)) return false;

    const size_t previous_count = unique_requests.size();
    std::vector<GenericInstantiationRequest> dependencies;
    for (const auto& plan : plans) {
      switch (plan.declaration.kind) {
        case Simple::Lang::TAST::GenericDeclarationKind::Function: {
          const auto* source = FindFunctionDecl(program, plan.declaration.name);
          if (!source) {
            if (error) *error = "missing generic function declaration: " + plan.declaration.name;
            return false;
          }
          Simple::Lang::AST::FuncDecl specialized;
          if (!SpecializeFunctionDeclaration(*source, plan, &specialized, error) ||
              !CollectFromFunction(specialized, &dependencies)) {
            return false;
          }
          break;
        }
        case Simple::Lang::TAST::GenericDeclarationKind::Artifact:
        case Simple::Lang::TAST::GenericDeclarationKind::Data: {
          const auto* source = FindArtifactDecl(program, plan.declaration.name);
          if (!source) {
            if (error) *error = "missing generic artifact declaration: " + plan.declaration.name;
            return false;
          }
          Simple::Lang::AST::ArtifactDecl specialized;
          if (!SpecializeArtifactLayoutDeclaration(*source, plan, &specialized, error)) {
            return false;
          }
          for (const auto& field : specialized.fields) {
            if (!CollectInstantiationRequestsFromType(field.type, &dependencies)) return false;
          }
          for (const auto& method : specialized.methods) {
            if (!CollectFromFunction(method, &dependencies)) return false;
          }
          break;
        }
        case Simple::Lang::TAST::GenericDeclarationKind::Method:
          if (error) *error = "generic method specialization requires receiver specialization";
          return false;
      }
    }
    requests = std::move(unique_requests);
    append_declared_requests(dependencies, &requests);
    std::vector<GenericInstantiationRequest> expanded;
    if (!NormalizeInstantiationRequests(requests, &expanded)) {
      if (error) *error = "conflicting generic dependency metadata";
      return false;
    }
    if (expanded.size() == previous_count) {
      return BuildSpecializationPlan(declarations, expanded, out, error);
    }
    requests = std::move(expanded);
  }
}

bool BuildOrderedSpecializationPlan(
    const std::vector<Simple::Lang::TAST::GenericDeclarationMetadata>& declarations,
    const std::vector<GenericInstantiationNode>& nodes,
    std::vector<GenericSpecializationPlan>* out,
    std::string* error) {
  std::vector<GenericInstantiationRequest> ordered;
  if (!ResolveInstantiationOrder(nodes, &ordered, error)) return false;
  return BuildSpecializationPlan(declarations, ordered, out, error);
}

bool BuildGenericSubstitutionMap(const GenericSpecializationPlan& plan,
                                 Simple::Lang::TAST::GenericSubstitutionMap* out,
                                 std::string* error) {
  if (!out) return false;
  out->clear();
  for (const auto& binding : plan.bindings) {
    if (!binding.has_concrete_type) {
      if (error) {
        *error = "generic specialization missing concrete type for " + binding.parameter_name;
      }
      return false;
    }
    auto [it, inserted] = out->emplace(binding.parameter_name, binding.concrete_type);
    if (!inserted && !Simple::Lang::TAST::TypeEquals(it->second, binding.concrete_type)) {
      if (error) {
        *error = "generic specialization conflicting binding for " + binding.parameter_name;
      }
      return false;
    }
  }
  if (error) error->clear();
  return true;
}

bool SpecializeFunctionDeclaration(const Simple::Lang::AST::FuncDecl& source,
                                   const GenericSpecializationPlan& plan,
                                   Simple::Lang::AST::FuncDecl* out,
                                   std::string* error) {
  if (!out) return false;
  if (plan.declaration.kind != Simple::Lang::TAST::GenericDeclarationKind::Function) {
    if (error) *error = "generic specialization plan is not a function";
    return false;
  }
  if (plan.declaration.name != source.name) {
    if (error) *error = "generic specialization declaration mismatch for " + source.name;
    return false;
  }
  Simple::Lang::TAST::GenericSubstitutionMap substitutions;
  if (!BuildGenericSubstitutionMap(plan, &substitutions, error)) return false;
  *out = source;
  out->name = plan.specialized_symbol;
  out->generics.clear();
  if (!ApplySubstitutionToFunction(out, substitutions)) return false;
  if (error) error->clear();
  return true;
}

bool SpecializeArtifactLayoutDeclaration(const Simple::Lang::AST::ArtifactDecl& source,
                                         const GenericSpecializationPlan& plan,
                                         Simple::Lang::AST::ArtifactDecl* out,
                                         std::string* error) {
  if (!out) return false;
  if (plan.declaration.kind != Simple::Lang::TAST::GenericDeclarationKind::Artifact &&
      plan.declaration.kind != Simple::Lang::TAST::GenericDeclarationKind::Data) {
    if (error) *error = "generic specialization plan is not an artifact";
    return false;
  }
  if (plan.declaration.name != source.name) {
    if (error) *error = "generic specialization declaration mismatch for " + source.name;
    return false;
  }
  Simple::Lang::TAST::GenericSubstitutionMap substitutions;
  if (!BuildGenericSubstitutionMap(plan, &substitutions, error)) return false;
  *out = source;
  out->name = plan.specialized_symbol;
  out->generics.clear();
  for (auto& field : out->fields) {
    if (!ApplySubstitutionToVar(&field, substitutions)) return false;
  }
  for (auto& method : out->methods) {
    if (!ApplySubstitutionToFunction(&method, substitutions)) return false;
  }
  if (error) error->clear();
  return true;
}

bool MaterializeConcreteProgram(const Simple::Lang::AST::Program& source,
                                const std::vector<GenericSpecializationPlan>& plans,
                                Simple::Lang::AST::Program* out,
                                std::string* error) {
  if (!out) return false;
  out->decls.clear();
  out->top_level_stmts = source.top_level_stmts;
  for (const auto& decl : source.decls) {
    if (IsConcreteDeclForMaterialization(decl)) out->decls.push_back(decl);
  }
  for (const auto& plan : plans) {
    Simple::Lang::AST::Decl decl;
    switch (plan.declaration.kind) {
      case Simple::Lang::TAST::GenericDeclarationKind::Function: {
        const auto* source_fn = FindFunctionDecl(source, plan.declaration.name);
        if (!source_fn) {
          if (error) *error = "missing generic function declaration: " + plan.declaration.name;
          return false;
        }
        decl.kind = Simple::Lang::AST::DeclKind::Function;
        if (!SpecializeFunctionDeclaration(*source_fn, plan, &decl.func, error)) return false;
        out->decls.push_back(std::move(decl));
        break;
      }
      case Simple::Lang::TAST::GenericDeclarationKind::Artifact:
      case Simple::Lang::TAST::GenericDeclarationKind::Data: {
        const auto* source_artifact = FindArtifactDecl(source, plan.declaration.name);
        if (!source_artifact) {
          if (error) *error = "missing generic artifact declaration: " + plan.declaration.name;
          return false;
        }
        decl.kind = Simple::Lang::AST::DeclKind::Artifact;
        if (!SpecializeArtifactLayoutDeclaration(*source_artifact, plan, &decl.artifact, error)) {
          return false;
        }
        out->decls.push_back(std::move(decl));
        break;
      }
      case Simple::Lang::TAST::GenericDeclarationKind::Method:
        if (error) *error = "generic method materialization requires receiver specialization";
        return false;
    }
  }
  std::unordered_map<std::string, std::string> specialized_symbols;
  for (const auto& plan : plans) {
    specialized_symbols.emplace(InstantiationRequestKey(plan.request), plan.specialized_symbol);
  }

  auto build_key = [](const std::string& base,
                      const std::vector<Simple::Lang::AST::TypeRef>& args) -> std::string {
    std::string key = base + "<";
    for (size_t i = 0; i < args.size(); ++i) {
      if (i != 0) key += ",";
      key += TypeRefIdentity(args[i]);
    }
    key += ">";
    return key;
  };

  auto rewrite_type = [&](auto&& self, Simple::Lang::AST::TypeRef* type) -> bool {
    if (!type) return false;
    for (auto& arg : type->type_args) {
      if (!self(self, &arg)) return false;
    }
    if (type->is_proc) {
      for (auto& param : type->proc_params) {
        if (!self(self, &param)) return false;
      }
      if (type->proc_return && !self(self, type->proc_return.get())) return false;
    }
    if (!type->name.empty() && !type->type_args.empty() && type->pointer_depth == 0 && type->dims.empty()) {
      const auto it = specialized_symbols.find(build_key(type->name, type->type_args));
      if (it != specialized_symbols.end()) {
        type->name = it->second;
        type->type_args.clear();
      }
    }
    return true;
  };

  auto rewrite_var = [&](auto&& rewrite_expr, auto&& rewrite_type_fn,
                         Simple::Lang::AST::VarDecl* var) -> bool {
    if (!var) return false;
    if (!rewrite_type_fn(rewrite_type_fn, &var->type)) return false;
    return !var->has_init_expr || rewrite_expr(rewrite_expr, rewrite_type_fn, &var->init_expr);
  };

  auto rewrite_stmt = [&](auto&& self, auto&& rewrite_expr, auto&& rewrite_type_fn,
                          Simple::Lang::AST::Stmt* stmt) -> bool {
    if (!stmt) return false;
    if (!rewrite_expr(rewrite_expr, rewrite_type_fn, &stmt->expr)) return false;
    if (!rewrite_expr(rewrite_expr, rewrite_type_fn, &stmt->target)) return false;
    if (!rewrite_var(rewrite_expr, rewrite_type_fn, &stmt->var_decl)) return false;
    for (auto& branch : stmt->if_branches) {
      if (!rewrite_expr(rewrite_expr, rewrite_type_fn, &branch.first)) return false;
      for (auto& nested : branch.second) {
        if (!self(self, rewrite_expr, rewrite_type_fn, &nested)) return false;
      }
    }
    for (auto& nested : stmt->else_branch) {
      if (!self(self, rewrite_expr, rewrite_type_fn, &nested)) return false;
    }
    if (!rewrite_expr(rewrite_expr, rewrite_type_fn, &stmt->if_cond)) return false;
    for (auto& nested : stmt->if_then) {
      if (!self(self, rewrite_expr, rewrite_type_fn, &nested)) return false;
    }
    for (auto& nested : stmt->if_else) {
      if (!self(self, rewrite_expr, rewrite_type_fn, &nested)) return false;
    }
    if (!rewrite_expr(rewrite_expr, rewrite_type_fn, &stmt->loop_cond)) return false;
    for (auto& nested : stmt->loop_body) {
      if (!self(self, rewrite_expr, rewrite_type_fn, &nested)) return false;
    }
    if (!rewrite_expr(rewrite_expr, rewrite_type_fn, &stmt->loop_iter)) return false;
    if (!rewrite_expr(rewrite_expr, rewrite_type_fn, &stmt->loop_step)) return false;
    return !stmt->has_loop_var_decl || rewrite_var(rewrite_expr, rewrite_type_fn, &stmt->loop_var_decl);
  };

  auto rewrite_expr = [&](auto&& self, auto&& rewrite_type_fn,
                          Simple::Lang::AST::Expr* expr) -> bool {
    if (!expr) return false;
    for (auto& arg : expr->type_args) {
      if (!rewrite_type_fn(rewrite_type_fn, &arg)) return false;
    }
    if (expr->kind == Simple::Lang::AST::ExprKind::Call && !expr->children.empty()) {
      const std::string callee = CalleeName(expr->children[0]);
      if (!expr->type_args.empty()) {
        const auto it = specialized_symbols.find(build_key(callee, expr->type_args));
        if (it != specialized_symbols.end()) {
          expr->children[0].text = it->second;
          expr->type_args.clear();
        }
      }
    }
    for (auto& param : expr->fn_params) {
      if (!rewrite_type_fn(rewrite_type_fn, &param.type)) return false;
    }
    for (auto& child : expr->children) {
      if (!self(self, rewrite_type_fn, &child)) return false;
    }
    for (auto& arg : expr->args) {
      if (!self(self, rewrite_type_fn, &arg)) return false;
    }
    for (auto& value : expr->field_values) {
      if (!self(self, rewrite_type_fn, &value)) return false;
    }
    for (auto& branch : expr->switch_branches) {
      if (!branch.is_default && !self(self, rewrite_type_fn, &branch.condition)) return false;
      if (branch.has_inline_value && !self(self, rewrite_type_fn, &branch.value)) return false;
      for (auto& stmt : branch.block) {
        if (!rewrite_stmt(rewrite_stmt, self, rewrite_type_fn, &stmt)) return false;
      }
    }
    return true;
  };

  auto rewrite_function = [&](Simple::Lang::AST::FuncDecl* fn) -> bool {
    if (!fn) return false;
    if (!rewrite_type(rewrite_type, &fn->return_type)) return false;
    for (auto& param : fn->params) {
      if (!rewrite_type(rewrite_type, &param.type)) return false;
    }
    for (auto& stmt : fn->body) {
      if (!rewrite_stmt(rewrite_stmt, rewrite_expr, rewrite_type, &stmt)) return false;
    }
    return true;
  };

  for (auto& decl : out->decls) {
    switch (decl.kind) {
      case Simple::Lang::AST::DeclKind::Extern:
        if (!rewrite_type(rewrite_type, &decl.ext.return_type)) return false;
        for (auto& param : decl.ext.params) {
          if (!rewrite_type(rewrite_type, &param.type)) return false;
        }
        break;
      case Simple::Lang::AST::DeclKind::Function:
        if (!rewrite_function(&decl.func)) return false;
        break;
      case Simple::Lang::AST::DeclKind::Variable:
        if (!rewrite_var(rewrite_expr, rewrite_type, &decl.var)) return false;
        break;
      case Simple::Lang::AST::DeclKind::Artifact:
        for (auto& field : decl.artifact.fields) {
          if (!rewrite_var(rewrite_expr, rewrite_type, &field)) return false;
        }
        for (auto& method : decl.artifact.methods) {
          if (!rewrite_function(&method)) return false;
        }
        break;
      case Simple::Lang::AST::DeclKind::Module:
        for (auto& var : decl.module.variables) {
          if (!rewrite_var(rewrite_expr, rewrite_type, &var)) return false;
        }
        for (auto& fn : decl.module.functions) {
          if (!rewrite_function(&fn)) return false;
        }
        break;
      default:
        break;
    }
  }
  for (auto& stmt : out->top_level_stmts) {
    if (!rewrite_stmt(rewrite_stmt, rewrite_expr, rewrite_type, &stmt)) return false;
  }
  if (!ValidateMaterializedTopLevelNames(*out, error)) return false;
  if (error) error->clear();
  return true;
}

bool MaterializeProgramForEmission(const Simple::Lang::AST::Program& source,
                                   Simple::Lang::AST::Program* out,
                                   bool* materialized,
                                   std::string* error) {
  if (!out || !materialized) return false;
  std::vector<Simple::Lang::TAST::GenericDeclarationMetadata> declarations;
  if (!Simple::Lang::TAST::CollectGenericDeclarationMetadata(source, &declarations, error)) {
    return false;
  }
  if (declarations.empty()) {
    *materialized = false;
    if (error) error->clear();
    return true;
  }
  Simple::Lang::AST::Program annotated = source;
  if (!Simple::Lang::TAST::AnnotateInferredGenericCallTypeArguments(&annotated, error)) {
    return false;
  }
  std::vector<GenericSpecializationPlan> plans;
  if (!BuildSpecializationPlanFromProgram(annotated, &plans, error)) return false;
  if (!MaterializeConcreteProgram(annotated, plans, out, error)) return false;
  *materialized = true;
  if (error) error->clear();
  return true;
}

} // namespace Simple::Lang::GEN
