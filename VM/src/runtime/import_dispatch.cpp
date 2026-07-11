#include "runtime/import_dispatch.h"

#include "ffi/dl_call.h"
#include "native/dispatch.h"
#include "runtime/values.h"
#include "sbc_loader.h"

namespace Simple::VM::Runtime {

using Simple::Byte::TypeKind;

bool DispatchImportCallByName(const Simple::Byte::SbcModule& module,
                              const ExecOptions& options,
                              const Simple::VM::Native::NativeRegistry& native_registry,
                              Heap& heap,
                              std::vector<Simple::VM::Native::NativeHandleId>& file_handles,
                              Simple::VM::Native::NativeResourceRegistry& resource_registry,
                              std::string& dl_last_error,
                              uint32_t func_id,
                              const std::vector<Slot>& args,
                              Slot& out_ret,
                              bool& out_has_ret,
                              std::string& out_error) {
  if (module.imports.empty()) {
    out_error = "import not supported";
    return false;
  }
  size_t import_base = module.functions.size() - module.imports.size();
  if (func_id < import_base) {
    out_error = "import not supported";
    return false;
  }
  size_t import_index = func_id - import_base;
  if (import_index >= module.imports.size()) {
    out_error = "import index out of range";
    return false;
  }
  const Simple::Byte::ImportRow& row = module.imports[import_index];
  std::string mod = Simple::Byte::ReadConstPoolString(module, row.module_name_str);
  std::string sym = Simple::Byte::ReadConstPoolString(module, row.symbol_name_str);
  if (mod.empty() || sym.empty()) {
    out_error = "import name invalid";
    return false;
  }
  if (mod == "System.io" || mod == "System.fs" || mod == "System.path" ||
      mod == "System.env" || mod == "System.os" || mod == "System.dl" ||
      mod == "System.buffer" || mod == "System.json" || mod == "System.log" ||
      mod == "System.random" || mod == "System.thread" || mod == "System.channel") {
    out_error = "stale lowercase native module name: " + mod;
    return false;
  }
  if (func_id >= module.functions.size()) {
    out_error = "import function id invalid";
    return false;
  }
  const auto& func = module.functions[func_id];
  if (func.method_id >= module.methods.size()) {
    out_error = "import method id invalid";
    return false;
  }
  const auto& method = module.methods[func.method_id];
  if (method.sig_id >= module.sigs.size()) {
    out_error = "import signature id invalid";
    return false;
  }
  const auto& sig = module.sigs[method.sig_id];
  out_has_ret = (sig.ret_type_id != 0xFFFFFFFFu);
  TypeKind ret_kind = TypeKind::Unspecified;
  if (out_has_ret) {
    if (sig.ret_type_id >= module.types.size()) {
      out_error = "import return type out of range";
      return false;
    }
    ret_kind = static_cast<TypeKind>(module.types[sig.ret_type_id].kind);
  }
  if (options.import_resolver) {
    Slot custom_ret = out_ret;
    bool custom_has_ret = out_has_ret;
    std::string custom_error;
    if (options.import_resolver(mod, sym, args, custom_ret, custom_has_ret, custom_error)) {
      out_ret = custom_ret;
      out_has_ret = custom_has_ret;
      return true;
    }
    if (!custom_error.empty()) {
      out_error = custom_error;
      return false;
    }
  }
  Simple::VM::Native::MetadataDispatchContext native_context;
  native_context.heap = &heap;
  native_context.argv = &options.argv;
  native_context.file_handles = &file_handles;
  native_context.dl_last_error = &dl_last_error;
  native_context.resource_registry = &resource_registry;
  native_context.capability_policy = &options.capability_policy;
  if (Simple::VM::Native::DispatchMetadataImport(native_registry,
                                                 mod,
                                                 sym,
                                                 args,
                                                 ret_kind,
                                                 native_context,
                                                 &out_ret,
                                                 &out_has_ret,
                                                 &out_error)) {
    return out_error.empty();
  }
  if (mod == "System.FFI") {
    if (sym.rfind("call$", 0) == 0) {
      if (sig.param_count == 0) {
        out_error = "System.FFI.call signature missing function pointer";
        return false;
      }
      if (args.size() != sig.param_count) {
        out_error = "System.FFI.call arg count mismatch";
        return false;
      }
      uint32_t ptr_type_id = module.param_types[sig.param_type_start];
      if (ptr_type_id >= module.types.size()) {
        out_error = "System.FFI.call pointer type out of range";
        return false;
      }
      TypeKind ptr_kind = static_cast<TypeKind>(module.types[ptr_type_id].kind);
      if (ptr_kind != TypeKind::I64 && ptr_kind != TypeKind::U64) {
        out_error = "System.FFI.call first parameter must be i64/u64";
        return false;
      }
      int64_t ptr_bits = UnpackI64(args[0]);
      if (ptr_bits == 0) {
        if (dl_last_error.empty()) {
          dl_last_error = "System.FFI.call null ptr";
          out_error = "System.FFI.call null ptr";
        } else {
          out_error = "System.FFI.call null ptr: " + dl_last_error;
        }
        return false;
      }
      std::vector<uint32_t> arg_type_ids;
      arg_type_ids.reserve(sig.param_count > 0 ? static_cast<size_t>(sig.param_count - 1) : 0u);
      for (uint16_t i = 1; i < sig.param_count; ++i) {
        uint32_t type_id = module.param_types[sig.param_type_start + i];
        if (type_id >= module.types.size()) {
          out_error = "System.FFI.call parameter type out of range";
          return false;
        }
        arg_type_ids.push_back(type_id);
      }
      if (!Simple::VM::Ffi::DispatchDynamicDlCall(ptr_bits,
                                                  module,
                                                  sig.ret_type_id,
                                                  out_has_ret,
                                                  arg_type_ids,
                                                  args,
                                                  1,
                                                  heap,
                                                  &out_ret,
                                                  &out_error)) {
        return false;
      }
      dl_last_error.clear();
      return true;
    }
  }
  out_error = "import not supported: " + mod + "." + sym;
  return false;
}

} // namespace Simple::VM::Runtime
