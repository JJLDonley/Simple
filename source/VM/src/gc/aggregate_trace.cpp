#include "gc/aggregate_trace.h"

#include <cstdint>
#include <string>

#include "sbc_loader.h"

namespace Simple::VM::Gc {

std::vector<AggregateTraceDescriptor> BuildAggregateTraceDescriptors(
    const Simple::Byte::SbcModule& module) {
  auto is_gc_reference_type = [&](uint32_t type_id) {
    if (type_id >= module.types.size()) return false;
    const auto& row = module.types[type_id];
    if (Simple::Byte::IsManagedClassType(row)) return true;
    switch (static_cast<Simple::Byte::TypeKind>(row.kind)) {
      case Simple::Byte::TypeKind::Ref:
      case Simple::Byte::TypeKind::String:
      case Simple::Byte::TypeKind::Array:
      case Simple::Byte::TypeKind::List:
      case Simple::Byte::TypeKind::Function:
      case Simple::Byte::TypeKind::Result:
      case Simple::Byte::TypeKind::Optional:
        return true;
      default:
        return false;
    }
  };

  std::vector<AggregateTraceDescriptor> descriptors(module.types.size());
  for (uint32_t type_id = 0; type_id < module.types.size(); ++type_id) {
    const auto& row = module.types[type_id];
    const auto kind = static_cast<Simple::Byte::TypeKind>(row.kind);
    if (kind != Simple::Byte::TypeKind::Result &&
        kind != Simple::Byte::TypeKind::Optional) {
      continue;
    }
    AggregateTraceDescriptor& descriptor = descriptors[type_id];
    descriptor.configured = true;
    const uint64_t field_end = static_cast<uint64_t>(row.field_start) + row.field_count;
    if (field_end > module.fields.size()) continue;

    bool found_result_tag = false;
    for (uint32_t i = 0; i < row.field_count; ++i) {
      const auto& field = module.fields[row.field_start + i];
      const std::string name = Simple::Byte::ReadConstPoolString(module, field.name_str);
      if (kind == Simple::Byte::TypeKind::Result && name == "tag") {
        descriptor.tag_offset = field.offset;
        found_result_tag = true;
        continue;
      }
      if (!is_gc_reference_type(field.type_id)) continue;
      if (kind == Simple::Byte::TypeKind::Result && name == "value") {
        descriptor.zero_tag_refs.push_back(field.offset);
      } else if (kind == Simple::Byte::TypeKind::Result && name == "error") {
        descriptor.nonzero_tag_refs.push_back(field.offset);
      } else {
        descriptor.refs.push_back(field.offset);
      }
    }
    descriptor.branch_on_tag = kind == Simple::Byte::TypeKind::Result && found_result_tag;
    if (kind == Simple::Byte::TypeKind::Result && !descriptor.branch_on_tag) {
      descriptor.refs.insert(descriptor.refs.end(),
                             descriptor.zero_tag_refs.begin(),
                             descriptor.zero_tag_refs.end());
      descriptor.refs.insert(descriptor.refs.end(),
                             descriptor.nonzero_tag_refs.begin(),
                             descriptor.nonzero_tag_refs.end());
      descriptor.zero_tag_refs.clear();
      descriptor.nonzero_tag_refs.clear();
    }
  }
  return descriptors;
}

} // namespace Simple::VM::Gc
