#include "test_utils.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "heap.h"
#include "native/buffer.h"
#include "native/registry.h"

namespace Simple::VM::Tests {
namespace {

bool NativeRegistryMatchesCatalogAvailability() {
  using namespace Simple::Lang;
  const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  const std::unordered_set<std::string> internal_native_backing = {
      "System.OS.args_count", "System.OS.args_get", "System.OS.env_get",
      "System.OS.cwd_get", "System.OS.time_mono_ns", "System.OS.time_wall_ns",
      "System.OS.formatWallNs", "System.Random.range", "System.Log.info",
      "System.Log.warn", "System.Log.error",
  };
  for (const auto& spec : registry.Functions()) {
    if (!spec.library_module) return false;
    const auto symbol = ParseLibraryMember(*spec.library_module, spec.symbol_name);
    const std::string qualified = spec.module_name + "." + spec.symbol_name;
    if (!symbol) {
      if (internal_native_backing.find(qualified) == internal_native_backing.end()) {
        std::cerr << "unclassified internal native: " << qualified << "\n";
        return false;
      }
      continue;
    }
    const LibraryMemberMetadata metadata =
        GetLibraryMemberMetadata(*spec.library_module, spec.symbol_name);
    if (metadata.availability == LibraryApiAvailability::Implemented) {
      if (!metadata.signature) {
        std::cerr << "implemented native missing catalog signature: " << qualified << "\n";
        return false;
      }
    } else if (internal_native_backing.find(qualified) == internal_native_backing.end()) {
      std::cerr << "planned catalog member dispatches without internal-backing policy: "
                << qualified << "\n";
      return false;
    }
  }
  return true;
}

bool RunNativeRegistryModuleTest() {
  Simple::VM::Native::NativeRegistry registry;
  Simple::VM::Native::NativeFunctionSpec spec;
  spec.module_name = "System.test";
  spec.symbol_name = "id";
  spec.parameter_types = {Simple::Byte::TypeKind::I32};
  spec.result_type = Simple::Byte::TypeKind::I32;
  spec.handler = [](Simple::VM::Native::NativeCallContext& ctx) {
    Simple::VM::Native::NativeCallResult result;
    result.value = ctx.args.empty() ? 0 : ctx.args[0];
    return result;
  };
  if (!registry.Register(std::move(spec))) return false;
  if (registry.Register({})) return false;
  const auto* found = registry.Find("System.test", "id");
  if (!found || found->parameter_types.size() != 1 ||
      found->result_type != Simple::Byte::TypeKind::I32) {
    return false;
  }
  Simple::VM::Native::NativeCallContext ctx;
  ctx.args.push_back(123);
  const auto result = found->handler(ctx);
  Simple::VM::Native::NativeRegistry default_registry = Simple::VM::Native::BuildDefaultRegistry();
  std::string metadata_error;
  const bool default_metadata_valid =
      Simple::VM::Native::ValidateNativeRegistryMetadata(default_registry, &metadata_error);
  Simple::VM::Native::NativeFunctionSpec missing_doc_spec = *found;
  missing_doc_spec.doc_summary.clear();
  std::string missing_doc_error;
  const bool missing_doc_rejected =
      !Simple::VM::Native::ValidateNativeFunctionMetadata(missing_doc_spec, &missing_doc_error) &&
      missing_doc_error.find("missing doc summary") != std::string::npos;
  Simple::VM::Native::NativeFunctionSpec bad_layer_spec = *found;
  bad_layer_spec.doc_summary = "bad layer test";
  bad_layer_spec.layer = Simple::VM::Native::NativeLayer::Standard;
  std::string bad_layer_error;
  const bool bad_layer_rejected =
      !Simple::VM::Native::ValidateNativeFunctionMetadata(bad_layer_spec, &bad_layer_error) &&
      bad_layer_error.find("system layer") != std::string::npos;
  const std::string stdlib_markdown = Simple::VM::Native::GenerateStdLibMarkdown(default_registry);
  bool stdlib_markdown_complete = true;
  for (const auto& native_spec : default_registry.Functions()) {
    const std::string module_heading = "## " + native_spec.module_name;
    const std::string symbol_cell = "| `" + native_spec.symbol_name + "` |";
    if (stdlib_markdown.find(module_heading) == std::string::npos ||
        stdlib_markdown.find(symbol_cell) == std::string::npos) {
      stdlib_markdown_complete = false;
      break;
    }
  }
  const auto* random_i32 = default_registry.Find("System.Random", "i32");
  const auto* os_time = default_registry.Find("System.OS", "time_mono_ns");
  const auto* os_sleep = default_registry.Find("System.OS", "sleepMs");
  const auto* os_cwd = default_registry.Find("System.OS", "cwd_get");
  const auto* os_format = default_registry.Find("System.OS", "formatWallNs");
  const auto* os_args_count = default_registry.Find("System.OS", "args_count");
  const auto* os_args_get = default_registry.Find("System.OS", "args_get");
  const auto* os_env_get = default_registry.Find("System.OS", "env_get");
  const auto* path_join = default_registry.Find("System.Path", "join");
  const auto* path_basename = default_registry.Find("System.Path", "basename");
  const auto* path_normalize = default_registry.Find("System.Path", "normalize");
  const auto* path_exists = default_registry.Find("System.Path", "exists");
  const auto* fs_read_text = default_registry.Find("System.FS", "readText");
  const auto* fs_write_text = default_registry.Find("System.FS", "writeText");
  const auto* fs_read_bytes = default_registry.Find("System.FS", "readBytes");
  const auto* fs_write_bytes = default_registry.Find("System.FS", "writeBytes");
  const auto* fs_list_dir = default_registry.Find("System.FS", "listDir");
  const auto* fs_open = default_registry.Find("System.FS", "open");
  const auto* fs_read = default_registry.Find("System.FS", "read");
  const auto* fs_write = default_registry.Find("System.FS", "write");
  const auto* fs_close = default_registry.Find("System.FS", "close");
  const auto* fs_cwd = default_registry.Find("System.FS", "cwd");
  const auto* fs_copy = default_registry.Find("System.FS", "copy");
  const auto* fs_remove = default_registry.Find("System.FS", "remove");
  const auto* fs_mkdir = default_registry.Find("System.FS", "mkdir");
  const auto* fs_set_cwd = default_registry.Find("System.FS", "setCwd");
  const auto* thread_yield = default_registry.Find("System.Thread", "yield");
  const auto* thread_hw = default_registry.Find("System.Thread", "hardwareConcurrency");
  const auto* channel_new = default_registry.Find("System.Channel", "newI32");
  const auto* channel_send = default_registry.Find("System.Channel", "sendI32");
  const auto* channel_recv = default_registry.Find("System.Channel", "recvI32");
  const auto* channel_try_recv = default_registry.Find("System.Channel", "tryRecvI32");
  const auto* channel_pending = default_registry.Find("System.Channel", "pendingI32");
  const auto* channel_i64 = default_registry.Find("System.Channel", "recvI64");
  const auto* channel_pending_i64 = default_registry.Find("System.Channel", "pendingI64");
  const auto* channel_f32 = default_registry.Find("System.Channel", "recvF32");
  const auto* channel_f64 = default_registry.Find("System.Channel", "recvF64");
  const auto* channel_pending_f64 = default_registry.Find("System.Channel", "pendingF64");
  const auto* channel_bool = default_registry.Find("System.Channel", "recvBool");
  const auto* channel_pending_bool = default_registry.Find("System.Channel", "pendingBool");
  const auto* channel_string = default_registry.Find("System.Channel", "newString");
  const auto* channel_send_string = default_registry.Find("System.Channel", "sendString");
  const auto* channel_recv_string = default_registry.Find("System.Channel", "recvString");
  const auto* channel_pending_string = default_registry.Find("System.Channel", "pendingString");
  const auto* channel_bytes = default_registry.Find("System.Channel", "newBytes");
  const auto* channel_send_bytes = default_registry.Find("System.Channel", "sendBytes");
  const auto* channel_recv_bytes = default_registry.Find("System.Channel", "recvBytes");
  const auto* channel_pending_bytes = default_registry.Find("System.Channel", "pendingBytes");
  const auto* channel_close = default_registry.Find("System.Channel", "close");
  const auto* json_parse = default_registry.Find("System.Json", "parse");
  const auto* json_stringify = default_registry.Find("System.Json", "stringify");
  const auto* json_free = default_registry.Find("System.Json", "free");
  const auto* dl_open = default_registry.Find("System.FFI", "open");
  const auto* dl_sym = default_registry.Find("System.FFI", "sym");
  const auto* dl_close = default_registry.Find("System.FFI", "close");
  const auto* dl_last_error = default_registry.Find("System.FFI", "last_error");
  const auto* log_set_level = default_registry.Find("System.Log", "setLevel");
  const auto* log_set_file = default_registry.Find("System.Log", "setFile");
  const auto* log_emit = default_registry.Find("System.Log", "log");
  const auto* log_info = default_registry.Find("System.Log", "info");
  const auto* env_args_count = default_registry.Find("System.Env", "argsCount");
  const auto* env_arg = default_registry.Find("System.Env", "arg");
  const auto* env_get = default_registry.Find("System.Env", "get");
  const auto* env_set = default_registry.Find("System.Env", "set");
  const auto* os_platform = default_registry.Find("System.OS", "platform");
  const auto* os_arch = default_registry.Find("System.OS", "arch");
  const auto* env_unset = default_registry.Find("System.Env", "unset");
  const auto* env_exe = default_registry.Find("System.Env", "exePath");
  const auto* io_buffer_new = default_registry.Find("System.IO", "buffer_new");
  const auto* io_buffer_len = default_registry.Find("System.IO", "buffer_len");
  const auto* io_buffer_fill = default_registry.Find("System.IO", "buffer_fill");
  const auto* io_buffer_copy = default_registry.Find("System.IO", "buffer_copy");
  const auto* buffer_new = default_registry.Find("System.Buffer", "new");
  const auto* buffer_len = default_registry.Find("System.Buffer", "len");
  const auto* buffer_write = default_registry.Find("System.Buffer", "writeU16LE");
  const auto* buffer_read = default_registry.Find("System.Buffer", "readU16LE");
  const auto* buffer_slice = default_registry.Find("System.Buffer", "slice");
  const auto* buffer_copy = default_registry.Find("System.Buffer", "copy");
  const std::vector<std::string> os_metadata_argv = {"simple", "legacy-arg"};
  Simple::VM::Native::NativeCallContext os_args_count_ctx;
  os_args_count_ctx.argv = &os_metadata_argv;
  const auto os_args_count_result = os_args_count ? os_args_count->handler(os_args_count_ctx)
                                                  : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext os_args_get_ctx;
  os_args_get_ctx.argv = &os_metadata_argv;
  os_args_get_ctx.args = {1};
  const auto os_args_get_result = os_args_get ? os_args_get->handler(os_args_get_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext os_cwd_ctx;
  const auto os_cwd_result = os_cwd ? os_cwd->handler(os_cwd_ctx)
                                    : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext os_format_ctx;
  os_format_ctx.args = {0};
  const auto os_format_result = os_format ? os_format->handler(os_format_ctx)
                                          : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext env_ctx;
  const auto os_platform_result = os_platform ? os_platform->handler(env_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  const auto os_arch_result = os_arch ? os_arch->handler(env_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  const auto env_exe_result = env_exe ? env_exe->handler(env_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  Heap metadata_heap;
  auto make_metadata_string = [&](const std::string& value) -> uint32_t {
    const uint32_t handle = metadata_heap.Allocate(ObjectKind::String, 0,
                                                   4u + static_cast<uint32_t>(value.size()) * 2u);
    HeapObject* obj = metadata_heap.Get(handle);
    if (!obj) return 0xffffffffu;
    WriteU32Payload(obj->payload, 0, static_cast<uint32_t>(value.size()));
    for (size_t i = 0; i < value.size(); ++i) {
      const size_t offset = 4u + i * 2u;
      obj->payload[offset] = static_cast<uint8_t>(value[i]);
      obj->payload[offset + 1] = 0;
    }
    return handle;
  };
  const auto read_test_u32 = [](const std::vector<uint8_t>& payload, size_t offset) -> uint32_t {
    if (offset + 4 > payload.size()) return 0;
    return payload[offset] | (static_cast<uint32_t>(payload[offset + 1]) << 8u) |
           (static_cast<uint32_t>(payload[offset + 2]) << 16u) |
           (static_cast<uint32_t>(payload[offset + 3]) << 24u);
  };
  const std::vector<std::string> metadata_argv = {"simple", "arg-one"};
  Simple::VM::Native::NativeCallContext env_args_count_ctx;
  env_args_count_ctx.argv = &metadata_argv;
  const auto env_args_count_result = env_args_count ? env_args_count->handler(env_args_count_ctx)
                                                    : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext env_arg_ctx;
  env_arg_ctx.argv = &metadata_argv;
  env_arg_ctx.args = {1};
  const auto env_arg_result = env_arg ? env_arg->handler(env_arg_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  const uint32_t env_name = make_metadata_string(
      "SIMPLE_NATIVE_REGISTRY_ENV_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const uint32_t env_value = make_metadata_string("metadata-env");
  Simple::VM::Native::NativeCallContext env_set_ctx;
  env_set_ctx.heap = &metadata_heap;
  env_set_ctx.args = {env_name, env_value};
  const auto env_set_result = env_set ? env_set->handler(env_set_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext env_get_ctx;
  env_get_ctx.heap = &metadata_heap;
  env_get_ctx.args = {env_name};
  const auto env_get_result = env_get ? env_get->handler(env_get_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext os_env_get_ctx;
  os_env_get_ctx.heap = &metadata_heap;
  os_env_get_ctx.args = {env_name};
  const auto os_env_get_result = os_env_get ? os_env_get->handler(os_env_get_ctx)
                                            : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext env_unset_ctx;
  env_unset_ctx.heap = &metadata_heap;
  env_unset_ctx.args = {env_name};
  const auto env_unset_result = env_unset ? env_unset->handler(env_unset_ctx)
                                          : Simple::VM::Native::NativeCallResult{};
  const uint32_t json_text = make_metadata_string("{\"ok\":true}");
  Simple::VM::Native::NativeCallContext json_parse_ctx;
  json_parse_ctx.heap = &metadata_heap;
  json_parse_ctx.args = {json_text};
  const auto json_parse_result = json_parse ? json_parse->handler(json_parse_ctx)
                                            : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext json_stringify_ctx;
  json_stringify_ctx.args = {json_parse_result.value};
  const auto json_stringify_result = json_stringify ? json_stringify->handler(json_stringify_ctx)
                                                    : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext json_free_ctx;
  json_free_ctx.args = {json_parse_result.value};
  const auto json_free_result = json_free ? json_free->handler(json_free_ctx)
                                          : Simple::VM::Native::NativeCallResult{};
  const uint32_t dl_missing_path = make_metadata_string("build/simple_missing_native_registry_library.so");
  std::string metadata_dl_error;
  Simple::VM::Native::NativeCallContext dl_open_ctx;
  dl_open_ctx.heap = &metadata_heap;
  dl_open_ctx.dl_last_error = &metadata_dl_error;
  dl_open_ctx.args = {dl_missing_path};
  const auto dl_open_result = dl_open ? dl_open->handler(dl_open_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext dl_sym_ctx;
  dl_sym_ctx.heap = &metadata_heap;
  dl_sym_ctx.dl_last_error = &metadata_dl_error;
  dl_sym_ctx.args = {0, dl_missing_path};
  const auto dl_sym_result = dl_sym ? dl_sym->handler(dl_sym_ctx)
                                    : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext dl_last_error_ctx;
  dl_last_error_ctx.dl_last_error = &metadata_dl_error;
  const auto dl_last_error_result = dl_last_error ? dl_last_error->handler(dl_last_error_ctx)
                                                  : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext dl_close_ctx;
  dl_close_ctx.dl_last_error = &metadata_dl_error;
  dl_close_ctx.args = {0};
  const auto dl_close_result = dl_close ? dl_close->handler(dl_close_ctx)
                                        : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext channel_new_string_ctx;
  const auto channel_new_string_result = channel_string ? channel_string->handler(channel_new_string_ctx)
                                                        : Simple::VM::Native::NativeCallResult{};
  const uint32_t channel_message = make_metadata_string("metadata-channel");
  Simple::VM::Native::NativeCallContext channel_send_string_ctx;
  channel_send_string_ctx.heap = &metadata_heap;
  channel_send_string_ctx.args = {channel_new_string_result.value, channel_message};
  const auto channel_send_string_result = channel_send_string ? channel_send_string->handler(channel_send_string_ctx)
                                                              : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext channel_pending_string_ctx;
  channel_pending_string_ctx.args = {channel_new_string_result.value};
  const auto channel_pending_string_result = channel_pending_string ? channel_pending_string->handler(channel_pending_string_ctx)
                                                                    : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext channel_recv_string_ctx;
  channel_recv_string_ctx.args = {channel_new_string_result.value};
  const auto channel_recv_string_result = channel_recv_string ? channel_recv_string->handler(channel_recv_string_ctx)
                                                              : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext channel_new_bytes_ctx;
  const auto channel_new_bytes_result = channel_bytes ? channel_bytes->handler(channel_new_bytes_ctx)
                                                      : Simple::VM::Native::NativeCallResult{};
  const uint32_t channel_bytes_ref = CreateBytes(metadata_heap, {65, 66});
  HeapObject* channel_bytes_obj = metadata_heap.Get(channel_bytes_ref);
  if (!channel_bytes_obj || channel_bytes_obj->header.kind != ObjectKind::Bytes) return false;
  Simple::VM::Native::NativeCallContext channel_send_bytes_ctx;
  channel_send_bytes_ctx.heap = &metadata_heap;
  channel_send_bytes_ctx.args = {channel_new_bytes_result.value, channel_bytes_ref};
  const auto channel_send_bytes_result = channel_send_bytes ? channel_send_bytes->handler(channel_send_bytes_ctx)
                                                            : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext channel_pending_bytes_ctx;
  channel_pending_bytes_ctx.args = {channel_new_bytes_result.value};
  const auto channel_pending_bytes_result = channel_pending_bytes ? channel_pending_bytes->handler(channel_pending_bytes_ctx)
                                                                  : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext channel_recv_bytes_ctx;
  channel_recv_bytes_ctx.heap = &metadata_heap;
  channel_recv_bytes_ctx.args = {channel_new_bytes_result.value};
  const auto channel_recv_bytes_result = channel_recv_bytes ? channel_recv_bytes->handler(channel_recv_bytes_ctx)
                                                            : Simple::VM::Native::NativeCallResult{};
  HeapObject* channel_recv_bytes_obj = metadata_heap.Get(static_cast<uint32_t>(channel_recv_bytes_result.value));
  if (!channel_recv_bytes_obj) return false;
  if (channel_recv_bytes_obj->header.kind != ObjectKind::List) return false;
  const uint32_t channel_recv_bytes_len = read_test_u32(channel_recv_bytes_obj->payload, 0);
  const uint32_t channel_recv_bytes_first = read_test_u32(channel_recv_bytes_obj->payload, 8);
  const uint32_t log_empty_path = make_metadata_string("");
  const uint32_t log_message = make_metadata_string("metadata log suppressed");
  Simple::VM::Native::NativeCallContext log_level_ctx;
  log_level_ctx.args = {99};
  if (log_set_level) (void)log_set_level->handler(log_level_ctx);
  Simple::VM::Native::NativeCallContext log_set_file_ctx;
  log_set_file_ctx.heap = &metadata_heap;
  log_set_file_ctx.args = {log_empty_path};
  const auto log_set_file_result = log_set_file ? log_set_file->handler(log_set_file_ctx)
                                                : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext log_emit_ctx;
  log_emit_ctx.heap = &metadata_heap;
  log_emit_ctx.args = {log_message, 1};
  const auto log_emit_result = log_emit ? log_emit->handler(log_emit_ctx)
                                        : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext log_info_ctx;
  log_info_ctx.heap = &metadata_heap;
  log_info_ctx.args = {log_message};
  const auto log_info_result = log_info ? log_info->handler(log_info_ctx)
                                        : Simple::VM::Native::NativeCallResult{};
  log_level_ctx.args = {0};
  if (log_set_level) (void)log_set_level->handler(log_level_ctx);
  const uint32_t path_left = make_metadata_string("/tmp");
  const uint32_t path_right = make_metadata_string("a/../b.txt");
  Simple::VM::Native::NativeCallContext path_join_ctx;
  path_join_ctx.heap = &metadata_heap;
  path_join_ctx.args = {path_left, path_right};
  const auto path_join_result = path_join ? path_join->handler(path_join_ctx)
                                          : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext path_basename_ctx;
  path_basename_ctx.heap = &metadata_heap;
  path_basename_ctx.args = {path_right};
  const auto path_basename_result = path_basename ? path_basename->handler(path_basename_ctx)
                                                  : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext path_normalize_ctx;
  path_normalize_ctx.heap = &metadata_heap;
  path_normalize_ctx.args = {path_right};
  const auto path_normalize_result = path_normalize ? path_normalize->handler(path_normalize_ctx)
                                                    : Simple::VM::Native::NativeCallResult{};
  const uint32_t dot_path = make_metadata_string(".");
  Simple::VM::Native::NativeCallContext path_exists_ctx;
  path_exists_ctx.heap = &metadata_heap;
  path_exists_ctx.args = {dot_path};
  const auto path_exists_result = path_exists ? path_exists->handler(path_exists_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  const std::string fs_base = "build/native_registry_fs_" +
                              std::to_string(std::chrono::steady_clock::now()
                                                 .time_since_epoch()
                                                 .count());
  const std::string fs_dir = fs_base + "_dir";
  const std::string fs_src = fs_base + "_src.txt";
  const std::string fs_dst = fs_base + "_dst.txt";
  const std::string fs_text = fs_base + "_text.txt";
  const std::string fs_bytes = fs_base + "_bytes.bin";
  const std::string fs_fd = fs_base + "_fd.bin";
  {
    std::ofstream out(fs_src, std::ios::binary);
    out << "metadata";
  }
  const uint32_t fs_dir_ref = make_metadata_string(fs_dir);
  const uint32_t fs_src_ref = make_metadata_string(fs_src);
  const uint32_t fs_dst_ref = make_metadata_string(fs_dst);
  const uint32_t fs_text_ref = make_metadata_string(fs_text);
  const uint32_t fs_bytes_ref = make_metadata_string(fs_bytes);
  const uint32_t fs_fd_ref = make_metadata_string(fs_fd);
  const uint32_t fs_text_value_ref = make_metadata_string("hello metadata");
  const uint32_t fs_bytes_value_ref = CreateBytes(metadata_heap, {65, 66, 67});
  HeapObject* fs_bytes_value = metadata_heap.Get(fs_bytes_value_ref);
  if (!fs_bytes_value || fs_bytes_value->header.kind != ObjectKind::Bytes) return false;
  const uint32_t fs_fd_write_buf_ref = metadata_heap.Allocate(ObjectKind::Array, 0, 4u + 3u * 4u);
  HeapObject* fs_fd_write_buf = metadata_heap.Get(fs_fd_write_buf_ref);
  if (!fs_fd_write_buf) return false;
  WriteU32Payload(fs_fd_write_buf->payload, 0, 3);
  WriteU32Payload(fs_fd_write_buf->payload, 4, 88);
  WriteU32Payload(fs_fd_write_buf->payload, 8, 89);
  WriteU32Payload(fs_fd_write_buf->payload, 12, 90);
  const uint32_t fs_fd_read_buf_ref = metadata_heap.Allocate(ObjectKind::Array, 0, 4u + 3u * 4u);
  HeapObject* fs_fd_read_buf = metadata_heap.Get(fs_fd_read_buf_ref);
  if (!fs_fd_read_buf) return false;
  WriteU32Payload(fs_fd_read_buf->payload, 0, 3);
  Simple::VM::Native::NativeCallContext fs_write_text_ctx;
  fs_write_text_ctx.heap = &metadata_heap;
  fs_write_text_ctx.args = {fs_text_ref, fs_text_value_ref};
  const auto fs_write_text_result = fs_write_text ? fs_write_text->handler(fs_write_text_ctx)
                                                  : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_read_text_ctx;
  fs_read_text_ctx.heap = &metadata_heap;
  fs_read_text_ctx.args = {fs_text_ref};
  const auto fs_read_text_result = fs_read_text ? fs_read_text->handler(fs_read_text_ctx)
                                                : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_write_bytes_ctx;
  fs_write_bytes_ctx.heap = &metadata_heap;
  fs_write_bytes_ctx.args = {fs_bytes_ref, fs_bytes_value_ref};
  const auto fs_write_bytes_result = fs_write_bytes ? fs_write_bytes->handler(fs_write_bytes_ctx)
                                                    : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_read_bytes_ctx;
  fs_read_bytes_ctx.heap = &metadata_heap;
  fs_read_bytes_ctx.args = {fs_bytes_ref};
  const auto fs_read_bytes_result = fs_read_bytes ? fs_read_bytes->handler(fs_read_bytes_ctx)
                                                  : Simple::VM::Native::NativeCallResult{};
  HeapObject* fs_read_bytes_obj = metadata_heap.Get(static_cast<uint32_t>(fs_read_bytes_result.value));
  if (!fs_read_bytes_obj) return false;
  if (fs_read_bytes_obj->header.kind != ObjectKind::List) return false;
  const uint32_t fs_read_bytes_len = read_test_u32(fs_read_bytes_obj->payload, 0);
  const uint32_t fs_read_bytes_first = read_test_u32(fs_read_bytes_obj->payload, 8);
  Simple::VM::Native::NativeCallContext io_new_ctx;
  io_new_ctx.heap = &metadata_heap;
  io_new_ctx.args = {3};
  const auto io_new_result = io_buffer_new ? io_buffer_new->handler(io_new_ctx)
                                           : Simple::VM::Native::NativeCallResult{};
  const uint32_t io_dst_ref = static_cast<uint32_t>(io_new_result.value);
  HeapObject* io_dst_obj = metadata_heap.Get(io_dst_ref);
  if (!io_dst_obj) return false;
  Simple::VM::Native::NativeCallContext io_fill_ctx;
  io_fill_ctx.heap = &metadata_heap;
  io_fill_ctx.args = {io_dst_ref, 7, 3};
  const auto io_fill_result = io_buffer_fill ? io_buffer_fill->handler(io_fill_ctx)
                                             : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext io_len_ctx;
  io_len_ctx.heap = &metadata_heap;
  io_len_ctx.args = {io_dst_ref};
  const auto io_len_result = io_buffer_len ? io_buffer_len->handler(io_len_ctx)
                                           : Simple::VM::Native::NativeCallResult{};
  const uint32_t io_src_ref = metadata_heap.Allocate(ObjectKind::Array, 0, 4u + 3u * 4u);
  HeapObject* io_src_obj = metadata_heap.Get(io_src_ref);
  if (!io_src_obj) return false;
  WriteU32Payload(io_src_obj->payload, 0, 3);
  WriteU32Payload(io_src_obj->payload, 4, 11);
  WriteU32Payload(io_src_obj->payload, 8, 12);
  WriteU32Payload(io_src_obj->payload, 12, 13);
  Simple::VM::Native::NativeCallContext io_copy_ctx;
  io_copy_ctx.heap = &metadata_heap;
  io_copy_ctx.args = {io_dst_ref, io_src_ref, 2};
  const auto io_copy_result = io_buffer_copy ? io_buffer_copy->handler(io_copy_ctx)
                                             : Simple::VM::Native::NativeCallResult{};
  const uint32_t io_dst_first = read_test_u32(metadata_heap.Get(io_dst_ref)->payload, 8);
  Simple::VM::Native::NativeCallContext fs_list_dir_ctx;
  fs_list_dir_ctx.heap = &metadata_heap;
  fs_list_dir_ctx.args = {dot_path};
  const auto fs_list_dir_result = fs_list_dir ? fs_list_dir->handler(fs_list_dir_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  HeapObject* fs_list_dir_obj = metadata_heap.Get(static_cast<uint32_t>(fs_list_dir_result.value));
  if (!fs_list_dir_obj) return false;
  const uint32_t fs_list_dir_len = read_test_u32(fs_list_dir_obj->payload, 0);
  Simple::VM::Native::NativeResourceRegistry metadata_resources;
  std::vector<Simple::VM::Native::NativeHandleId> metadata_file_handles;
  Simple::VM::Native::NativeCallContext fs_open_write_ctx;
  fs_open_write_ctx.heap = &metadata_heap;
  fs_open_write_ctx.resource_registry = &metadata_resources;
  fs_open_write_ctx.file_handles = &metadata_file_handles;
  fs_open_write_ctx.args = {fs_fd_ref, 1};
  const auto fs_open_write_result = fs_open ? fs_open->handler(fs_open_write_ctx)
                                            : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_write_ctx;
  fs_write_ctx.heap = &metadata_heap;
  fs_write_ctx.resource_registry = &metadata_resources;
  fs_write_ctx.file_handles = &metadata_file_handles;
  fs_write_ctx.args = {fs_open_write_result.value, fs_fd_write_buf_ref, 3};
  const auto fs_write_result = fs_write ? fs_write->handler(fs_write_ctx)
                                        : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_close_write_ctx;
  fs_close_write_ctx.resource_registry = &metadata_resources;
  fs_close_write_ctx.file_handles = &metadata_file_handles;
  fs_close_write_ctx.args = {fs_open_write_result.value};
  const auto fs_close_write_result = fs_close ? fs_close->handler(fs_close_write_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_open_read_ctx;
  fs_open_read_ctx.heap = &metadata_heap;
  fs_open_read_ctx.resource_registry = &metadata_resources;
  fs_open_read_ctx.file_handles = &metadata_file_handles;
  fs_open_read_ctx.args = {fs_fd_ref, 0};
  const auto fs_open_read_result = fs_open ? fs_open->handler(fs_open_read_ctx)
                                           : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_read_ctx;
  fs_read_ctx.heap = &metadata_heap;
  fs_read_ctx.resource_registry = &metadata_resources;
  fs_read_ctx.file_handles = &metadata_file_handles;
  fs_read_ctx.args = {fs_open_read_result.value, fs_fd_read_buf_ref, 3};
  const auto fs_read_result = fs_read ? fs_read->handler(fs_read_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_close_read_ctx;
  fs_close_read_ctx.resource_registry = &metadata_resources;
  fs_close_read_ctx.file_handles = &metadata_file_handles;
  fs_close_read_ctx.args = {fs_open_read_result.value};
  const auto fs_close_read_result = fs_close ? fs_close->handler(fs_close_read_ctx)
                                             : Simple::VM::Native::NativeCallResult{};
  HeapObject* fs_fd_read_result_buf = metadata_heap.Get(fs_fd_read_buf_ref);
  if (!fs_fd_read_result_buf) return false;
  const uint32_t fs_fd_read_first = read_test_u32(fs_fd_read_result_buf->payload, 4);
  Simple::VM::Native::NativeCallContext fs_cwd_ctx;
  const auto fs_cwd_result = fs_cwd ? fs_cwd->handler(fs_cwd_ctx)
                                    : Simple::VM::Native::NativeCallResult{};
  const uint32_t fs_cwd_ref = make_metadata_string(fs_cwd_result.string_value);
  Simple::VM::Native::NativeCallContext fs_mkdir_ctx;
  fs_mkdir_ctx.heap = &metadata_heap;
  fs_mkdir_ctx.args = {fs_dir_ref};
  const auto fs_mkdir_result = fs_mkdir ? fs_mkdir->handler(fs_mkdir_ctx)
                                        : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_set_cwd_ctx;
  fs_set_cwd_ctx.heap = &metadata_heap;
  fs_set_cwd_ctx.args = {fs_cwd_ref};
  const auto fs_set_cwd_result = fs_set_cwd ? fs_set_cwd->handler(fs_set_cwd_ctx)
                                            : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_copy_ctx;
  fs_copy_ctx.heap = &metadata_heap;
  fs_copy_ctx.args = {fs_src_ref, fs_dst_ref};
  const auto fs_copy_result = fs_copy ? fs_copy->handler(fs_copy_ctx)
                                      : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_remove_dst_ctx;
  fs_remove_dst_ctx.heap = &metadata_heap;
  fs_remove_dst_ctx.args = {fs_dst_ref};
  const auto fs_remove_dst_result = fs_remove ? fs_remove->handler(fs_remove_dst_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext fs_remove_src_ctx;
  fs_remove_src_ctx.heap = &metadata_heap;
  fs_remove_src_ctx.args = {fs_src_ref};
  if (fs_remove) (void)fs_remove->handler(fs_remove_src_ctx);
  Simple::VM::Native::NativeCallContext fs_remove_text_ctx;
  fs_remove_text_ctx.heap = &metadata_heap;
  fs_remove_text_ctx.args = {fs_text_ref};
  if (fs_remove) (void)fs_remove->handler(fs_remove_text_ctx);
  Simple::VM::Native::NativeCallContext fs_remove_bytes_ctx;
  fs_remove_bytes_ctx.heap = &metadata_heap;
  fs_remove_bytes_ctx.args = {fs_bytes_ref};
  if (fs_remove) (void)fs_remove->handler(fs_remove_bytes_ctx);
  Simple::VM::Native::NativeCallContext fs_remove_fd_ctx;
  fs_remove_fd_ctx.heap = &metadata_heap;
  fs_remove_fd_ctx.args = {fs_fd_ref};
  if (fs_remove) (void)fs_remove->handler(fs_remove_fd_ctx);
  Simple::VM::Native::NativeCallContext fs_remove_dir_ctx;
  fs_remove_dir_ctx.heap = &metadata_heap;
  fs_remove_dir_ctx.args = {fs_dir_ref};
  if (fs_remove) (void)fs_remove->handler(fs_remove_dir_ctx);
  Simple::VM::Native::NativeCallContext buffer_new_ctx;
  buffer_new_ctx.heap = &metadata_heap;
  buffer_new_ctx.args = {2};
  const auto buffer_new_result = buffer_new ? buffer_new->handler(buffer_new_ctx)
                                            : Simple::VM::Native::NativeCallResult{};
  if (!metadata_heap.Get(static_cast<uint32_t>(buffer_new_result.value))) return false;
  const uint32_t buffer_ref = metadata_heap.Allocate(ObjectKind::List, 0, 8u + 3u * 4u);
  HeapObject* buffer_obj = metadata_heap.Get(buffer_ref);
  if (!buffer_obj) return false;
  WriteU32Payload(buffer_obj->payload, 0, 3);
  WriteU32Payload(buffer_obj->payload, 4, 3);
  Simple::VM::Native::NativeCallContext buffer_ctx;
  buffer_ctx.heap = &metadata_heap;
  buffer_ctx.args = {buffer_ref};
  const auto buffer_result = buffer_len ? buffer_len->handler(buffer_ctx)
                                        : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext buffer_write_ctx;
  buffer_write_ctx.heap = &metadata_heap;
  buffer_write_ctx.args = {buffer_ref, 1, 0x1234u};
  const auto buffer_write_result = buffer_write ? buffer_write->handler(buffer_write_ctx)
                                                : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext buffer_read_ctx;
  buffer_read_ctx.heap = &metadata_heap;
  buffer_read_ctx.args = {buffer_ref, 1};
  const auto buffer_read_result = buffer_read ? buffer_read->handler(buffer_read_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  const uint32_t copy_ref = metadata_heap.Allocate(ObjectKind::List, 0, 8u + 3u * 4u);
  HeapObject* copy_obj = metadata_heap.Get(copy_ref);
  if (!copy_obj) return false;
  WriteU32Payload(copy_obj->payload, 0, 3);
  WriteU32Payload(copy_obj->payload, 4, 3);
  Simple::VM::Native::NativeCallContext buffer_copy_ctx;
  buffer_copy_ctx.heap = &metadata_heap;
  buffer_copy_ctx.args = {copy_ref, 0, buffer_ref, 1, 2};
  const auto buffer_copy_result = buffer_copy ? buffer_copy->handler(buffer_copy_ctx)
                                              : Simple::VM::Native::NativeCallResult{};
  Simple::VM::Native::NativeCallContext buffer_slice_ctx;
  buffer_slice_ctx.heap = &metadata_heap;
  buffer_slice_ctx.args = {buffer_ref, 1, 2};
  const auto buffer_slice_result = buffer_slice ? buffer_slice->handler(buffer_slice_ctx)
                                                : Simple::VM::Native::NativeCallResult{};
  HeapObject* slice_obj = metadata_heap.Get(static_cast<uint32_t>(buffer_slice_result.value));
  if (!slice_obj) return false;
  return registry.Size() == 1 && result.ok && result.value == 123 &&
         default_metadata_valid && missing_doc_rejected && bad_layer_rejected &&
         stdlib_markdown_complete &&
         stdlib_markdown.find("## System.FS") != std::string::npos &&
         stdlib_markdown.find("| `readText` | `System.FS` | `implemented` | `system` | `(string) -> string` | `may-block` | `vm-alloc` | `may-safepoint` | `-` | `filesystem.read` | `-` |") != std::string::npos &&
         stdlib_markdown.find("| `buffer_copy` | `System.IO` | `implemented` | `system` | `(ref, ref, i32) -> i32` | `non-blocking` | `no-alloc` | `no-safepoint` | `-` | `-` | `-` |") != std::string::npos &&
         stdlib_markdown.find("out:file:to-caller:vm-shutdown") != std::string::npos &&
         random_i32 && os_time &&
         os_sleep && os_cwd && os_format && os_args_count && os_args_get && os_env_get &&
         os_args_count_result.value == 2 && os_args_get_result.string_value == "legacy-arg" &&
         os_env_get_result.string_value == "metadata-env" && path_join && path_basename && path_normalize &&
         path_exists && fs_read_text && fs_write_text && fs_read_bytes && fs_write_bytes &&
         fs_list_dir && fs_open && fs_read && fs_write && fs_close && fs_cwd && fs_copy &&
         fs_remove && fs_mkdir && fs_set_cwd &&
         !os_cwd_result.string_value.empty() &&
         !os_format_result.string_value.empty() &&
         path_join_result.string_value == "/tmp/b.txt" &&
         path_basename_result.string_value == "b.txt" &&
         path_normalize_result.string_value == "b.txt" && path_exists_result.value == 1 &&
         fs_write_text_result.value == 1 && fs_read_text_result.string_value == "hello metadata" &&
         fs_write_bytes_result.value == 1 && fs_read_bytes_len == 3 && fs_read_bytes_first == 65 &&
         fs_list_dir_len > 0 && static_cast<int32_t>(fs_open_write_result.value) >= 0 &&
         fs_write_result.value == 3 && !fs_close_write_result.has_value &&
         static_cast<int32_t>(fs_open_read_result.value) >= 0 && fs_read_result.value == 3 &&
         !fs_close_read_result.has_value && fs_fd_read_first == 88 &&
         !fs_cwd_result.string_value.empty() && fs_mkdir_result.value == 1 &&
         fs_set_cwd_result.value == 1 && fs_copy_result.value == 1 &&
         fs_remove_dst_result.value == 1 && thread_yield &&
         thread_hw && channel_new && channel_send && channel_recv &&
         channel_try_recv && channel_pending && channel_i64 && channel_pending_i64 && channel_f32 &&
         channel_f64 && channel_pending_f64 && channel_bool && channel_pending_bool &&
         channel_string && channel_send_string && channel_recv_string && channel_pending_string &&
         channel_bytes && channel_send_bytes && channel_recv_bytes && channel_pending_bytes &&
         channel_send_string_result.value == 1 && channel_pending_string_result.value == 1 &&
         channel_recv_string_result.string_value == "metadata-channel" &&
         channel_send_bytes_result.value == 1 && channel_pending_bytes_result.value == 1 &&
         channel_recv_bytes_len == 2 && channel_recv_bytes_first == 65 &&
         channel_close && json_parse && json_stringify && json_free && json_parse_result.value != 0 &&
         json_stringify_result.string_value == "{\"ok\":true}" && json_free_result.value == 1 &&
         dl_open && dl_sym && dl_close && dl_last_error && dl_open_result.value == 0 &&
         dl_sym_result.value == 0 && !dl_last_error_result.string_value.empty() &&
         static_cast<int32_t>(dl_close_result.value) == -1 && log_set_level && log_set_file && log_emit && log_info &&
         log_set_file_result.value == 1 && !log_emit_result.has_value && !log_info_result.has_value &&
         env_args_count && env_arg && env_args_count_result.value == 2 &&
         env_arg_result.string_value == "arg-one" && env_get && env_set &&
         env_set_result.value == 1 && env_get_result.string_value == "metadata-env" &&
         env_unset && env_unset_result.value == 1 && os_platform && os_arch && env_exe && io_buffer_new && io_buffer_len &&
         io_buffer_fill && io_buffer_copy && io_new_result.ok && io_len_result.value == 3 &&
         io_fill_result.value == 3 && io_copy_result.value == 2 && io_dst_first == 11 &&
         buffer_new && buffer_len && buffer_write && buffer_read && buffer_slice && buffer_copy &&
         !os_platform_result.string_value.empty() && !os_arch_result.string_value.empty() &&
         !env_exe_result.string_value.empty() && buffer_new_result.ok && buffer_result.value == 3 &&
         buffer_write_result.value == 1 &&
         buffer_read_result.value == 0x1234u && buffer_copy_result.value == 2 &&
         Simple::VM::Native::Buffer::ReadLE(copy_obj, 0, 2) == 0x1234u &&
         Simple::VM::Native::Buffer::ReadLE(slice_obj, 0, 2) == 0x1234u &&
         random_i32->result_type == Simple::Byte::TypeKind::I32 &&
         os_time->result_type == Simple::Byte::TypeKind::I64 &&
         os_sleep->result_type == Simple::Byte::TypeKind::Unspecified &&
         os_cwd->result_type == Simple::Byte::TypeKind::String &&
         os_format->result_type == Simple::Byte::TypeKind::String &&
         os_args_count->result_type == Simple::Byte::TypeKind::I32 &&
         os_args_get->result_type == Simple::Byte::TypeKind::String &&
         os_env_get->result_type == Simple::Byte::TypeKind::String &&
         path_join->result_type == Simple::Byte::TypeKind::String &&
         path_basename->result_type == Simple::Byte::TypeKind::String &&
         path_normalize->result_type == Simple::Byte::TypeKind::String &&
         path_exists->result_type == Simple::Byte::TypeKind::I32 &&
         fs_read_text->result_type == Simple::Byte::TypeKind::String &&
         fs_write_text->result_type == Simple::Byte::TypeKind::I32 &&
         fs_read_bytes->result_type == Simple::Byte::TypeKind::Ref &&
         fs_write_bytes->result_type == Simple::Byte::TypeKind::I32 &&
         fs_list_dir->result_type == Simple::Byte::TypeKind::Ref &&
         fs_open->result_type == Simple::Byte::TypeKind::I32 &&
         fs_read->result_type == Simple::Byte::TypeKind::I32 &&
         fs_write->result_type == Simple::Byte::TypeKind::I32 &&
         fs_close->result_type == Simple::Byte::TypeKind::Unspecified &&
         fs_cwd->result_type == Simple::Byte::TypeKind::String &&
         fs_copy->result_type == Simple::Byte::TypeKind::I32 &&
         fs_remove->result_type == Simple::Byte::TypeKind::I32 &&
         fs_mkdir->result_type == Simple::Byte::TypeKind::I32 &&
         fs_set_cwd->result_type == Simple::Byte::TypeKind::I32 &&
         thread_yield->result_type == Simple::Byte::TypeKind::Unspecified &&
         thread_hw->result_type == Simple::Byte::TypeKind::I32 &&
         channel_new->result_type == Simple::Byte::TypeKind::I64 &&
         channel_send->result_type == Simple::Byte::TypeKind::I32 &&
         channel_recv->result_type == Simple::Byte::TypeKind::I32 &&
         channel_try_recv->result_type == Simple::Byte::TypeKind::I32 &&
         channel_pending->result_type == Simple::Byte::TypeKind::I32 &&
         channel_i64->result_type == Simple::Byte::TypeKind::I64 &&
         channel_pending_i64->result_type == Simple::Byte::TypeKind::I32 &&
         channel_f32->result_type == Simple::Byte::TypeKind::F32 &&
         channel_f64->result_type == Simple::Byte::TypeKind::F64 &&
         channel_pending_f64->result_type == Simple::Byte::TypeKind::I32 &&
         channel_bool->result_type == Simple::Byte::TypeKind::Bool &&
         channel_pending_bool->result_type == Simple::Byte::TypeKind::I32 &&
         channel_string->result_type == Simple::Byte::TypeKind::I64 &&
         channel_send_string->result_type == Simple::Byte::TypeKind::I32 &&
         channel_recv_string->result_type == Simple::Byte::TypeKind::String &&
         channel_pending_string->result_type == Simple::Byte::TypeKind::I32 &&
         channel_bytes->result_type == Simple::Byte::TypeKind::I64 &&
         channel_send_bytes->result_type == Simple::Byte::TypeKind::I32 &&
         channel_recv_bytes->result_type == Simple::Byte::TypeKind::Ref &&
         channel_pending_bytes->result_type == Simple::Byte::TypeKind::I32 &&
         channel_close->result_type == Simple::Byte::TypeKind::Unspecified &&
         json_parse->result_type == Simple::Byte::TypeKind::I64 &&
         json_stringify->result_type == Simple::Byte::TypeKind::String &&
         json_free->result_type == Simple::Byte::TypeKind::I32 &&
         dl_open->result_type == Simple::Byte::TypeKind::I64 &&
         dl_sym->result_type == Simple::Byte::TypeKind::I64 &&
         dl_close->result_type == Simple::Byte::TypeKind::I32 &&
         dl_last_error->result_type == Simple::Byte::TypeKind::String &&
         buffer_new->result_type == Simple::Byte::TypeKind::Ref &&
         buffer_len->result_type == Simple::Byte::TypeKind::I32 &&
         buffer_write->result_type == Simple::Byte::TypeKind::I32 &&
         buffer_read->result_type == Simple::Byte::TypeKind::I32 &&
         buffer_slice->result_type == Simple::Byte::TypeKind::Ref &&
         buffer_copy->result_type == Simple::Byte::TypeKind::I32 &&
         log_set_level->result_type == Simple::Byte::TypeKind::Unspecified &&
         log_set_file->result_type == Simple::Byte::TypeKind::I32 &&
         log_emit->result_type == Simple::Byte::TypeKind::Unspecified &&
         log_info->result_type == Simple::Byte::TypeKind::Unspecified &&
         env_args_count->result_type == Simple::Byte::TypeKind::I32 &&
         env_arg->result_type == Simple::Byte::TypeKind::String &&
         env_get->result_type == Simple::Byte::TypeKind::String &&
         env_set->result_type == Simple::Byte::TypeKind::I32 &&
         env_unset->result_type == Simple::Byte::TypeKind::Bool &&
         io_buffer_new->result_type == Simple::Byte::TypeKind::Ref &&
         io_buffer_len->result_type == Simple::Byte::TypeKind::I32 &&
         io_buffer_fill->result_type == Simple::Byte::TypeKind::I32 &&
         io_buffer_copy->result_type == Simple::Byte::TypeKind::I32 &&
         os_platform->result_type == Simple::Byte::TypeKind::String &&
         os_arch->result_type == Simple::Byte::TypeKind::String &&
         env_exe->result_type == Simple::Byte::TypeKind::String;
}

const TestCase kVmNativeRegistryTests[] = {
    {"vm_native_registry_matches_catalog_availability", NativeRegistryMatchesCatalogAvailability},
    {"vm_native_registry_module", RunNativeRegistryModuleTest},
};

const TestSection kVmNativeRegistrySections[] = {
    {"vm_native_registry", kVmNativeRegistryTests,
     sizeof(kVmNativeRegistryTests) / sizeof(kVmNativeRegistryTests[0])},
};

} // namespace

const TestSection* GetVmNativeRegistrySections(size_t* count) {
  if (count) *count = sizeof(kVmNativeRegistrySections) / sizeof(kVmNativeRegistrySections[0]);
  return kVmNativeRegistrySections;
}

} // namespace Simple::VM::Tests
