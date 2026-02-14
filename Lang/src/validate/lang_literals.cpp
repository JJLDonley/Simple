#include <string>

bool CountFormatPlaceholders(const std::string& fmt,
                             size_t* out_count,
                             std::string* error) {
  if (!out_count) return false;
  *out_count = 0;
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{') {
      if (i + 1 >= fmt.size() || fmt[i + 1] != '}') {
        if (error) *error = "invalid format string: expected '{}' placeholder";
        return false;
      }
      ++(*out_count);
      ++i;
      continue;
    }
    if (fmt[i] == '}') {
      if (error) *error = "invalid format string: unmatched '}'";
      return false;
    }
  }
  return true;
}

bool IsIntegerLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Integer;
}

bool IsFloatLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Float;
}
