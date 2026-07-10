#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "native/resource_registry.h"
#include "runtime/abi.h"
#include "sbc_types.h"

namespace Simple::VM::Runtime {

std::string CanonicalPrimitiveTypeIdentity(Simple::Byte::TypeKind kind);
std::string CanonicalEnumTypeIdentity(const std::string& enum_name,
                                      Simple::Byte::TypeKind underlying_kind);
std::string CanonicalPointerTypeIdentity(const std::string& pointee_type_identity);
std::string CanonicalArrayTypeIdentity(const std::string& element_type_identity);
std::string CanonicalListTypeIdentity(const std::string& element_type_identity);
std::string CanonicalHandleTypeIdentity(Simple::VM::Native::NativeResourceKind kind);
std::string CanonicalChannelTypeIdentity(const std::string& value_type_identity);
std::string CanonicalAggregateTypeIdentity(const AbiAggregateLayout& layout);
std::string CanonicalInstantiatedTypeIdentity(const std::string& base_type_identity,
                                             const std::vector<std::string>& argument_identities);
std::string CanonicalPromiseTypeIdentity(const std::string& value_type_identity);
std::string CanonicalOptionTypeIdentity(const std::string& value_type_identity);
std::string CanonicalResultTypeIdentity(const std::string& ok_type_identity,
                                        const std::string& error_type_identity);
std::string HumanGenericSymbolName(const std::string& base_symbol,
                                   const std::vector<std::string>& argument_identities);
std::string LinkGenericSymbolName(const std::string& base_symbol,
                                  const std::vector<std::string>& argument_identities);
bool DetectGenericSymbolCollision(const std::vector<std::pair<std::string, std::string>>& link_to_human,
                                  std::string* error);

} // namespace Simple::VM::Runtime
