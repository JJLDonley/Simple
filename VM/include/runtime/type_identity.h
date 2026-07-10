#pragma once

#include <cstdint>
#include <string>

#include "native/resource_registry.h"
#include "runtime/abi.h"
#include "sbc_types.h"

namespace Simple::VM::Runtime {

std::string CanonicalPrimitiveTypeIdentity(Simple::Byte::TypeKind kind);
std::string CanonicalHandleTypeIdentity(Simple::VM::Native::NativeResourceKind kind);
std::string CanonicalAggregateTypeIdentity(const AbiAggregateLayout& layout);
std::string CanonicalPromiseTypeIdentity(const std::string& value_type_identity);
std::string CanonicalOptionTypeIdentity(const std::string& value_type_identity);
std::string CanonicalResultTypeIdentity(const std::string& ok_type_identity,
                                        const std::string& error_type_identity);

} // namespace Simple::VM::Runtime
