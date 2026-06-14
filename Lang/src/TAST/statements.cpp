#include "TAST/statements.h"

namespace Simple::Lang::TAST {

bool IsAssignOp(const std::string& op) {
  return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
         op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" ||
         op == ">>=";
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
