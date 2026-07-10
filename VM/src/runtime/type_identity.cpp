#include "runtime/type_identity.h"

#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace Simple::VM::Runtime {
namespace {

std::string Hex64(uint64_t value) {
  std::ostringstream out;
  out << std::hex << value;
  return out.str();
}

uint64_t Fnv1a64(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char ch : value) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string EscapeIdentitySegment(const std::string& value) {
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

} // namespace

std::string CanonicalPrimitiveTypeIdentity(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::I8: return "i8";
    case TypeKind::I16: return "i16";
    case TypeKind::I32: return "i32";
    case TypeKind::I64: return "i64";
    case TypeKind::I128: return "i128";
    case TypeKind::U8: return "u8";
    case TypeKind::U16: return "u16";
    case TypeKind::U32: return "u32";
    case TypeKind::U64: return "u64";
    case TypeKind::U128: return "u128";
    case TypeKind::F32: return "f32";
    case TypeKind::F64: return "f64";
    case TypeKind::Bool: return "bool";
    case TypeKind::Char: return "char";
    case TypeKind::String: return "string";
    case TypeKind::Void: return "void";
    case TypeKind::Never: return "never";
    case TypeKind::Ptr: return "ptr";
    case TypeKind::Ref: return "ref";
    case TypeKind::Array: return "array";
    case TypeKind::List: return "list";
    case TypeKind::Function: return "function";
    case TypeKind::Result: return "result";
    case TypeKind::Option: return "option";
    case TypeKind::Vector: return "vector";
    case TypeKind::Unspecified: return "unspecified";
  }
  return "invalid";
}

std::string CanonicalEnumTypeIdentity(const std::string& enum_name,
                                      Simple::Byte::TypeKind underlying_kind) {
  return "enum:" + EscapeIdentitySegment(enum_name) + ":" +
         CanonicalPrimitiveTypeIdentity(underlying_kind);
}

std::string CanonicalPointerTypeIdentity(const std::string& pointee_type_identity) {
  return "ptr<" + pointee_type_identity + ">";
}

std::string CanonicalArrayTypeIdentity(const std::string& element_type_identity) {
  return "array<" + element_type_identity + ">";
}

std::string CanonicalListTypeIdentity(const std::string& element_type_identity) {
  return "list<" + element_type_identity + ">";
}

std::string CanonicalHandleTypeIdentity(Simple::VM::Native::NativeResourceKind kind) {
  return "handle#" + std::to_string(Simple::VM::Native::NativeResourceKindId(kind));
}

std::string CanonicalChannelTypeIdentity(const std::string& value_type_identity) {
  return "channel<" + value_type_identity + ">";
}

std::string CanonicalAggregateTypeIdentity(const AbiAggregateLayout& layout) {
  return "data#" + Hex64(layout.layout_hash) + ":" + std::to_string(layout.size) + ":" +
         std::to_string(layout.align);
}

std::string CanonicalInstantiatedTypeIdentity(const std::string& base_type_identity,
                                             const std::vector<std::string>& argument_identities) {
  std::string out = "inst<" + base_type_identity;
  for (const std::string& argument : argument_identities) {
    out += ",";
    out += argument;
  }
  out += ">";
  return out;
}

std::string CanonicalPromiseTypeIdentity(const std::string& value_type_identity) {
  return "promise<" + value_type_identity + ">";
}

std::string CanonicalOptionTypeIdentity(const std::string& value_type_identity) {
  return "option<" + value_type_identity + ">";
}

std::string CanonicalResultTypeIdentity(const std::string& ok_type_identity,
                                        const std::string& error_type_identity) {
  return "result<" + ok_type_identity + "," + error_type_identity + ">";
}

std::string HumanGenericSymbolName(const std::string& base_symbol,
                                   const std::vector<std::string>& argument_identities) {
  std::string out = base_symbol + "<";
  for (size_t i = 0; i < argument_identities.size(); ++i) {
    if (i != 0) out += ", ";
    out += argument_identities[i];
  }
  out += ">";
  return out;
}

std::string LinkGenericSymbolName(const std::string& base_symbol,
                                  const std::vector<std::string>& argument_identities) {
  const std::string human = HumanGenericSymbolName(base_symbol, argument_identities);
  return EscapeIdentitySegment(base_symbol) + "$g$" + Hex64(Fnv1a64(human));
}

bool DetectGenericSymbolCollision(const std::vector<std::pair<std::string, std::string>>& link_to_human,
                                  std::string* error) {
  std::unordered_map<std::string, std::string> seen;
  for (const auto& entry : link_to_human) {
    const auto [it, inserted] = seen.emplace(entry.first, entry.second);
    if (!inserted && it->second != entry.second) {
      if (error) {
        *error = "generic symbol collision for " + entry.first + ": " + it->second + " vs " +
                 entry.second;
      }
      return false;
    }
  }
  if (error) error->clear();
  return true;
}

} // namespace Simple::VM::Runtime
