#include <cstdint>
#include <exception>
#include <string>

bool IsIntegerLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Integer;
}

bool IsFloatLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Float;
}

std::string EscapeStringLiteral(const std::string& value, std::string* error) {
  (void)error;
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          static const char kHex[] = "0123456789ABCDEF";
          unsigned char byte = static_cast<unsigned char>(ch);
          out += "\\x";
          out.push_back(kHex[(byte >> 4) & 0xF]);
          out.push_back(kHex[byte & 0xF]);
          break;
        }
        out.push_back(ch);
        break;
    }
  }
  return out;
}

bool ParseIntegerLiteralText(const std::string& text, int64_t* out) {
  if (!out) return false;
  try {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
      *out = static_cast<int64_t>(std::stoull(text.substr(2), nullptr, 16));
      return true;
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
      uint64_t value = 0;
      for (size_t i = 2; i < text.size(); ++i) {
        char c = text[i];
        if (c != '0' && c != '1') return false;
        value = (value << 1) | static_cast<uint64_t>(c - '0');
      }
      *out = static_cast<int64_t>(value);
      return true;
    }
    *out = static_cast<int64_t>(std::stoll(text, nullptr, 10));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}
