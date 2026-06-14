#include "TAST/statements.h"

namespace Simple::Lang::TAST {

bool IsAssignOp(const std::string& op) {
  return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
         op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" ||
         op == ">>=";
}

bool CheckProgramHasDeclarationsOrTopLevelStatements(const Simple::Lang::AST::Program& program,
                                                     std::string* error) {
  if (program.decls.empty() && program.top_level_stmts.empty()) {
    if (error) *error = "program has no declarations or top-level statements";
    return false;
  }
  return true;
}

bool CheckEnumMemberValue(const Simple::Lang::AST::EnumMember& member, std::string* error) {
  if (!member.has_value) {
    if (error) *error = "enum member requires explicit value: " + member.name;
    return false;
  }
  return true;
}

bool CheckUniqueNamedMember(const std::string& name,
                            std::unordered_set<std::string>* seen,
                            const std::string& error_prefix,
                            std::string* error) {
  if (!seen) return false;
  if (!seen->insert(name).second) {
    if (error) *error = error_prefix + name;
    return false;
  }
  return true;
}

bool CheckAssignment(const Stmt& stmt, std::string* error) {
  if (stmt.kind != Simple::Lang::AST::StmtKind::Assign) {
    if (error) *error = "expected assignment statement";
    return false;
  }
  if (stmt.target.kind == Simple::Lang::AST::ExprKind::Identifier && stmt.target.text.empty()) {
    if (error) *error = "assignment missing target";
    return false;
  }
  return true;
}

} // namespace Simple::Lang::TAST
