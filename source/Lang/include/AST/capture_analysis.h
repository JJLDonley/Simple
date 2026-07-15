#ifndef SIMPLE_LANG_AST_CAPTURE_ANALYSIS_H
#define SIMPLE_LANG_AST_CAPTURE_ANALYSIS_H

#include <string>
#include <unordered_set>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::ASTAnalysis {

std::unordered_set<std::string> FindFnLiteralFreeNames(const Expr& literal);

void CollectAllLocalNames(const std::vector<Stmt>& body,
                          std::unordered_set<std::string>* names);

void CollectCapturedLocalsFromStatements(
    const std::vector<Stmt>& body,
    const std::unordered_set<std::string>& available,
    std::unordered_set<std::string>* captures);

} // namespace Simple::Lang::ASTAnalysis

#endif // SIMPLE_LANG_AST_CAPTURE_ANALYSIS_H
