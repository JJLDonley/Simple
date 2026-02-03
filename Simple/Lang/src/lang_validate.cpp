#include "lang_validate.h"

#include <unordered_set>

#include "lang_parser.h"

namespace Simple::Lang {
namespace {

struct ValidateContext {
  std::unordered_set<std::string> enum_members;
  std::unordered_set<std::string> enum_types;
  std::unordered_set<std::string> top_level;
};

bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_set<std::string>>& scopes,
               std::string* error);

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_set<std::string>>& scopes,
               std::string* error);

bool HasLocal(const std::vector<std::unordered_set<std::string>>& scopes,
              const std::string& name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (it->find(name) != it->end()) return true;
  }
  return false;
}

bool AddLocal(std::vector<std::unordered_set<std::string>>& scopes,
              const std::string& name,
              std::string* error) {
  if (scopes.empty()) scopes.emplace_back();
  auto& current = scopes.back();
  if (!current.insert(name).second) {
    if (error) *error = "duplicate local declaration: " + name;
    return false;
  }
  return true;
}

bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_set<std::string>>& scopes,
               std::string* error) {
  switch (stmt.kind) {
    case StmtKind::Return:
      if (return_is_void && stmt.has_return_expr) {
        if (error) *error = "void function cannot return a value";
        return false;
      }
      if (!return_is_void && !stmt.has_return_expr) {
        if (error) *error = "non-void function must return a value";
        return false;
      }
      if (stmt.has_return_expr) {
        return CheckExpr(stmt.expr, ctx, scopes, error);
      }
      return true;
    case StmtKind::Expr:
      return CheckExpr(stmt.expr, ctx, scopes, error);
    case StmtKind::Assign:
      if (!CheckExpr(stmt.target, ctx, scopes, error)) return false;
      return CheckExpr(stmt.expr, ctx, scopes, error);
    case StmtKind::VarDecl:
      if (!AddLocal(scopes, stmt.var_decl.name, error)) return false;
      if (stmt.var_decl.has_init_expr) {
        return CheckExpr(stmt.var_decl.init_expr, ctx, scopes, error);
      }
      return true;
    case StmtKind::IfChain:
      for (const auto& branch : stmt.if_branches) {
        if (!CheckExpr(branch.first, ctx, scopes, error)) return false;
        scopes.emplace_back();
        for (const auto& child : branch.second) {
          if (!CheckStmt(child, ctx, return_is_void, loop_depth, scopes, error)) return false;
        }
        scopes.pop_back();
      }
      if (!stmt.else_branch.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.else_branch) {
          if (!CheckStmt(child, ctx, return_is_void, loop_depth, scopes, error)) return false;
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::IfStmt:
      if (!CheckExpr(stmt.if_cond, ctx, scopes, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.if_then) {
        if (!CheckStmt(child, ctx, return_is_void, loop_depth, scopes, error)) return false;
      }
      scopes.pop_back();
      if (!stmt.if_else.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.if_else) {
          if (!CheckStmt(child, ctx, return_is_void, loop_depth, scopes, error)) return false;
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::WhileLoop:
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child, ctx, return_is_void, loop_depth + 1, scopes, error)) return false;
      }
      scopes.pop_back();
      return true;
    case StmtKind::ForLoop: {
      scopes.emplace_back();
      if (!CheckExpr(stmt.loop_iter, ctx, scopes, error)) return false;
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, error)) return false;
      if (!CheckExpr(stmt.loop_step, ctx, scopes, error)) return false;
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child, ctx, return_is_void, loop_depth + 1, scopes, error)) return false;
      }
      scopes.pop_back();
      return true;
    }
    case StmtKind::Break:
      if (loop_depth == 0) {
        if (error) *error = "break used outside of loop";
        return false;
      }
      return true;
    case StmtKind::Skip:
      if (loop_depth == 0) {
        if (error) *error = "skip used outside of loop";
        return false;
      }
      return true;
  }
  return true;
}

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_set<std::string>>& scopes,
               std::string* error) {
  switch (expr.kind) {
    case ExprKind::Identifier:
      if (expr.text == "self") return true;
      if (HasLocal(scopes, expr.text)) return true;
      if (ctx.top_level.find(expr.text) != ctx.top_level.end()) return true;
      if (ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
        if (error) *error = "unqualified enum value: " + expr.text;
        return false;
      }
      if (error) *error = "undeclared identifier: " + expr.text;
      return false;
    case ExprKind::Literal:
      return true;
    case ExprKind::Unary:
      return CheckExpr(expr.children[0], ctx, scopes, error);
    case ExprKind::Binary:
      return CheckExpr(expr.children[0], ctx, scopes, error) &&
             CheckExpr(expr.children[1], ctx, scopes, error);
    case ExprKind::Call:
      if (!CheckExpr(expr.children[0], ctx, scopes, error)) return false;
      for (const auto& arg : expr.args) {
        if (!CheckExpr(arg, ctx, scopes, error)) return false;
      }
      return true;
    case ExprKind::Member:
      if (!CheckExpr(expr.children[0], ctx, scopes, error)) return false;
      if (expr.op == "." && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end() &&
            ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
          if (error) *error = "enum members must be qualified with '::': " + base.text + "::" + expr.text;
          return false;
        }
      }
      return true;
    case ExprKind::Index:
      return CheckExpr(expr.children[0], ctx, scopes, error) &&
             CheckExpr(expr.children[1], ctx, scopes, error);
    case ExprKind::ArrayLiteral:
    case ExprKind::ListLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, error)) return false;
      }
      return true;
    case ExprKind::ArtifactLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, error)) return false;
      }
      for (const auto& field_value : expr.field_values) {
        if (!CheckExpr(field_value, ctx, scopes, error)) return false;
      }
      return true;
    case ExprKind::FnLiteral:
      return true;
  }
  return true;
}

bool CheckFunctionBody(const FuncDecl& fn,
                       const ValidateContext& ctx,
                       std::string* error) {
  std::vector<std::unordered_set<std::string>> scopes;
  scopes.emplace_back();
  std::unordered_set<std::string> param_names;
  const bool return_is_void = fn.return_type.name == "void";
  bool saw_return = false;
  for (const auto& param : fn.params) {
    if (!param_names.insert(param.name).second) {
      if (error) *error = "duplicate parameter name: " + param.name;
      return false;
    }
    if (!AddLocal(scopes, param.name, error)) return false;
  }
  for (const auto& stmt : fn.body) {
    if (stmt.kind == StmtKind::Return) saw_return = true;
    if (!CheckStmt(stmt, ctx, return_is_void, 0, scopes, error)) return false;
  }
  if (!return_is_void && !saw_return) {
    if (error) *error = "non-void function must return a value";
    return false;
  }
  return true;
}

} // namespace

bool ValidateProgram(const Program& program, std::string* error) {
  ValidateContext ctx;
  for (const auto& decl : program.decls) {
    const std::string* name_ptr = nullptr;
    switch (decl.kind) {
      case DeclKind::Enum:
        name_ptr = &decl.enm.name;
        {
          std::unordered_set<std::string> local_members;
          for (const auto& member : decl.enm.members) {
            if (!local_members.insert(member.name).second) {
              if (error) *error = "duplicate enum member: " + member.name;
              return false;
            }
            ctx.enum_members.insert(member.name);
          }
        }
        ctx.enum_types.insert(decl.enm.name);
        break;
      case DeclKind::Artifact:
        name_ptr = &decl.artifact.name;
        break;
      case DeclKind::Module:
        name_ptr = &decl.module.name;
        break;
      case DeclKind::Function:
        name_ptr = &decl.func.name;
        break;
      case DeclKind::Variable:
        name_ptr = &decl.var.name;
        break;
    }
    if (name_ptr && !ctx.top_level.insert(*name_ptr).second) {
      if (error) *error = "duplicate top-level declaration: " + *name_ptr;
      return false;
    }
  }

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case DeclKind::Function:
        if (!CheckFunctionBody(decl.func, ctx, error)) return false;
        break;
      case DeclKind::Artifact:
        {
          std::unordered_set<std::string> names;
          for (const auto& field : decl.artifact.fields) {
            if (!names.insert(field.name).second) {
              if (error) *error = "duplicate artifact member: " + field.name;
              return false;
            }
          }
          for (const auto& method : decl.artifact.methods) {
            if (!names.insert(method.name).second) {
              if (error) *error = "duplicate artifact member: " + method.name;
              return false;
            }
          }
        }
        for (const auto& method : decl.artifact.methods) {
          if (!CheckFunctionBody(method, ctx, error)) return false;
        }
        break;
      case DeclKind::Module:
        {
          std::unordered_set<std::string> names;
          for (const auto& var : decl.module.variables) {
            if (!names.insert(var.name).second) {
              if (error) *error = "duplicate module member: " + var.name;
              return false;
            }
          }
          for (const auto& fn : decl.module.functions) {
            if (!names.insert(fn.name).second) {
              if (error) *error = "duplicate module member: " + fn.name;
              return false;
            }
          }
        }
        for (const auto& fn : decl.module.functions) {
          if (!CheckFunctionBody(fn, ctx, error)) return false;
        }
        break;
      case DeclKind::Enum:
      case DeclKind::Variable:
        break;
    }
  }

  return true;
}

bool ValidateProgramFromString(const std::string& text, std::string* error) {
  Program program;
  if (!ParseProgramFromString(text, &program, error)) return false;
  return ValidateProgram(program, error);
}

} // namespace Simple::Lang
