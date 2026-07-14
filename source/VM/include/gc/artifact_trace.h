#ifndef SIMPLE_VM_GC_ARTIFACT_TRACE_H
#define SIMPLE_VM_GC_ARTIFACT_TRACE_H

#include <vector>

#include "heap.h"
#include "sbc_types.h"

namespace Simple::VM::Gc {

std::vector<ArtifactTraceDescriptor> BuildArtifactTraceDescriptors(
    const Simple::Byte::SbcModule& module);

} // namespace Simple::VM::Gc

#endif // SIMPLE_VM_GC_ARTIFACT_TRACE_H
