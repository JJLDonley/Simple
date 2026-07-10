#include "runtime/type_identity.h"

#include <sstream>

namespace Simple::VM::Runtime {
namespace {

std::string Hex64(uint64_t value) {
  std::ostringstream out;
  out << std::hex << value;
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

std::string CanonicalHandleTypeIdentity(Simple::VM::Native::NativeResourceKind kind) {
  return "handle#" + std::to_string(Simple::VM::Native::NativeResourceKindId(kind));
}

std::string CanonicalAggregateTypeIdentity(const AbiAggregateLayout& layout) {
  return "data#" + Hex64(layout.layout_hash) + ":" + std::to_string(layout.size) + ":" +
         std::to_string(layout.align);
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

} // namespace Simple::VM::Runtime
