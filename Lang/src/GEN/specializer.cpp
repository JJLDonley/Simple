#include "GEN/specializer.h"

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
  return EscapeSymbolSegment(request.base_name) + "$g$" + Hex64(Fnv1a64(key));
}

bool NormalizeInstantiationRequests(const std::vector<GenericInstantiationRequest>& requests,
                                    std::vector<GenericInstantiationRequest>* unique_requests) {
  if (!unique_requests) return false;
  unique_requests->clear();
  std::unordered_set<std::string> seen;
  for (const auto& request : requests) {
    if (seen.insert(InstantiationRequestKey(request)).second) {
      unique_requests->push_back(request);
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
  std::vector<Simple::Lang::TAST::GenericDeclarationMetadata> declarations;
  if (!Simple::Lang::TAST::CollectGenericDeclarationMetadata(program, &declarations, error)) {
    return false;
  }
  std::vector<GenericInstantiationRequest> requests;
  if (!CollectInstantiationRequestsFromProgram(program, &requests)) return false;
  std::vector<GenericInstantiationRequest> unique_requests;
  if (!NormalizeInstantiationRequests(requests, &unique_requests)) return false;
  return BuildSpecializationPlan(declarations, unique_requests, out, error);
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

} // namespace Simple::Lang::GEN
