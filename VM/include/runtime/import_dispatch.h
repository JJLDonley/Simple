#ifndef SIMPLE_VM_RUNTIME_IMPORT_DISPATCH_H
#define SIMPLE_VM_RUNTIME_IMPORT_DISPATCH_H

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "heap.h"
#include "native/registry.h"
#include "sbc_types.h"
#include "vm.h"

namespace Simple::VM::Runtime {

using Slot = uint64_t;

bool DispatchImportCallByName(const Simple::Byte::SbcModule& module,
                              const ExecOptions& options,
                              const Simple::VM::Native::NativeRegistry& native_registry,
                              Heap& heap,
                              std::vector<std::FILE*>& open_files,
                              std::vector<Simple::VM::Native::NativeHandleId>& file_handles,
                              Simple::VM::Native::NativeResourceRegistry& resource_registry,
                              std::string& dl_last_error,
                              uint32_t func_id,
                              const std::vector<Slot>& args,
                              Slot& out_ret,
                              bool& out_has_ret,
                              std::string& out_error);

} // namespace Simple::VM::Runtime

#endif // SIMPLE_VM_RUNTIME_IMPORT_DISPATCH_H
