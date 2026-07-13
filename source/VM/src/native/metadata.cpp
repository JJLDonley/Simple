#include "native/registry.h"

#include "runtime/abi.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Simple::VM::Native {

bool IsDirectNativeBindingSafe(const NativeFunctionSpec& spec) {
  return spec.direct_binding_safe && spec.blocking == NativeBlockingBehavior::NonBlocking &&
         spec.allocation == NativeAllocationBehavior::NoAllocation &&
         spec.gc_behavior == NativeGcBehavior::NoSafepoint && spec.resources.empty();
}

bool NativeTypeMatchesLibraryType(Simple::Byte::TypeKind native_type,
                                  std::string_view library_type) {
  using Simple::Byte::TypeKind;
  switch (native_type) {
    case TypeKind::Unspecified:
    case TypeKind::Void: return library_type == "void";
    case TypeKind::Bool: return library_type == "bool";
    case TypeKind::I8: return library_type == "i8";
    case TypeKind::I16: return library_type == "i16";
    case TypeKind::I32: return library_type == "i32" || library_type == "bool";
    case TypeKind::I64: return library_type == "i64";
    case TypeKind::I128: return library_type == "i128";
    case TypeKind::U8: return library_type == "u8";
    case TypeKind::U16: return library_type == "u16";
    case TypeKind::U32: return library_type == "u32";
    case TypeKind::U64: return library_type == "u64";
    case TypeKind::U128: return library_type == "u128";
    case TypeKind::F32: return library_type == "f32";
    case TypeKind::F64: return library_type == "f64";
    case TypeKind::String: return library_type == "string";
    case TypeKind::Ref:
    case TypeKind::Array:
    case TypeKind::List: return library_type == "ref" || library_type == "i32[]" || library_type == "string[]";
    case TypeKind::Ptr: return library_type == "ptr" || library_type == "i64" || library_type == "handle";
    case TypeKind::Char: return library_type == "char";
    case TypeKind::Never: return library_type == "never";
    case TypeKind::Function: return library_type == "function";
    case TypeKind::Result: return library_type == "result";
    case TypeKind::Option: return library_type == "option";
    case TypeKind::Vector: return library_type == "vector";
  }
  return false;
}

bool ValidateNativeSpecAgainstLibrarySignature(const NativeFunctionSpec& spec,
                                               const Simple::Lang::LibrarySignatureSpec& signature,
                                               std::string* error) {
  const std::string name = NativeFunctionName(spec.module_name, spec.symbol_name);
  if (spec.parameter_types.size() != signature.params.size()) {
    if (error) *error = name + " catalog signature arity mismatch";
    return false;
  }
  for (size_t i = 0; i < spec.parameter_types.size(); ++i) {
    if (!NativeTypeMatchesLibraryType(spec.parameter_types[i], signature.params[i].type.name)) {
      if (error) *error = name + " catalog signature parameter mismatch at " + std::to_string(i);
      return false;
    }
  }
  if (!NativeTypeMatchesLibraryType(spec.result_type, signature.return_type.name)) {
    if (error) *error = name + " catalog signature result mismatch";
    return false;
  }
  return true;
}

NativeJitCallValidation AnalyzeNativeJitCall(const NativeFunctionSpec& spec,
                                             const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                             Simple::Byte::TypeKind result_type) {
  NativeJitCallValidation out;
  out.may_block = spec.blocking != NativeBlockingBehavior::NonBlocking;
  out.may_allocate = spec.allocation != NativeAllocationBehavior::NoAllocation;
  auto needs_root = [](Simple::Byte::TypeKind kind) {
    return kind == Simple::Byte::TypeKind::String || kind == Simple::Byte::TypeKind::Ref;
  };
  for (Simple::Byte::TypeKind kind : parameter_types) {
    if (needs_root(kind)) out.needs_roots = true;
  }
  if (needs_root(result_type)) out.needs_roots = true;

  std::string metadata_error;
  out.metadata_valid = ValidateNativeFunctionMetadata(spec, &metadata_error);
  if (!out.metadata_valid) {
    out.reason = "invalid-native-abi-metadata";
    return out;
  }
  if (spec.parameter_types.size() != parameter_types.size()) {
    out.reason = "metadata-signature-mismatch";
    return out;
  }
  for (size_t i = 0; i < parameter_types.size(); ++i) {
    if (spec.parameter_types[i] != parameter_types[i]) {
      out.reason = "metadata-signature-mismatch";
      return out;
    }
  }
  if (spec.result_type != Simple::Byte::TypeKind::Unspecified && spec.result_type != result_type) {
    out.reason = "metadata-signature-mismatch";
    return out;
  }
  out.signature_matches = true;
  out.jit_helper_safe = true;

  for (const NativeResourceUse& resource : spec.resources) {
    if (resource.access != NativeResourceAccess::Input || resource.parameter_index >= spec.parameter_types.size()) {
      out.reason = "resource-argument-or-result";
      return out;
    }
  }
  if (out.may_block) {
    out.reason = "blocking-call";
    return out;
  }
  if (out.may_allocate) {
    out.reason = "allocating-call";
    return out;
  }
  if (spec.gc_behavior != NativeGcBehavior::NoSafepoint) {
    out.reason = "gc-safepoint-call";
    return out;
  }
  out.jit_loop_safe = true;
  return out;
}

bool ValidateNativeFunctionMetadata(const NativeFunctionSpec& spec, std::string* error) {
  const std::string name = NativeFunctionName(spec.module_name, spec.symbol_name);
  if (spec.module_name.empty() || spec.symbol_name.empty()) {
    if (error) *error = "native metadata has empty module or symbol";
    return false;
  }
  if (!spec.handler) {
    if (error) *error = name + " missing handler";
    return false;
  }
  const bool standard_backed_by_system =
      spec.library_module && spec.library_module->root == Simple::Lang::LibraryRoot::Standard &&
      Simple::Lang::ToNativeModule(*spec.library_module) == spec.module_name &&
      spec.layer == NativeLayer::Standard;
  if (spec.module_name.rfind("System.", 0) == 0 && spec.layer != NativeLayer::System &&
      !standard_backed_by_system) {
    if (error) *error = name + " system native function must declare system layer";
    return false;
  }
  if (spec.library_module) {
    const auto parsed_module = Simple::Lang::ParseLibraryImportPath(spec.module_name);
    if (!parsed_module) {
      if (error) *error = name + " native module catalog id has non-catalog module name";
      return false;
    }
    const Simple::Lang::LibraryModuleId expected{parsed_module->root, parsed_module->module_index};
    if (!(*spec.library_module == expected) &&
        Simple::Lang::ToNativeModule(*spec.library_module) != spec.module_name) {
      if (error) *error = name + " native module catalog id mismatch";
      return false;
    }
    const auto signature = Simple::Lang::GetLibrarySignature(*spec.library_module, spec.symbol_name);
    if (signature && !ValidateNativeSpecAgainstLibrarySignature(spec, *signature, error)) {
      return false;
    }
  }
  if (spec.doc_summary.empty()) {
    if (error) *error = name + " missing doc summary";
    return false;
  }
  std::string abi_error;
  if (!Simple::VM::Runtime::ValidateAbiCallableSignature(spec.parameter_types, spec.result_type,
                                                         false, &abi_error)) {
    if (error) *error = name + " has invalid ABI signature: " + abi_error;
    return false;
  }
  if (spec.allocation == NativeAllocationBehavior::MayAllocateVm &&
      spec.gc_behavior != NativeGcBehavior::MaySafepoint) {
    if (error) *error = name + " VM allocation metadata must declare GC safepoint behavior";
    return false;
  }
  if (spec.direct_binding_safe && !IsDirectNativeBindingSafe(spec)) {
    if (error) *error = name + " direct native binding marked safe with unsafe metadata";
    return false;
  }
  for (const NativeResourceUse& resource : spec.resources) {
    if (resource.kind == NativeResourceKind::Unknown) {
      if (error) *error = name + " declares unknown resource kind";
      return false;
    }
    if (resource.access != NativeResourceAccess::Output &&
        resource.parameter_index >= spec.parameter_types.size()) {
      if (error) *error = name + " resource parameter index out of range";
      return false;
    }
    if (resource.ownership == NativeOwnershipRule::None) {
      if (error) *error = name + " resource missing ownership rule";
      return false;
    }
    if (resource.access == NativeResourceAccess::Output &&
        resource.cleanup == NativeCleanupBehavior::None) {
      if (error) *error = name + " resource output missing cleanup behavior";
      return false;
    }
    if (resource.access == NativeResourceAccess::InputOutput &&
        resource.cleanup != NativeCleanupBehavior::CloseRequired) {
      if (error) *error = name + " inout resource must require close";
      return false;
    }
  }
  return true;
}

bool ValidateNativeRegistryMetadata(const NativeRegistry& registry, std::string* error) {
  for (const NativeFunctionSpec& spec : registry.Functions()) {
    if (!ValidateNativeFunctionMetadata(spec, error)) return false;
  }
  return true;
}

std::string LayerMarkdown(NativeLayer layer) {
  switch (layer) {
    case NativeLayer::Core:
      return "core";
    case NativeLayer::System:
      return "system";
    case NativeLayer::Standard:
      return "standard";
    case NativeLayer::Domain:
      return "domain";
  }
  return "unknown";
}

std::string TypeKindMarkdown(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::Unspecified:
      return "void";
    case TypeKind::Bool:
      return "bool";
    case TypeKind::I32:
      return "i32";
    case TypeKind::I64:
      return "i64";
    case TypeKind::F32:
      return "f32";
    case TypeKind::F64:
      return "f64";
    case TypeKind::String:
      return "string";
    case TypeKind::Ref:
      return "ref";
    default:
      return "unknown";
  }
}

std::string BlockingMarkdown(NativeBlockingBehavior blocking) {
  return blocking == NativeBlockingBehavior::MayBlock ? "may-block" : "non-blocking";
}

std::string AllocationMarkdown(NativeAllocationBehavior allocation) {
  switch (allocation) {
    case NativeAllocationBehavior::NoAllocation:
      return "no-alloc";
    case NativeAllocationBehavior::MayAllocateVm:
      return "vm-alloc";
    case NativeAllocationBehavior::MayAllocateHost:
      return "host-alloc";
  }
  return "unknown";
}

std::string GcMarkdown(NativeGcBehavior gc_behavior) {
  switch (gc_behavior) {
    case NativeGcBehavior::NoSafepoint:
      return "no-safepoint";
    case NativeGcBehavior::MaySafepoint:
      return "may-safepoint";
  }
  return "unknown";
}

std::string DirectBindingMarkdown(bool direct_binding_safe) {
  return direct_binding_safe ? "safe" : "-";
}

std::string StabilityMarkdown(NativeStability stability) {
  switch (stability) {
    case NativeStability::Experimental:
      return "experimental";
    case NativeStability::Stable:
      return "stable";
    case NativeStability::Unsafe:
      return "unsafe";
  }
  return "unknown";
}

std::string ResourceKindMarkdown(NativeResourceKind kind) {
  switch (kind) {
    case NativeResourceKind::File:
      return "file";
    case NativeResourceKind::Directory:
      return "directory";
    case NativeResourceKind::Socket:
      return "socket";
    case NativeResourceKind::Listener:
      return "listener";
    case NativeResourceKind::Process:
      return "process";
    case NativeResourceKind::Thread:
      return "thread";
    case NativeResourceKind::Job:
      return "job";
    case NativeResourceKind::Channel:
      return "channel";
    case NativeResourceKind::FfiLibrary:
      return "ffi-library";
    case NativeResourceKind::FfiSymbol:
      return "ffi-symbol";
    case NativeResourceKind::AsmUnit:
      return "asm-unit";
    case NativeResourceKind::AsmObject:
      return "asm-object";
    case NativeResourceKind::AsmSymbol:
      return "asm-symbol";
    case NativeResourceKind::Buffer:
      return "buffer";
    case NativeResourceKind::Timer:
      return "timer";
    case NativeResourceKind::Watcher:
      return "watcher";
    case NativeResourceKind::Terminal:
      return "terminal";
    case NativeResourceKind::JsonValue:
      return "json-value";
    case NativeResourceKind::Logger:
      return "logger";
    case NativeResourceKind::Random:
      return "random";
    case NativeResourceKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string ResourceAccessMarkdown(NativeResourceAccess access) {
  switch (access) {
    case NativeResourceAccess::Input:
      return "in";
    case NativeResourceAccess::Output:
      return "out";
    case NativeResourceAccess::InputOutput:
      return "inout";
  }
  return "unknown";
}

std::string OwnershipMarkdown(NativeOwnershipRule ownership) {
  switch (ownership) {
    case NativeOwnershipRule::None:
      return "none";
    case NativeOwnershipRule::Borrow:
      return "borrow";
    case NativeOwnershipRule::TransferToCaller:
      return "to-caller";
    case NativeOwnershipRule::TransferToCallee:
      return "to-callee";
  }
  return "unknown";
}

std::string CleanupMarkdown(NativeCleanupBehavior cleanup) {
  switch (cleanup) {
    case NativeCleanupBehavior::None:
      return "none";
    case NativeCleanupBehavior::CloseRequired:
      return "close-required";
    case NativeCleanupBehavior::AutoCloseOnVmShutdown:
      return "vm-shutdown";
  }
  return "unknown";
}

std::string TagsMarkdown(const std::vector<std::string>& tags) {
  if (tags.empty()) return "-";
  std::ostringstream out;
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i > 0) out << ", ";
    out << tags[i];
  }
  return out.str();
}

std::string PlatformsMarkdown(const std::vector<std::string>& platforms) {
  return platforms.empty() ? "all" : TagsMarkdown(platforms);
}

std::string SummaryMarkdown(const std::string& summary) {
  return summary.empty() ? "-" : summary;
}

std::string AvailabilityMarkdown(Simple::Lang::LibraryApiAvailability availability) {
  switch (availability) {
    case Simple::Lang::LibraryApiAvailability::Implemented: return "implemented";
    case Simple::Lang::LibraryApiAvailability::Planned: return "planned";
  }
  return "planned";
}

std::string ResourcesMarkdown(const std::vector<NativeResourceUse>& resources) {
  if (resources.empty()) return "-";
  std::ostringstream out;
  for (size_t i = 0; i < resources.size(); ++i) {
    if (i > 0) out << ", ";
    const NativeResourceUse& resource = resources[i];
    out << ResourceAccessMarkdown(resource.access) << ":" << ResourceKindMarkdown(resource.kind);
    if (resource.parameter_index != kNativeResourceNoParameter) out << "@" << resource.parameter_index;
    out << ":" << OwnershipMarkdown(resource.ownership) << ":" << CleanupMarkdown(resource.cleanup);
  }
  return out.str();
}

std::string GenerateStdLibMarkdown(const NativeRegistry& registry) {
  std::map<std::string, std::vector<const NativeFunctionSpec*>> modules;
  for (const NativeFunctionSpec& spec : registry.Functions()) {
    const std::string module_key = spec.library_module
                                       ? std::string(Simple::Lang::ToImportPath(*spec.library_module))
                                       : spec.module_name;
    modules[module_key].push_back(&spec);
  }
  std::ostringstream out;
  out << "# Native Standard Library Metadata\n\n";
  out << "Generated from `NativeRegistry` metadata plus library catalog module/member ids.\n";
  for (auto& entry : modules) {
    std::sort(entry.second.begin(), entry.second.end(), [](const NativeFunctionSpec* lhs,
                                                           const NativeFunctionSpec* rhs) {
      return lhs->symbol_name < rhs->symbol_name;
    });
    out << "\n## " << entry.first << "\n\n";
    out << "| Symbol | Catalog | Availability | Layer | Signature | Blocking | Allocation | GC | Direct | Capabilities | Resources | Platforms | Stability | Summary |\n"
        << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const NativeFunctionSpec* spec : entry.second) {
      const auto catalog_meta = spec->library_module
                                    ? Simple::Lang::GetLibraryMemberMetadata(*spec->library_module, spec->symbol_name)
                                    : Simple::Lang::LibraryMemberMetadata{};
      out << "| `" << spec->symbol_name << "` | `"
          << (spec->library_module ? Simple::Lang::ToImportPath(*spec->library_module) : std::string_view("-"))
          << "` | `"
          << (spec->library_module ? AvailabilityMarkdown(catalog_meta.availability) : std::string("-"))
          << "` | `" << LayerMarkdown(spec->layer) << "` | `(";
      for (size_t i = 0; i < spec->parameter_types.size(); ++i) {
        if (i > 0) out << ", ";
        out << TypeKindMarkdown(spec->parameter_types[i]);
      }
      out << ") -> " << TypeKindMarkdown(spec->result_type) << "` | `"
          << BlockingMarkdown(spec->blocking) << "` | `"
          << AllocationMarkdown(spec->allocation) << "` | `"
          << GcMarkdown(spec->gc_behavior) << "` | `"
          << DirectBindingMarkdown(spec->direct_binding_safe) << "` | `"
          << TagsMarkdown(spec->capability_tags) << "` | `"
          << ResourcesMarkdown(spec->resources) << "` | `"
          << PlatformsMarkdown(spec->platforms) << "` | `"
          << StabilityMarkdown(spec->stability) << "` | "
          << SummaryMarkdown(spec->doc_summary) << " |\n";
    }
  }
  return out.str();
}


} // namespace Simple::VM::Native
