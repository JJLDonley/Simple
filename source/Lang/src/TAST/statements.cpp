#include "TAST/statements.h"

#include <limits>

namespace Simple::Lang::TAST {
namespace {

uint32_t CanonicalEnumBitWidth(const std::string& name) {
  if (name == "i8" || name == "u8") return 8;
  if (name == "i16" || name == "u16") return 16;
  if (name == "i32" || name == "u32") return 32;
  return 64;
}

} // namespace

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

bool IsCanonicalEnumUnderlyingType(const TypeRef& type) {
  if (type.pointer_depth > 0 || type.is_proc || !type.type_args.empty() ||
      !type.dims.empty()) {
    return false;
  }
  return type.name == "i8" || type.name == "i16" || type.name == "i32" ||
         type.name == "i64" || type.name == "u8" || type.name == "u16" ||
         type.name == "u32" || type.name == "u64";
}

bool ParseCanonicalEnumValue(const std::string& text,
                             const TypeRef& underlying_type,
                             uint64_t* value,
                             std::string* error) {
  if (!value || !IsCanonicalEnumUnderlyingType(underlying_type) || text.empty()) {
    if (error) *error = "enum underlying type must be a fixed-width integer";
    return false;
  }
  size_t index = 0;
  const bool negative = text[index] == '-';
  if (negative && ++index == text.size()) {
    if (error) *error = "invalid enum value";
    return false;
  }
  uint32_t base = 10;
  if (index + 2 <= text.size() && text[index] == '0' &&
      (text[index + 1] == 'x' || text[index + 1] == 'X')) {
    base = 16;
    index += 2;
  } else if (index + 2 <= text.size() && text[index] == '0' &&
             (text[index + 1] == 'b' || text[index + 1] == 'B')) {
    base = 2;
    index += 2;
  }
  if (index == text.size()) {
    if (error) *error = "invalid enum value";
    return false;
  }
  uint64_t magnitude = 0;
  for (; index < text.size(); ++index) {
    const char c = text[index];
    uint32_t digit = 0;
    if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
    else {
      if (error) *error = "invalid enum value";
      return false;
    }
    if (digit >= base || magnitude >
                             (std::numeric_limits<uint64_t>::max() - digit) / base) {
      if (error) *error = "enum value is out of range";
      return false;
    }
    magnitude = magnitude * base + digit;
  }
  const bool signed_type = !underlying_type.name.empty() &&
                           underlying_type.name.front() == 'i';
  const uint32_t bits = CanonicalEnumBitWidth(underlying_type.name);
  const uint64_t mask = bits == 64 ? std::numeric_limits<uint64_t>::max()
                                   : (uint64_t{1} << bits) - 1u;
  if (negative) {
    if (!signed_type) {
      if (error) *error = "negative enum value requires signed underlying type";
      return false;
    }
    const uint64_t limit = uint64_t{1} << (bits - 1u);
    if (magnitude > limit) {
      if (error) *error = "enum value is out of range";
      return false;
    }
    *value = (uint64_t{0} - magnitude) & mask;
    return true;
  }
  const uint64_t maximum = signed_type
                               ? (bits == 64 ? static_cast<uint64_t>(
                                                   std::numeric_limits<int64_t>::max())
                                             : (uint64_t{1} << (bits - 1u)) - 1u)
                               : mask;
  if (magnitude > maximum) {
    if (error) *error = "enum value is out of range";
    return false;
  }
  *value = magnitude;
  return true;
}

std::string FormatCanonicalEnumValue(uint64_t value,
                                     const TypeRef& underlying_type) {
  const bool signed_type = !underlying_type.name.empty() &&
                           underlying_type.name.front() == 'i';
  const uint32_t bits = CanonicalEnumBitWidth(underlying_type.name);
  if (!signed_type) return std::to_string(value);
  if (bits < 64 && (value & (uint64_t{1} << (bits - 1u))) != 0u) {
    value |= ~((uint64_t{1} << bits) - 1u);
  }
  return std::to_string(static_cast<int64_t>(value));
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
